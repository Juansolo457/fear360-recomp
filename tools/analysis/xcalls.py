"""Count calls from each 64 KB of code into a target address range.

    python -I xcalls.py <generated dir> LO HI
"""
import collections, glob, os, re, sys
gen, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
FUNC_RE = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{(.*?)\n\}', re.S)
CALL_RE = re.compile(r'\bsub_([0-9A-F]+)\(ctx, base\);')
by_region = collections.Counter(); callers = collections.defaultdict(set)
for path in glob.glob(os.path.join(gen, 'fear_recomp.*.cpp')):
    for m in FUNC_RE.finditer(open(path, encoding='utf-8', errors='replace').read()):
        a = int(m.group(1), 16)
        if lo <= a < hi: continue
        for c in CALL_RE.findall(m.group(2)):
            if lo <= int(c, 16) < hi:
                by_region[a >> 16] += 1; callers[a >> 16].add(a)
for k in sorted(by_region):
    print('%04X0000 %5d calls from %4d functions' % (k, by_region[k], len(callers[k])))
