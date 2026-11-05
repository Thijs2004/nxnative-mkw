# Mario Kart World (0400C3F00006E000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a module's NSO build ID;
# FRAME_STEP as described there ("Registered patches"); this game's stub only scales its render size
from nso_asm import ADRP, B

# Render scale (state +0x10, float written by the loader: DM_RENDER_SCALE, 1.0 by default). main+0x2f7cd0 sizes each render target of a view from the view's pixel size (view +0x50,
# 1920.0 x 1080.0 in handheld) times the target's normalized rectangle; every 3D target (scene, half- and quarter-resolution effect layers, their depth) is sized there, and the viewports
# follow the targets. The stub replaces the function's first load of that size (ldp s10,s9,[x0,#0x50], right after the prologue) and multiplies both extents by the factor; x16 and s16
# are scratch at that point (s0..s7 hold the arguments). 1.0 or unset = the original size. Checked on the Mac 2026-11-02 at 2/3 (1280x720): races and menus full screen, the UI at 1080p
def frame_step(ctx):
    here, emit, fs_site, fs_put, FS_STATE_OFF = ctx.here, ctx.emit, ctx.site, ctx.put, ctx.state
    # Render scale through the game's own dynamic-resolution path. main+0x474970 sizes a view: when a graphics-settings byte is 1 and the view's bit is set in the scaler's mask (main+0x474a20..0x474a48)
    # it takes the scaler's scale (main+0x25b6bb0) and everything derived follows by the game's own design: its viewports and scissors, the ratio constants of its composite and upscale passes, the sizes
    # of fog, shadows and effects. Where the host sets a render scale (state +0x10, DM_RENDER_SCALE below 1), the two gate branches take the scaler path and the scaler stores that scale
    def cave(words, slots):
        start = here()
        for i, w in enumerate(words):
            if i == 0: w = ADRP(16, here(), FS_STATE_OFF)
            elif i in slots: w = B(here(), slots[i])
            emit(w)
        return start
    fs_site(0x474a24, '41010054'); fs_site(0x474a48, 'e1030054'); fs_site(0x25b6cbc, '021400bd')
    ca = cave([0xd503201f, 0xbd401210, 0x1e202208, 0x540000ad, 0x1e2e1011, 0x1e312200, 0x5400004a, 0xd503201f, 0x7100055f, 0x54000040, 0xd503201f, 0xd503201f], {7: 0x474a28, 10: 0x474a4c, 11: 0x474a28})
    cb = cave([0xd503201f, 0xbd401210, 0x1e202208, 0x540000ad, 0x1e2e1011, 0x1e312200, 0x5400004a, 0xd503201f, 0x7100057f, 0x7a400944, 0x54000040, 0xd503201f, 0xd503201f], {7: 0x474ac4, 11: 0x474ac4, 12: 0x474a4c})
    cc = cave([0xd503201f, 0xbd401210, 0x1e202208, 0x5400004d, 0x1e204202, 0xbd001402, 0xd503201f], {6: 0x25b6cc0})
    fs_put(0x474a24, [B(0x474a24, ca)], 'dynamic resolution: the scaler gate opens at a host render scale'); fs_put(0x474a48, [B(0x474a48, cb)], 'dynamic resolution: the view mask check is skipped at a host render scale')
    fs_put(0x25b6cbc, [B(0x25b6cbc, cc)], 'dynamic resolution: the scaler stores the host render scale'); ctx.render_scale = 1
    # Frame rate lock: the game picks its present interval (int at +0x214 of the display object) every frame from how late the last frame was (main+0x69cc64..0x69cce4: 1 or 2),
    # and its time step follows it, so a game that cannot keep 60 flips 1 <-> 2 every other frame. Where the state page's lock word (+0x14, DM_FPS_LOCK: the launcher's
    # Frame rate; not the host's frame step target at +0), the stub writes the interval itself after that decision: 2 at 30 or less, 1 above; 0 / unset keeps the game's own choice
    fs_site(0x69ccf4, '7fb20839')
    start2 = here()
    for i, w in enumerate((0xd503201f, 0xbd401610, 0x1e202208, 0x540000ed, 0x1e380208, 0x7100791f, 0x52800048, 0x52800030, 0x1a909108, 0xb9021668, 0x3908b27f, 0xd503201f)):
        emit(ADRP(16, here(), FS_STATE_OFF) if i == 0 else B(here(), 0x69ccf8) if i == 11 else w)
    fs_put(0x69ccf4, [B(0x69ccf4, start2)], 'present interval lock (DM_FPS_TARGET)'); ctx.fps_lock = 1

FRAME_STEP = {'d00e4f399e411d0613aa8d26b1c02fef': frame_step}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md)
BINDING = [
    {'what': 'render scale', 'now': 'build', 'goal': 'game', 'purpose': 'frame rate',
     'why': "the game's own view sizing function",
     'blocked': 'frame-step stubs have no function-matched form yet'},
]
