"""Most-called functions in an address range (call sites across the whole translation).

    python -I mostcalled.py <generated dir> LO HI [N]
"""
import collections, glob, os, re, sys
gen, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
n = int(sys.argv[4]) if len(sys.argv) > 4 else 40
FUNC_RE = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{(.*?)\n\}', re.S)
CALL_RE = re.compile(r'\bsub_([0-9A-F]+)\(ctx, base\);')
calls = collections.Counter(); size = {}
for path in glob.glob(os.path.join(gen, 'fear_recomp.*.cpp')):
    for m in FUNC_RE.finditer(open(path, encoding='utf-8', errors='replace').read()):
        size[m.group(1)] = m.group(2).count('\n\t// ')
        for c in CALL_RE.findall(m.group(2)):
            if lo <= int(c, 16) < hi: calls[c] += 1
for c, k in calls.most_common(n):
    print('sub_%s %6d call sites, %5d instructions' % (c, k, size.get(c, -1)))
