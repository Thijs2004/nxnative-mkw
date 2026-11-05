// Guest memory write watch (diagnostic, Mac): which guest code writes a value. DM_WATCH_AT=<seconds after start> scans the guest heap for the 8 bytes DM_WATCH_VALUE (hex; default two floats,
// 1920.0 and 1080.0: a render view's pixel size) and lists them. With DM_WATCH_PROTECT=1 the pages holding them are made read-only; the first write to each 8-byte target logs the writing
// instruction (pc and lr as module offsets), the page is opened for a moment and protected again, so later writers show up too (DM_WATCH_MAX unique pc/address pairs, default 400).
// Found while looking for where Mario Kart World decides its render size (render scale work).
#include "dm.h"
#include <pthread.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

int dm_guest_heap_readable(uint64_t a, size_t n);
#define WMAX_T 4096
#define WMAX_P 512
static uint64_t g_wt[WMAX_T]; static int g_nwt;               // target addresses (8-byte values)
static uintptr_t g_wp[WMAX_P]; static int g_nwp; static _Atomic uint64_t g_wp_open_ns[WMAX_P];
static _Atomic int g_wlog, g_wmax = 400, g_wready;
static struct { uint64_t pc, lr, addr; } g_seen[512]; static _Atomic int g_nseen;

static uint64_t now_ns_w(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static int fmt_addr(char *b, size_t n, uint64_t a) {
    module_t *m = module_of((void *)a);
    return m ? snprintf(b, n, "%s+%#llx", m->name, (unsigned long long)(a - (uint64_t)m->base)) : snprintf(b, n, "%#llx", (unsigned long long)a);
}
static int watch_fault(void *addr, int is_write, void *uc_) {
    if (!atomic_load(&g_wready) || !is_write) return 0;
    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)4095; int pi = -1;
    for (int i = 0; i < g_nwp; i++) if (g_wp[i] == page) { pi = i; break; }
    if (pi < 0) return 0;
    ucontext_t *uc = uc_; uint64_t pc = uc->uc_mcontext->__ss.__pc, lr = uc->uc_mcontext->__ss.__lr, a = (uint64_t)(uintptr_t)addr;
    int hit = 0; for (int i = 0; i < g_nwt; i++) if (a + 8 > g_wt[i] && a < g_wt[i] + 8) { hit = 1; break; }   // a store that overlaps a target (a 16-byte store covers two)
    if (hit && atomic_load(&g_wlog) < atomic_load(&g_wmax)) {
        int ns = atomic_load(&g_nseen), dup = 0; for (int i = 0; i < ns && i < 512; i++) if (g_seen[i].pc == pc && g_seen[i].addr == a) { dup = 1; break; }
        if (!dup && ns < 512) { g_seen[ns].pc = pc; g_seen[ns].lr = lr; g_seen[ns].addr = a; atomic_store(&g_nseen, ns + 1); atomic_fetch_add(&g_wlog, 1);
            char b[256], p[64], l[64]; fmt_addr(p, sizeof p, pc); fmt_addr(l, sizeof l, lr);
            int n = snprintf(b, sizeof b, "[dm] [watch] write to %#llx by pc=%s lr=%s\n", (unsigned long long)a, p, l); if (write(2, b, (size_t)n) < 0) {} } }
    mprotect((void *)page, 4096, PROT_READ | PROT_WRITE); atomic_store(&g_wp_open_ns[pi], now_ns_w());
    return 1;
}
static void *watch_main(void *arg) {
    (void)arg;
    const char *e = getenv("DM_WATCH_AT"); double at = e ? atof(e) : 0; if (at <= 0) return NULL;
    // DM_WATCH_VALUE=<hex>[,<hex>...]: up to four consecutive 8-byte words that must all match (the first one's address is the target); default one word, the two floats 1920.0 and 1080.0
    const char *vs = getenv("DM_WATCH_VALUE"); uint64_t vals[4] = {0x4487000044F00000ull}; int nv = 1;   // 1080.0f = 0x44870000, 1920.0f = 0x44F00000
    if (vs) { nv = 0; for (const char *q = vs; *q && nv < 4; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) vals[nv++] = strtoull(q, NULL, 16); }
    uint64_t val = vals[0];
    int protect = getenv("DM_WATCH_PROTECT") != NULL; const char *mx = getenv("DM_WATCH_MAX"); if (mx) atomic_store(&g_wmax, atoi(mx));
    struct timespec ts = {(time_t)at, (long)((at - (double)(time_t)at) * 1e9)}; nanosleep(&ts, NULL);
    uint64_t base; size_t len; dm_guest_heap_range(&base, &len);
    if (!base || !len) { fprintf(stderr, "[dm] [watch] no guest heap yet\n"); return NULL; }
    fprintf(stderr, "[dm] [watch] scanning the guest heap %#llx + %#zx for %#llx\n", (unsigned long long)base, len, (unsigned long long)val);
    for (uint64_t a = base; a + 8 <= base + len; a += 8) {
        if (!dm_guest_heap_readable(a, 8 * (size_t)nv)) continue;
        if (*(volatile uint64_t *)(uintptr_t)a != val) continue;
        int ok = 1; for (int q = 1; q < nv; q++) if (*(volatile uint64_t *)(uintptr_t)(a + 8 * (uint64_t)q) != vals[q]) { ok = 0; break; }
        if (ok && g_nwt < WMAX_T) g_wt[g_nwt++] = a;
    }
    fprintf(stderr, "[dm] [watch] %d targets\n", g_nwt);
    for (int i = 0; i < g_nwt && i < 64; i++) fprintf(stderr, "[dm] [watch]   %#llx (heap+%#llx)\n", (unsigned long long)g_wt[i], (unsigned long long)(g_wt[i] - base));
    {   // DM_WATCH_POKE=<hex word> [DM_WATCH_POKE_OFF=<byte offset, default 0>]: write that 8-byte word at target + offset in every match (an experiment: what does the game do when the value it reads is another one)
        const char *pk = getenv("DM_WATCH_POKE"), *po = getenv("DM_WATCH_POKE_OFF");
        if (pk) { uint64_t w = strtoull(pk, NULL, 16); uint64_t off = po ? strtoull(po, NULL, 0) : 0; for (int i = 0; i < g_nwt; i++) *(volatile uint64_t *)(uintptr_t)(g_wt[i] + off) = w;
            fprintf(stderr, "[dm] [watch] poked %#llx at +%llu of %d targets\n", (unsigned long long)w, (unsigned long long)off, g_nwt); } }
    if (!protect) return NULL;
    for (int i = 0; i < g_nwt; i++) { uintptr_t page = (uintptr_t)g_wt[i] & ~(uintptr_t)4095; int have = 0; for (int k = 0; k < g_nwp; k++) have |= g_wp[k] == page; if (!have && g_nwp < WMAX_P) g_wp[g_nwp++] = page; }
    dm_fault_hook_register(watch_fault);
    for (int i = 0; i < g_nwp; i++) mprotect((void *)g_wp[i], 4096, PROT_READ);
    atomic_store(&g_wready, 1); fprintf(stderr, "[dm] [watch] %d pages protected\n", g_nwp);
    for (;;) {   // protect the opened pages again a moment after their write
        usleep(300);
        uint64_t t = now_ns_w();
        for (int i = 0; i < g_nwp; i++) { uint64_t o = atomic_load(&g_wp_open_ns[i]); if (o && t > o + 200000) { atomic_store(&g_wp_open_ns[i], 0); mprotect((void *)g_wp[i], 4096, PROT_READ); } }
    }
    return NULL;
}
__attribute__((constructor)) static void watch_init(void) {
    if (!getenv("DM_WATCH_AT")) return;
    pthread_t th; if (!pthread_create(&th, NULL, watch_main, NULL)) pthread_detach(th);
}
