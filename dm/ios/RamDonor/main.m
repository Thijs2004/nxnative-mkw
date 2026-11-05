// Memory donor extension: a separate process with its own memory limit that allocates memory and hands it to the app as xpc_shmem (dm/ios/Sources/ram_donor.m)
@import Foundation;
#import <mach/mach.h>
#import <objc/runtime.h>
#import <os/proc.h>
#import <xpc/xpc.h>
#import "../Sources/ram_donor_proto.h"

extern kern_return_t mach_vm_allocate(vm_map_t, mach_vm_address_t *, mach_vm_size_t, int);
extern kern_return_t mach_vm_deallocate(vm_map_t, mach_vm_address_t, mach_vm_size_t);
#ifndef VM_FLAGS_PURGABLE
#define VM_FLAGS_PURGABLE 0x2
#endif
@interface NSXPCListenerEndpoint (DmPrivate)
@property (nonatomic, setter=_setEndpoint:) xpc_object_t _endpoint;
@end

static void handle(xpc_connection_t c, xpc_object_t m) {
    if (xpc_get_type(m) != XPC_TYPE_DICTIONARY) return;
    const char *op = xpc_dictionary_get_string(m, DM_DONOR_OP);
    xpc_object_t r = op ? xpc_dictionary_create_reply(m) : NULL;
    if (!r) return;
    if (!strcmp(op, DM_DONOR_OP_CREATE)) {
        uint64_t size = xpc_dictionary_get_uint64(m, DM_DONOR_SIZE); mach_vm_address_t a = 0;
        // purgeable memory, as MeloNX: it is created nonvolatile, so the system never empties it
        if (!size || mach_vm_allocate(mach_task_self(), &a, size, VM_FLAGS_ANYWHERE | VM_FLAGS_PURGABLE) != KERN_SUCCESS) xpc_dictionary_set_string(r, DM_DONOR_ERROR, "mach_vm_allocate failed");
        else {
            xpc_object_t sh = xpc_shmem_create((void *)a, (size_t)size);
            if (sh) xpc_dictionary_set_value(r, DM_DONOR_SHMEM, sh);
            else { mach_vm_deallocate(mach_task_self(), a, size); xpc_dictionary_set_string(r, DM_DONOR_ERROR, "xpc_shmem_create failed"); }
        }
    } else if (!strcmp(op, DM_DONOR_OP_AVAILABLE)) xpc_dictionary_set_uint64(r, DM_DONOR_AVAILABLE, (uint64_t)os_proc_available_memory());
    else xpc_dictionary_set_string(r, DM_DONOR_ERROR, "unknown operation");
    xpc_connection_send_message(c, r);
}

static void allow_any(void) {}
__attribute__((constructor)) static void allow_endpoint_decoding(void) {   // the request's userInfo carries an NSXPCListenerEndpoint, which the decoder's class check refuses
    Method m = class_getInstanceMethod(NSClassFromString(@"NSXPCDecoder"), NSSelectorFromString(@"_validateAllowedClass:forKey:allowingInvocations:"));
    if (m) method_setImplementation(m, (IMP)allow_any);
}

@interface DmRamDonorHandler : NSObject <NSExtensionRequestHandling>
@end
@implementation DmRamDonorHandler { xpc_connection_t _c; }
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)ctx {
    id ep = ((NSExtensionItem *)ctx.inputItems.firstObject).userInfo[@"endpoint"];
    xpc_object_t e = [ep isKindOfClass:NSXPCListenerEndpoint.class] ? ((NSXPCListenerEndpoint *)ep)._endpoint : nil;
    if (!e || xpc_get_type(e) != XPC_TYPE_ENDPOINT) { NSLog(@"dm ram donor: no endpoint"); return; }
    _c = xpc_connection_create_from_endpoint((xpc_endpoint_t)e);
    xpc_connection_t c = _c;
    xpc_connection_set_event_handler(c, ^(xpc_object_t ev) { if (xpc_get_type(ev) == XPC_TYPE_ERROR) exit(0); handle(c, ev); });
    xpc_connection_resume(c);
    xpc_object_t hello = xpc_dictionary_create(NULL, NULL, 0); xpc_dictionary_set_string(hello, DM_DONOR_OP, "hello");
    xpc_connection_send_message(c, hello);   // the app counts a donor once its connection arrives
}
@end
