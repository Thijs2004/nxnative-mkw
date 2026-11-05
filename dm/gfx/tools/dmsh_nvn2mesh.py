# NVN2 mesh shaders (Mario Kart World): the vertex shader of the host's mesh draw emulation. The task and mesh stages run as compute dispatches that write their output into host
# buffers (the translator's InstEmitAmpereMesh.cs has the layout; nvn_mtl.m mesh_draw dispatches and draws); this generates, per mesh shader, a Maxwell vertex shader that draws those
# windows: shader_split.py packs it as a vertex shader of key mesh key ^ 0x5a5a5a5a5a5a5a5a, with an NVN1-shaped control section, and dmsh_build.py translates it like any other.
# Vertex ID → mesh group g (a power-of-two run of IDs per group, 3 per primitive), primitive p, corner c; the corner's local vertex from the window; each of the mesh shader's outputs
# from the vertex's tile. A primitive past the group's count, or a group past its task's count, has all its corners at the origin (dropped as degenerate).
import struct
import dmsh_maxwell_enc as M

# InstEmitAmpereMesh.cs
TASK_STRIDE, MESH_STRIDE, MESH_PRIMS, MESH_PER_TASK, SLOT_TASK, SLOT_MESH = 0x4000, 0x4800, 0x3400, 32, 14, 15
C0_SSBO_VS = 0x110   # the vertex stage's storage buffer table in c[0]
RZ = 255
ATTR_VERTEX_ID = 0x2fc
KEY_XOR = 0x5a5a5a5a5a5a5a5a
CTL_MAGIC, GFX_MAGIC = bytes.fromhex('34127698'), bytes.fromhex('78563412')


def vs_key(mesh_key): return '%016x' % (int(mesh_key, 16) ^ KEY_XOR)


def is_mesh(ctl): return len(ctl) > 0x62 and ctl[4] == 0xc9 and ctl[0x56] == 7


def mesh_info(ctl, code):
    maxv, maxp = struct.unpack_from('<HH', ctl, 0x60)
    bits = int.from_bytes(code[0x35:0x45], 'little')   # output map: bit n from SPH byte 0x35 = attribute 0x60 + 4 n (0x70..0x7c = the position)
    addrs = [0x60 + 4 * n for n in range(128) if bits >> n & 1]
    npc = bin(int.from_bytes(ctl[0x68:0x6c], 'little')).count('1')   # per-primitive components, stored after the vertex tiles
    return dict(maxv=maxv, maxp=maxp, addrs=addrs, nvc=len(addrs), npc=npc, topo=ctl[0x5f])


def fits(info):
    tiles = (info['maxv'] + 31) // 32 * info['nvc'] * 0x80 + (info['maxp'] + 31) // 32 * info['npc'] * 0x80
    return info['topo'] == 3 and tiles <= MESH_PRIMS and MESH_PRIMS + 4 * (4 + 3 * info['maxp']) <= MESH_STRIDE


def prim_slot(maxp):   # the vertex IDs of one mesh group: 3 per primitive, rounded up to a power of two (nvn_mtl.m mesh_draw draws groups × this)
    n = 1
    while n < maxp * 3: n <<= 1
    return n


def gen_vs(info, mesh_code, pp_addrs=()):
    out = []
    def e(form, **kw): out.append(M.enc(form, **kw))
    pslot = prim_slot(info['maxp']); sh = pslot.bit_length() - 1
    VID, G, Rr, P, C, CNT, LV, T, B0, A0, V, TC, L = 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14   # B0 / A0 pairs
    e('Ald', Dest=VID, SrcA=RZ, SrcB=RZ, Imm11=ATTR_VERTEX_ID, AlSize=0)
    e('ShrI', Dest=G, SrcA=VID, Imm20=sh)
    e('Lop32i', Dest=Rr, SrcA=VID, Imm32=pslot - 1, LogicOp=0)
    e('Imul32i', Dest=P, SrcA=Rr, Imm32=0xAAAB); e('ShrI', Dest=P, SrcA=P, Imm20=17)   # p = r / 3 (r < 98304)
    e('ImadI', Dest=C, SrcA=P, Imm20=(-3) & 0xFFFFF, SrcC=Rr, ASigned=1, BSigned=1)    # c = r - 3 p
    def ld(dst, slot, base, idx_reg, off):   # dst = storage[slot][base + idx + off], shaped for the translator's storage buffer tracking (an add with the c[0] table entry, plus a constant)
        t = base
        if idx_reg != RZ: e('Iadd3R', Dest=B0 + 1, SrcA=base, SrcB=idx_reg, SrcC=RZ); t = B0 + 1
        sb = C0_SSBO_VS + 16 * slot
        e('Iadd3C', Dest=A0, SrcA=t, CbufSlot=0, CbufOffset=sb >> 2, SrcC=RZ, WriteCC=1); e('Iadd3C', Dest=A0 + 1, SrcA=RZ, CbufSlot=0, CbufOffset=(sb + 4) >> 2, SrcC=RZ, X=1)
        e('Ldg', Dest=dst, SrcA=A0, Imm24=off & 0xFFFFFF, LsSize=4, E=1)
    # the group's task: its count of mesh groups
    e('ShrI', Dest=T, SrcA=G, Imm20=5); e('Imul32i', Dest=T, SrcA=T, Imm32=TASK_STRIDE)
    ld(TC, SLOT_TASK, T, RZ, 4)
    e('Lop32i', Dest=L, SrcA=G, Imm32=MESH_PER_TASK - 1, LogicOp=0)
    e('Imul32i', Dest=B0, SrcA=G, Imm32=MESH_STRIDE)   # the group's window
    ld(CNT, SLOT_MESH, B0, RZ, MESH_PRIMS)
    e('Imad32i', Dest=T, SrcA=P, Imm32=3, ASigned=0, BSigned=0); e('Iadd3R', Dest=T, SrcA=T, SrcB=C, SrcC=RZ)
    e('ShlI', Dest=T, SrcA=T, Imm20=2)
    ld(LV, SLOT_MESH, B0, T, MESH_PRIMS + 16)   # corner 3 p + c: the word for byte 4 + 3 p + c
    # vertex address: (lv >> 5) * nvc * 0x80 + (lv & 31) * 4
    e('ShrI', Dest=T, SrcA=LV, Imm20=5); e('Imul32i', Dest=T, SrcA=T, Imm32=info['nvc'] * 0x80)
    e('Lop32i', Dest=LV, SrcA=LV, Imm32=31, LogicOp=0); e('LeaR', Dest=T, SrcA=LV, SrcB=T, ImmU5=2)
    # P0 = p >= count || local group >= the task's count
    e('IsetpR', SrcA=P, SrcB=CNT, IComp=6, Signed=0, Bop=0, SrcPred=7, DestPred=0, DestPredInv=7)
    e('IsetpR', SrcA=L, SrcB=TC, IComp=6, Signed=0, Bop=1, SrcPred=0, DestPred=0, DestPredInv=7)
    # an unused corner (the slot's padding past 3 x the most primitives, a primitive past the group's count, a group past its task's count) leaves now, its outputs all
    # zero, so its triangle is degenerate: most vertex IDs of a draw are such (Mario Kart World's effect pass: 576 of 1024 used at most a group), and they skip every load
    e('Exit', Pred=0); out[-1] |= 15
    for k, addr in enumerate(info['addrs']):
        ld(V, SLOT_MESH, B0, T, k * 0x80)
        e('Mov32i', Dest=V, Imm32=0, QuadMask=15, Pred=0)
        e('Ast', SrcB=V, SrcA=RZ, SrcC=RZ, Imm11=addr, AlSize=0)
    if pp_addrs:   # per-primitive components: after the vertex tiles, tiled by primitive the same way; every corner writes its primitive's value
        base_pp = (info['maxv'] + 31) // 32 * info['nvc'] * 0x80
        e('ShrI', Dest=T, SrcA=P, Imm20=5); e('Imul32i', Dest=T, SrcA=T, Imm32=info['npc'] * 0x80)
        e('Lop32i', Dest=LV, SrcA=P, Imm32=31, LogicOp=0); e('LeaR', Dest=T, SrcA=LV, SrcB=T, ImmU5=2)
        for k, addr in enumerate(pp_addrs[:info['npc']]):
            ld(V, SLOT_MESH, B0, T, base_pp + k * 0x80)
            e('Ast', SrcB=V, SrcA=RZ, SrcC=RZ, Imm11=addr, AlSize=0)
    e('Exit'); out[-1] |= 15
    while len(out) % 3: out.append(M.enc('Nop'))
    b = bytearray()
    for g in range(0, len(out), 3):
        b += struct.pack('<Q', 0x001f8000fc0007e0)   # scheduling word
        for w in out[g:g + 3]: b += struct.pack('<Q', w)
    b += struct.pack('<Q', M.enc('Exit') | 15) * 0   # (no tail)
    sph = bytearray(mesh_code[:0x50])   # NVN2's 0x80-byte SPH keeps version 3's layout in its first 0x50 bytes: the mesh shader's output map is the vertex shader's
    w0 = struct.unpack_from('<I', sph)[0]
    struct.pack_into('<I', sph, 0, (w0 & ~(0x1F << 5) & ~(0xF << 10)) | (3 << 5) | (1 << 10))   # SPH version 3, shader type vertex
    return bytes(sph) + bytes(b)


def vs_record(ctl, code):
    """(control section, code area) of the generated vertex shader, shaped as an NVN1 one (dmsh_build.py job_for: a 0x30-byte code header, the SPH, the instructions;
    control +0x700 / +0x704 both the code area's length: no c1). None if the mesh shader's output does not fit the window layout"""
    info = mesh_info(ctl, code)
    if not fits(info): return None
    body = gen_vs(info, code)
    area = GFX_MAGIC + bytes(0x2c) + body
    area += bytes(-len(area) % 0x100)
    c = bytearray(0x880); c[:4] = CTL_MAGIC; struct.pack_into('<I', c, 0x20, 0x880); struct.pack_into('<2I', c, 0x700, len(area), len(area))
    return bytes(c), area
