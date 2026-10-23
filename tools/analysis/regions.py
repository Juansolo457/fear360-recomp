"""Per 64 KB of guest code: function count, instruction count, sample strings referenced.

    python -I regions.py <generated dir> <image.bin> [LO HI]
"""
import collections, glob, os, re, struct, sys
gen, image_path, *rng = sys.argv[1:]
lo, hi = (int(rng[0], 16), int(rng[1], 16)) if rng else (0, 1 << 32)
image = open(image_path, 'rb').read()
BASE = 0x82000000
FUNC_RE = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{(.*?)\n\}', re.S)
LIS_RE = re.compile(r'// lis r(\d+),(-?\d+)')
MEM_RE = re.compile(r'// (?:addi|lwz|lfs|lhz|lbz|lfd|stw|stfs) [rf](\d+),(?:r(\d+),(-?\d+)|(-?\d+)\(r(\d+)\))')
def read_string(va):
    o = va - BASE
    if not (0 <= o < len(image)): return None
    end = image.find(b'\0', o, o + 120)
    if end <= o + 5: return None
    s = image[o:end]
    return s.decode() if all(32 <= c < 127 for c in s) else None
stats = collections.defaultdict(lambda: [0, 0, collections.Counter()])
for path in glob.glob(os.path.join(gen, 'fear_recomp.*.cpp')):
    text = open(path, encoding='utf-8', errors='replace').read()
    for m in FUNC_RE.finditer(text):
        a = int(m.group(1), 16)
        if not (lo <= a < hi): continue
        body = m.group(2)
        st = stats[a >> 16]
        st[0] += 1
        st[1] += body.count('\n\t// ')
        hireg = {}
        for line in body.splitlines():
            mm = LIS_RE.search(line)
            if mm:
                hireg[int(mm.group(1))] = (int(mm.group(2)) & 0xFFFF) << 16; continue
            mm = MEM_RE.search(line)
            if mm:
                b, off = (int(mm.group(2)), int(mm.group(3))) if mm.group(2) else (int(mm.group(5)), int(mm.group(4)))
                if b in hireg:
                    s = read_string((hireg[b] + off) & 0xFFFFFFFF)
                    if s and re.search('[A-Za-z]{4}', s): st[2][s] += 1
for k in sorted(stats):
    n, ins, strs = stats[k]
    sample = sorted(strs, key=lambda s: -len(s))[:5]
    print('%04X0000 %5d fn %7d ins | %s' % (k, n, ins, ' | '.join(s[:60] for s in sample)))
