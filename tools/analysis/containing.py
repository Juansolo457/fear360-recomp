"""Map guest addresses (e.g. return addresses) to the generated function containing them.

    python -I containing.py <generated dir> ADDR...
"""
import bisect, glob, os, re, sys
gen, *addrs = sys.argv[1:]
starts = set()
for path in glob.glob(os.path.join(gen, 'fear_recomp.*.cpp')):
    for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', open(path, encoding='utf-8', errors='replace').read()):
        starts.add(int(m.group(1), 16))
starts = sorted(starts)
for a in addrs:
    v = int(a, 16)
    i = bisect.bisect_right(starts, v) - 1
    print(a, '-> sub_%08X +0x%X' % (starts[i], v - starts[i]))
