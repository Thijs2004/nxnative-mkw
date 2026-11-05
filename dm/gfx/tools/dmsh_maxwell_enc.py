# Maxwell (SM5x) instruction encoder generated from the Ryujinx decoder sources in nxnative (dm/third_party/ryujinx):
#   InstTable.cs   Add("<64-char pattern>", InstName.X, InstEmit.<Form>, ...)  -> fixed opcode bits per form
#   InstDecoders.cs struct Inst<Form> { T Field => ((_opcode >> S) & M) / (_opcode & BIT) != 0 }  -> field layout
# enc('FfmaR', Dest=1, SrcA=2, ...) returns the 64-bit word the translator decodes back into those fields.
import re, os

RYU = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../third_party/ryujinx/src/Ryujinx.Graphics.Shader/Decoders')

def _parse():
    forms = {}
    for m in re.finditer(r'Add\("([01x]{64})",\s*InstName\.(\w+),\s*InstEmit\.(\w+)', open(os.path.join(RYU, 'InstTable.cs')).read()):
        pat, name, form = m.groups()
        fixed = int(pat.replace('x', '0'), 2); mask = int(''.join('0' if c == 'x' else '1' for c in pat), 2)
        forms.setdefault(form, (fixed, mask, name))
    fields = {}
    src = open(os.path.join(RYU, 'InstDecoders.cs')).read()
    for m in re.finditer(r'struct Inst(\w+)\s*\{(.*?)\n    \}', src, re.S):
        sname, body = m.groups(); f = {}
        for p in re.finditer(r'public (\w+) (\w+) => (.*?);', body):
            ty, fname, expr = p.groups()
            parts = []
            # each "((_opcode >> S) & M)" or "(_opcode & M)" piece, optionally shifted left/right after extraction
            for q in re.finditer(r'\(_opcode >> (\d+)\) & 0x([0-9A-Fa-f]+)\)', expr):
                parts.append((int(q.group(1)), int(q.group(2), 16)))
            if not parts:
                q = re.search(r'\(_opcode & 0x([0-9A-Fa-f]+)\) != 0', expr)
                if q: m_ = int(q.group(1), 16); parts.append(('bit', m_.bit_length() - 1))
                q2 = re.search(r'\(int\)\(_opcode >> (\d+)\)$', expr)
                if q2: parts.append((int(q2.group(1)), 0xFFFFFFFF))
                q3 = re.search(r'\(int\)\(_opcode & 0x([0-9A-Fa-f]+)\)', expr)
                if q3 and not parts: parts.append((0, int(q3.group(1), 16)))
            if len(parts) == 1 and parts[0][0] != 'bit': f[fname] = ('f', parts[0][0], parts[0][1])
            elif len(parts) == 1: f[fname] = ('b', parts[0][1])
            elif fname == 'Imm20': f[fname] = ('imm20',)
            else: f[fname] = ('raw', expr)
        fields[sname] = f
    return forms, fields

FORMS, FIELDS = _parse()

def enc(form, **kv):
    fixed, mask, _ = FORMS[form]; w = fixed
    lay = FIELDS[form]
    if 'Pred' in lay and 'Pred' not in kv: kv['Pred'] = 7   # unguarded = @PT (field 0 would be @P0)
    for k, v in kv.items():
        if k not in lay: raise KeyError(f'{form}: no field {k} (has {sorted(lay)})')
        t = lay[k]
        v = int(v)
        if t[0] == 'f':
            s, m = t[1], t[2]
            if v & ~m and not (v < 0 and (v & m) == v & m): pass
            w |= (v & m) << s
        elif t[0] == 'b':
            if v: w |= 1 << t[1]
        elif t[0] == 'imm20':   # 19 bits at 20, sign at 56
            w |= (v & 0x7FFFF) << 20; w |= ((v >> 19) & 1) << 56
        else: raise ValueError(f'{form}.{k}: unsupported layout {t}')
    assert w & mask == fixed, (form, hex(w), hex(fixed))   # a field overlapped the opcode bits
    return w

def fits_imm20(v):   # the I forms' 20-bit immediate is sign-extended (integer) / the top 20 bits of an f32 (float)
    v &= 0xFFFFFFFF; s = v - (1 << 32) if v >> 31 else v
    return -(1 << 19) <= s < (1 << 19)
def fits_fimm20(v): return (v & 0xFFF) == 0

if __name__ == '__main__':
    print(len(FORMS), 'forms', len(FIELDS), 'structs')
    for f in ('FfmaR', 'FmulI', 'Mov32i', 'Tex', 'Ldc', 'Bra', 'Ipa'): print(f, {k: v for k, v in FIELDS[f].items()})
