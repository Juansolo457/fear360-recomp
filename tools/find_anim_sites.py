"""Find the model-animation millisecond truncations (see port-nx/src/anim_timing.h) in a generated
translation, for the [[entrypoint.midasm_hook]] addresses of another build of the engine:
trk_Update (flags at 25(r3), rate at 12(r3), fmuls + fctidz) and the model update that turns
seconds into msDelta (lbz 32(r3) == 1, fmuls f0,f1,f0 by 1000.0 + fctidz). Prints each fctidz
address (function start + 4 * instruction index). On F.E.A.R. it finds exactly the three in
port-nx/fear_manifest.toml.

    python -I tools/find_anim_sites.py <generated/default folder>
"""
import glob, re, sys
for path in sorted(glob.glob(sys.argv[1] + '/*_recomp.*.cpp')):
    name = None; insns = []
    def check():
        if not name or len(insns) > 80: return
        text = '\n'.join(insns)
        base = int(name, 16)
        for i, ins in enumerate(insns):
            if ins != 'fctidz f0,f0': continue
            prev = insns[i - 1] if i else ''
            if prev == 'fmuls f0,f0,f13' and 'lbz r11,25(r3)' in text and 'lfs f0,12(r3)' in text:
                print(f'trk_Update   sub_{name}  fctidz at {base + 4 * i:#010x}')
            if prev == 'fmuls f0,f1,f0' and 'lbz r11,32(r3)' in text and 'mr r3,r4' in text:
                print(f'model msDelta sub_{name}  fctidz at {base + 4 * i:#010x}')
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', line)
        if m:
            check(); name = m.group(1); insns = []; continue
        m = re.match(r'\s*// (.*)$', line)
        if m and name: insns.append(m.group(1).strip())
    check()
