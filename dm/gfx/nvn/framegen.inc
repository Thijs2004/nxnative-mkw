// ---------- Frame generation (DM_FRAMEGEN) ----------
// Included by nvn_mtl.m after present_encode_m4 (the display-link present uses it). A game that presents every second display tick (a 30 fps game on the 60 Hz link) gets one picture
// between each two of its frames: Apple's low-latency frame interpolation (VTFrameProcessor, iOS / macOS 26) on a smaller copy of the two last pictures, shown first, then the real frame.
// The game's picture is copied into a host texture of the drawable's size (C, so the game's swapchain texture is free at once), converted to 4:2:0 video range (the processor's format) at
// DM_FRAMEGEN_W x (aspect) on the classic queue behind an event the Metal 4 queue raises after the copy, interpolated, converted back and scaled to the drawable's size (M). The display-link
// callback shows M one tick, C the tick after; it waits on the CPU for M's completion (never on the Metal 4 queue: that would stall the game's own work behind the interpolation).
// Frames come as lists [M, C]; M is left out while the list is long (a game running faster than half the link rate) or for the first frame.
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#define FG_RING 4
typedef struct { int slot, mid; uint64_t ticket; double period; uint64_t frame; } fg_item;   // slot: the ring entry holding the picture (m / c), its semaphore in g_fg.sem
static struct {
    int on, ready, failed, n_made;
    NSUInteger dw, dh, fw, fh; MTLPixelFormat pf;
    id<MTLCommandQueue> q; id<MTLSharedEvent> ev; uint64_t v;
    id<MTLComputePipelineState> to_yuv, to_rgb;
    VTFrameProcessor *vp; VTLowLatencyFrameInterpolationConfiguration *cfg;
    CVPixelBufferRef yuv[3], mid; id<MTLTexture> yuv_y[3], yuv_c[3], mid_y, mid_c;
    id<MTLTexture> c[FG_RING], craw[FG_RING], m[FG_RING], mraw[FG_RING]; id<MTLBuffer> uvb[FG_RING];
    dispatch_semaphore_t sem[FG_RING];
    uint64_t n; int have_prev;   // frames kicked; whether yuv[(n-1)%3] holds the previous game frame
    fg_item list[8]; int nlist;  // pictures still to show, in order (display-link thread only)
    uint64_t made, midn, skipped, late;
    double gpu_sum; uint64_t gpu_n;
} g_fg;
static const char *k_fg_msl =
"#include <metal_stdlib>\nusing namespace metal;\n"
"constexpr sampler fs(filter::linear, address::clamp_to_edge);\n"
"kernel void fg_to_yuv(texture2d<float> src [[texture(0)]], texture2d<float, access::write> yp [[texture(1)]], texture2d<float, access::write> cp [[texture(2)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= cp.get_width() || g.y >= cp.get_height()) return;\n"
"  float2 inv = 1.0 / float2(yp.get_width(), yp.get_height()); float3 sum = 0; uint2 o = g * 2;\n"
"  for (uint j = 0; j < 2; j++) for (uint i = 0; i < 2; i++) { uint2 p = o + uint2(i, j); float3 rgb = src.sample(fs, (float2(p) + 0.5) * inv).rgb;\n"
"    yp.write(float4((16.0 + 219.0 * dot(rgb, float3(0.2126, 0.7152, 0.0722))) / 255.0), p); sum += rgb; }\n"
"  float3 a = sum * 0.25; float y = dot(a, float3(0.2126, 0.7152, 0.0722));\n"
"  cp.write(float4((128.0 + 224.0 * (a.b - y) / 1.8556) / 255.0, (128.0 + 224.0 * (a.r - y) / 1.5748) / 255.0, 0, 0), g);\n"
"}\n"
"kernel void fg_to_rgb(texture2d<float> yt [[texture(0)]], texture2d<float> ct [[texture(1)]], texture2d<float, access::write> dst [[texture(2)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= dst.get_width() || g.y >= dst.get_height()) return;\n"
"  float2 uv = (float2(g) + 0.5) / float2(dst.get_width(), dst.get_height());\n"
"  float y = (yt.sample(fs, uv).r * 255.0 - 16.0) / 219.0; float2 c = ct.sample(fs, uv).rg; float cb = (c.x * 255.0 - 128.0) / 224.0, cr = (c.y * 255.0 - 128.0) / 224.0;\n"
"  dst.write(float4(saturate(float3(y + 1.5748 * cr, y - 0.1873 * cb - 0.4681 * cr, y + 1.8556 * cb)), 1.0), g);\n"
"}\n";
static int g_fg_force;   // DM_FRAMEGEN=2 (test): interpolate whatever the game's frame rate
static int fg_enabled(void) {
    static int v = -1; if (v >= 0) return v;
    const char *e = getenv("DM_FRAMEGEN"); v = e && atoi(e) > 0; g_fg_force = e && atoi(e) == 2;
#if TARGET_OS_OSX
    if (v && !g_fg_force) { MLOG("frame generation (DM_FRAMEGEN): not used on the Mac (the game runs well there); DM_FRAMEGEN=2 forces it for tests"); v = 0; }   // the Mac has no switch for it
#endif
    if (v) {
        if (@available(iOS 26.0, macOS 26.0, *)) { if (!VTLowLatencyFrameInterpolationConfiguration.supported) { MLOG("frame generation (DM_FRAMEGEN): this device does not support the low-latency frame interpolation; off"); v = 0; } }
        else { MLOG("frame generation (DM_FRAMEGEN): needs iOS / macOS 26; off"); v = 0; }
    }
    if (v) MLOG("frame generation (DM_FRAMEGEN=%s): a picture between each two frames of a 30 fps game, interpolated at %s pixels wide", e, getenv("DM_FRAMEGEN_W") ? getenv("DM_FRAMEGEN_W") : "1280");
    return v;
}
static CVPixelBufferRef fg_buf(NSUInteger w, NSUInteger h) {
    NSDictionary *a = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES};
    CVPixelBufferRef b = NULL; CVPixelBufferCreate(NULL, w, h, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)a, &b); return b;
}
static int fg_planes(CVPixelBufferRef b, id<MTLTexture> __strong *y, id<MTLTexture> __strong *c) {
    IOSurfaceRef s = CVPixelBufferGetIOSurface(b); if (!s) return 0;
    NSUInteger w = CVPixelBufferGetWidth(b), h = CVPixelBufferGetHeight(b);
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; *y = [g_dev newTextureWithDescriptor:d iosurface:s plane:0];
    d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:w / 2 height:h / 2 mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; *c = [g_dev newTextureWithDescriptor:d iosurface:s plane:1];
    return *y && *c;
}
// Everything that depends on the picture's size; made again when the drawable's size or format changes. Returns 0 (and turns frame generation off for good) when something cannot be made
static int fg_setup(NSUInteger dw, NSUInteger dh, MTLPixelFormat pf) {
    if (g_fg.ready && g_fg.dw == dw && g_fg.dh == dh && g_fg.pf == pf) return 1;
    if (g_fg.failed) return 0;
    uint64_t t0 = now_ns();
    if (g_fg.ready) { [g_fg.vp endSession]; for (int i = 0; i < FG_RING; i++) { res_remove(g_fg.c[i]); res_remove(g_fg.m[i]); } for (int i = 0; i < 3; i++) if (g_fg.yuv[i]) CFRelease(g_fg.yuv[i]); if (g_fg.mid) CFRelease(g_fg.mid); g_fg.ready = 0; g_fg.nlist = 0; g_fg.have_prev = 0; }
    if (@available(iOS 26.0, macOS 26.0, *)) {
        const char *e = getenv("DM_FRAMEGEN_W"); NSUInteger fw = e ? (NSUInteger)atoi(e) : 1280; if (fw < 320 || fw > 3840) fw = 1280;
        if (fw > dw) fw = dw; fw &= ~1ul; NSUInteger fh = ((NSUInteger)llround((double)fw * dh / dw)) & ~1ul;
        if (!g_fg.q) { g_fg.q = [g_dev newCommandQueue]; g_fg.ev = [g_dev newSharedEvent]; g_fg.v = 0; g_fg.q.label = @"frame generation"; }
        if (!g_fg.to_yuv) {
            NSError *er = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_fg_msl) options:nil error:&er];
            g_fg.to_yuv = lib ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fg_to_yuv"] error:&er] : nil;
            g_fg.to_rgb = lib ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fg_to_rgb"] error:&er] : nil;
            if (!g_fg.to_yuv || !g_fg.to_rgb) { MLOG("[RED] frame generation: conversion shaders: %s", er.description.UTF8String); g_fg.failed = 1; return 0; }
        }
        g_fg.cfg = [[VTLowLatencyFrameInterpolationConfiguration alloc] initWithFrameWidth:fw frameHeight:fh numberOfInterpolatedFrames:1];
        g_fg.vp = [VTFrameProcessor new]; NSError *er = nil;
        if (!g_fg.cfg || ![g_fg.vp startSessionWithConfiguration:g_fg.cfg error:&er]) { MLOG("[RED] frame generation: cannot start the frame processor at %lux%lu: %s", (unsigned long)fw, (unsigned long)fh, er.description.UTF8String); g_fg.failed = 1; return 0; }
        int ok = 1;
        for (int i = 0; i < 3; i++) { g_fg.yuv[i] = fg_buf(fw, fh); ok &= g_fg.yuv[i] && fg_planes(g_fg.yuv[i], &g_fg.yuv_y[i], &g_fg.yuv_c[i]); }
        g_fg.mid = fg_buf(fw, fh); ok &= g_fg.mid && fg_planes(g_fg.mid, &g_fg.mid_y, &g_fg.mid_c);
        MTLPixelFormat raw = pf == MTLPixelFormatBGRA8Unorm_sRGB ? MTLPixelFormatBGRA8Unorm : pf == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm : pf;
        for (int i = 0; ok && i < FG_RING; i++) {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:dw height:dh mipmapped:NO];
            d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView;
            g_fg.c[i] = [g_dev newTextureWithDescriptor:d]; g_fg.m[i] = [g_dev newTextureWithDescriptor:d];
            g_fg.craw[i] = raw == pf ? g_fg.c[i] : [g_fg.c[i] newTextureViewWithPixelFormat:raw]; g_fg.mraw[i] = raw == pf ? g_fg.m[i] : [g_fg.m[i] newTextureViewWithPixelFormat:raw];
            g_fg.uvb[i] = [g_dev newBufferWithLength:sizeof(float) * 8 options:MTLResourceStorageModeShared];
            ok &= g_fg.c[i] && g_fg.m[i] && g_fg.craw[i] && g_fg.mraw[i] && g_fg.uvb[i];
            if (ok) { g_fg.c[i].label = @"frame generation game picture"; g_fg.m[i].label = @"frame generation interpolated picture"; res_add(g_fg.c[i]); res_add(g_fg.m[i]); res_add(g_fg.uvb[i]); }
        }
        if (!ok) { MLOG("[RED] frame generation: cannot make its textures at %lux%lu / %lux%lu", (unsigned long)dw, (unsigned long)dh, (unsigned long)fw, (unsigned long)fh); g_fg.failed = 1; return 0; }
        res_commit();
        g_fg.dw = dw; g_fg.dh = dh; g_fg.fw = fw; g_fg.fh = fh; g_fg.pf = pf; g_fg.ready = 1; g_fg.n_made++;
        MLOG("frame generation: pictures %lux%lu, interpolated at %lux%lu (set up in %.1f ms)", (unsigned long)dw, (unsigned long)dh, (unsigned long)fw, (unsigned long)fh, (now_ns() - t0) / 1e6);
        return 1;
    }
    return 0;
}
static void fg_disp(id<MTLComputeCommandEncoder> ce, id<MTLComputePipelineState> ps, NSUInteger w, NSUInteger h) {
    NSUInteger tw = ps.threadExecutionWidth, th = ps.maxTotalThreadsPerThreadgroup / tw; if (th > 8) th = 8;
    [ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
}
// The game's picture src into slot k's texture C (the way the present draws it, letterbox included) on the Metal 4 queue, then the conversion / interpolation on the classic queue;
// returns the item for M (done = signalled when M has been written), or NULL semaphore when no interpolated picture was made. done_cb: C is complete (the game's texture is free)
static int fg_kick(id<MTLTexture> src, const int crop[4], int want_mid, void (^c_done)(void), dispatch_semaphore_t *mid_done) {
    *mid_done = NULL;
    int k = (int)(g_fg.n % FG_RING); id<MTLTexture> c = g_fg.c[k];
    id<MTL4CommandAllocator> al = [g_dev newCommandAllocator]; id<MTL4CommandBuffer> cb = [g_dev newCommandBuffer]; id<MTLTexture> raw = present_raw_src(src);
    if (!al || !cb) { MLOG("[RED] frame generation: no command buffer"); return 0; }
    [cb beginCommandBufferWithAllocator:al];
    int kind = present_encode_m4(cb, raw, c, crop, g_fg.uvb[k], NULL);
    [cb endCommandBuffer];
    if (!kind) return 0;
    MTL4CommitOptions *o = [MTL4CommitOptions new];
    [o addFeedbackHandler:^(id<MTL4CommitFeedback> f) { (void)al; (void)cb; (void)raw; (void)src; if (f.error) MLOG("[RED] frame generation copy: %s", f.error.description.UTF8String); c_done(); }];
    [g_q4 commit:&cb count:1 options:o];
    uint64_t v1 = ++g_fg.v; [g_q4 signalEvent:g_fg.ev value:v1];
    int cur = (int)(g_fg.n % 3), prv = (int)((g_fg.n + 2) % 3), mid = want_mid && g_fg.have_prev;
    id<MTLCommandBuffer> fcb = [g_fg.q commandBuffer]; fcb.label = @"frame generation";
    [fcb encodeWaitForEvent:g_fg.ev value:v1];
    id<MTLComputeCommandEncoder> ce = [fcb computeCommandEncoder];
    [ce setComputePipelineState:g_fg.to_yuv]; [ce setTexture:g_fg.craw[k] atIndex:0]; [ce setTexture:g_fg.yuv_y[cur] atIndex:1]; [ce setTexture:g_fg.yuv_c[cur] atIndex:2];
    fg_disp(ce, g_fg.to_yuv, g_fg.fw / 2, g_fg.fh / 2); [ce endEncoding];
    dispatch_semaphore_t sem = NULL;
    if (mid) {
        if (@available(iOS 26.0, macOS 26.0, *)) {
            int64_t ts = (int64_t)g_fg.n * 2;
            VTFrameProcessorFrame *sf = [[VTFrameProcessorFrame alloc] initWithBuffer:g_fg.yuv[cur] presentationTimeStamp:CMTimeMake(ts + 2, 60)];
            VTFrameProcessorFrame *pf = [[VTFrameProcessorFrame alloc] initWithBuffer:g_fg.yuv[prv] presentationTimeStamp:CMTimeMake(ts, 60)];
            VTFrameProcessorFrame *df = [[VTFrameProcessorFrame alloc] initWithBuffer:g_fg.mid presentationTimeStamp:CMTimeMake(ts + 1, 60)];
            VTLowLatencyFrameInterpolationParameters *p = [[VTLowLatencyFrameInterpolationParameters alloc] initWithSourceFrame:sf previousFrame:pf interpolationPhase:@[@0.5] destinationFrames:@[df]];
            if (p) {
                [g_fg.vp processWithCommandBuffer:fcb parameters:p];
                ce = [fcb computeCommandEncoder];
                [ce setComputePipelineState:g_fg.to_rgb]; [ce setTexture:g_fg.mid_y atIndex:0]; [ce setTexture:g_fg.mid_c atIndex:1]; [ce setTexture:g_fg.mraw[k] atIndex:2];
                fg_disp(ce, g_fg.to_rgb, g_fg.dw, g_fg.dh); [ce endEncoding];
                sem = dispatch_semaphore_create(0);
            }
        }
    }
    dispatch_semaphore_t s = sem; uint64_t t0 = now_ns();
    [fcb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        if (b.error) MLOG("[RED] frame generation: %s", b.error.description.UTF8String);
        if (s) { double ms = (now_ns() - t0) / 1e6; (void)ms; dispatch_semaphore_signal(s); } }];
    [fcb commit];
    g_fg.have_prev = 1; g_fg.n++; g_fg.made++; *mid_done = sem;
    return 1;
}
// Display-link callback side. Shows the next picture of the list on this tick's drawable (an interpolated one that is not finished within a few ms is left out); returns whether it presented
static int fg_show(id<CAMetalDrawable> dr, double target_ts) {
    while (g_fg.nlist) {
        fg_item it = g_fg.list[0]; for (int i = 0; i + 1 < g_fg.nlist; i++) g_fg.list[i] = g_fg.list[i + 1]; g_fg.nlist--;
        if (it.mid && dispatch_semaphore_wait(g_fg.sem[it.slot], dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_MSEC))) { g_fg.late++; continue; }
        if (it.mid) g_fg.midn++;
        tex_t t; memset(&t, 0, sizeof t); t.magic = MAGIC('T', 'E', 'X', 0);
        int none[4] = {0, 0, 0, 0};
        g_fg_override = it.mid ? g_fg.m[it.slot] : g_fg.c[it.slot]; int ok;
        @autoreleasepool { ok = present_now(&t, it.period, it.frame, it.ticket, none, dr, target_ts); }
        g_fg_override = nil;
        return ok;
    }
    return 0;
}
// A game frame taken on this tick: copied (the game's texture is free after it), converted, interpolated with the previous one; the pictures wait in the list. Returns 0 when frame
// generation cannot take it (nothing was done: the caller presents it the usual way)
static int fg_take(id<MTLTexture> dtex, id<MTLTexture> src, const int crop[4], uint64_t ticket, double period, uint64_t frame) {
    if (!dtex || !src || !fg_setup(dtex.width, dtex.height, dtex.pixelFormat)) return 0;
    if (g_fg.nlist > 5) return 0;
    int k = (int)(g_fg.n % FG_RING), want_mid = (period > 0.025 || g_fg_force) && g_fg.nlist <= 1;
    dispatch_semaphore_t sem = NULL;
    int ok = fg_kick(src, crop, want_mid, ^{ pthread_mutex_lock(&g_pq_mu); if (ticket > g_pq_done) g_pq_done = ticket; pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu); }, &sem);
    if (!ok) return 0;
    if (sem) { g_fg.sem[k] = sem; g_fg.list[g_fg.nlist++] = (fg_item){k, 1, ticket, period, frame}; }
    else g_fg.skipped++;
    g_fg.list[g_fg.nlist++] = (fg_item){k, 0, ticket, period, frame};
    static uint64_t last;
    if (g_fg.made - last >= 300) { last = g_fg.made; MLOG("frame generation up to frame %llu: %llu frames, %llu interpolated pictures shown, %llu without one, %llu left out (not finished in time)",
        (unsigned long long)frame, (unsigned long long)g_fg.made, (unsigned long long)g_fg.midn, (unsigned long long)g_fg.skipped, (unsigned long long)g_fg.late); }
    return 1;
}
