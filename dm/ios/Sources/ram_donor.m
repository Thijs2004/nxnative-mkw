// Guest memory from helper processes (the technique of MeloNX 2.7's "RAM limit workaround", reimplemented here).
// The app embeds an app extension (dm/ios/RamDonor). Each extension process allocates memory in its own task and hands it back as xpc_shmem; the app remaps it at the guest
// heap's address. Shared memory owned by another task is not counted in the app's phys_footprint, which is what the iOS memory limit (jetsam) is based on, so a game whose heap
// is larger than the app's limit (Mario Kart World: a 7 GB root heap) can still run. Each donor has its own limit (os_proc_available_memory in the donor); up to DONOR_MAX are started.
//
// Two steps, because the guest address block is only reserved once the host starts (hle_svc.c mem_init_once):
//   dm_ram_donor_prepare(bytes)   boot thread, before dm_main: start donors and map up to bytes of their memory anywhere
//   dm_ram_donor_take(target, n)  hle_svc.c: move up to n of those bytes onto target (MAP_FIXED semantics, shared, not copied); returns the bytes moved
// DM_RAM_DONOR=<MB> sets how much to ask for (0 turns it off; default 7168). DM_RAM_DONOR_KEEP=1 keeps the donor processes alive (by default they are killed once mapped:
// the mappings keep the memory, and iOS cannot then kill a donor under pressure and take the pages with it).
@import Foundation;
#import <mach/mach.h>
#import <xpc/xpc.h>
#import <os/proc.h>
#import "ram_donor_proto.h"

extern kern_return_t mach_vm_allocate(vm_map_t, mach_vm_address_t *, mach_vm_size_t, int);
extern kern_return_t mach_vm_deallocate(vm_map_t, mach_vm_address_t, mach_vm_size_t);
extern kern_return_t mach_vm_remap(vm_map_t, mach_vm_address_t *, mach_vm_size_t, mach_vm_offset_t, int, vm_map_t, mach_vm_address_t, boolean_t, vm_prot_t *, vm_prot_t *, vm_inherit_t);
#ifndef VM_FLAGS_OVERWRITE
#define VM_FLAGS_OVERWRITE 0x4000
#endif

@interface NSExtension : NSObject   // private: what MeloNX uses to start an extension process on demand
@property (nonatomic, strong) NSArray *preferredLanguages;
+ (instancetype)extensionWithIdentifier:(NSString *)identifier error:(NSError **)error;
- (void)beginExtensionRequestWithInputItems:(NSArray *)items completion:(void (^)(NSUUID *))callback;
- (int)pidForRequestIdentifier:(NSUUID *)identifier;
- (void)_kill:(int)signal;
@end
@interface NSXPCListenerEndpoint (DmPrivate)
@property (nonatomic, setter=_setEndpoint:) xpc_object_t _endpoint;
@end

enum { DONOR_MAX = 6 };
static const uint64_t HEADROOM = 256ull << 20;   // left to each donor below its limit
static xpc_connection_t g_listener;
static xpc_connection_t g_conn[DONOR_MAX]; static int g_nconn;
static NSMutableArray *g_ext, *g_shmems;      // the extensions (to kill them) and the xpc_shmem objects (kept for the mappings' lifetime)
static dispatch_semaphore_t g_connected;
static mach_vm_address_t g_base; static uint64_t g_size, g_taken;   // the prepared mapping and how much of it dm_ram_donor_take has moved

static NSString *donor_id(void) {
    for (NSURL *u in [NSFileManager.defaultManager contentsOfDirectoryAtURL:NSBundle.mainBundle.builtInPlugInsURL includingPropertiesForKeys:nil options:0 error:nil]) {
        NSBundle *b = [NSBundle bundleWithURL:u];
        if ([b.infoDictionary[@"NSExtension"][@"NSExtensionPrincipalClass"] isEqualToString:@"DmRamDonorHandler"]) return b.bundleIdentifier;
    }
    return nil;
}

static xpc_object_t ask(xpc_connection_t c, const char *op, uint64_t size, double timeout) {
    xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(m, DM_DONOR_OP, op); if (size) xpc_dictionary_set_uint64(m, DM_DONOR_SIZE, size);
    __block xpc_object_t r = nil; dispatch_semaphore_t done = dispatch_semaphore_create(0);
    xpc_connection_send_message_with_reply(c, m, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^(xpc_object_t a) {
        if (xpc_get_type(a) == XPC_TYPE_DICTIONARY) r = a; dispatch_semaphore_signal(done); });
    return dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC))) ? nil : r;
}

static int start_donor(NSString *ident) {   // 1 when a new donor connected
    NSError *err = nil; NSExtension *ext = [NSExtension extensionWithIdentifier:ident error:&err];
    if (!ext) { fprintf(stderr, "[RED] ram donor: cannot load the extension: %s\n", err.localizedDescription.UTF8String); return 0; }
    ext.preferredLanguages = @[];
    NSXPCListenerEndpoint *ep = [NSXPCListenerEndpoint new]; ep._endpoint = xpc_endpoint_create(g_listener);
    NSExtensionItem *item = [NSExtensionItem new]; item.userInfo = @{ @"endpoint": ep };
    int before; @synchronized (g_ext) { before = g_nconn; }
    __block NSUUID *req = nil; dispatch_semaphore_t spawned = dispatch_semaphore_create(0);
    [ext beginExtensionRequestWithInputItems:@[item] completion:^(NSUUID *u) { req = u; dispatch_semaphore_signal(spawned); }];
    dispatch_semaphore_wait(spawned, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
    if (!req) { fprintf(stderr, "[RED] ram donor: the extension request was refused\n"); return 0; }
    for (int i = 0; i < 100; i++) {
        @synchronized (g_ext) { if (g_nconn > before) { [g_ext addObject:ext]; return 1; } }
        dispatch_semaphore_wait(g_connected, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC));
    }
    fprintf(stderr, "[RED] ram donor: pid %d started but never connected\n", [ext pidForRequestIdentifier:req]);
    return 0;
}

static int map_chunk(xpc_connection_t c, uint64_t len, mach_vm_address_t at) {   // one chunk of a donor's memory at at
    xpc_object_t r = ask(c, DM_DONOR_OP_CREATE, len, 30);
    xpc_object_t sh = r ? xpc_dictionary_get_value(r, DM_DONOR_SHMEM) : nil;
    if (!sh || xpc_get_type(sh) != XPC_TYPE_SHMEM) return 0;
    void *tmp = NULL; size_t n = xpc_shmem_map(sh, &tmp);
    if (n < len || !tmp) return 0;
    vm_prot_t cur, max; mach_vm_address_t dst = at;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &dst, len, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), (mach_vm_address_t)tmp, false, &cur, &max, VM_INHERIT_SHARE);
    mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)tmp, n);
    if (kr != KERN_SUCCESS) return 0;
    [g_shmems addObject:sh];
    return 1;
}

uint64_t dm_ram_donor_prepare(uint64_t want) {
    const char *e = getenv("DM_RAM_DONOR");
    if (e && *e) want = strtoull(e, NULL, 0) << 20;
    if (!want) return 0;
    NSString *ident = donor_id();
    if (!ident) { fprintf(stderr, "ram donor: no donor extension in this build\n"); return 0; }
    g_ext = [NSMutableArray array]; g_shmems = [NSMutableArray array]; g_connected = dispatch_semaphore_create(0);
    g_listener = xpc_connection_create(NULL, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    xpc_connection_set_event_handler(g_listener, ^(xpc_object_t ev) {
        if (xpc_get_type(ev) != XPC_TYPE_CONNECTION) return;
        xpc_connection_t c = (xpc_connection_t)ev;
        xpc_connection_set_event_handler(c, ^(xpc_object_t x) { (void)x; });
        xpc_connection_resume(c);
        @synchronized (g_ext) { if (g_nconn < DONOR_MAX) g_conn[g_nconn++] = c; }
        dispatch_semaphore_signal(g_connected);
    });
    xpc_connection_resume(g_listener);

    want = (want + vm_page_size - 1) & ~(uint64_t)(vm_page_size - 1);
    if (mach_vm_allocate(mach_task_self(), &g_base, want, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) { fprintf(stderr, "[RED] ram donor: no address space for %llu MB\n", want >> 20); return 0; }
    uint64_t off = 0;
    for (int d = 0; d < DONOR_MAX && off < want; d++) {
        if (!start_donor(ident)) break;
        xpc_connection_t c; @synchronized (g_ext) { c = g_conn[g_nconn - 1]; }
        xpc_object_t r = ask(c, DM_DONOR_OP_AVAILABLE, 0, 2);
        uint64_t avail = r ? xpc_dictionary_get_uint64(r, DM_DONOR_AVAILABLE) : 0, budget = avail > HEADROOM ? avail - HEADROOM : 0;
        budget &= ~(uint64_t)(vm_page_size - 1);
        fprintf(stderr, "ram donor %d: %llu MB available, taking up to %llu MB\n", d, avail >> 20, budget >> 20);
        while (budget && off < want) {
            uint64_t len = MIN(MIN((uint64_t)DM_DONOR_CHUNK, want - off), budget);
            if (!map_chunk(c, len, g_base + off)) { fprintf(stderr, "[RED] ram donor %d: a %llu MB chunk failed\n", d, len >> 20); budget = 0; break; }
            off += len; budget -= len;
        }
    }
    if (off < want) mach_vm_deallocate(mach_task_self(), g_base + off, want - off);
    g_size = off;
    fprintf(stderr, "ram donor: %llu MB of %llu MB from %d donor(s) (the app's own limit: %zu MB)\n", off >> 20, want >> 20, g_nconn, os_proc_available_memory() >> 20);
    if (off && !getenv("DM_RAM_DONOR_KEEP")) { for (NSExtension *x in g_ext) [x _kill:SIGKILL]; [g_ext removeAllObjects]; }
    return off;
}

size_t dm_ram_donor_take(void *target, size_t n) {
    if (g_taken >= g_size) return 0;
    n = (size_t)MIN((uint64_t)n, g_size - g_taken) & ~(size_t)(vm_page_size - 1);
    vm_prot_t cur, max; mach_vm_address_t dst = (mach_vm_address_t)target;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &dst, n, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), g_base + g_taken, false, &cur, &max, VM_INHERIT_SHARE);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "[RED] ram donor: remap onto %p failed: %s\n", target, mach_error_string(kr)); return 0; }
    mach_vm_deallocate(mach_task_self(), g_base + g_taken, n);
    g_taken += n;
    return n;
}
