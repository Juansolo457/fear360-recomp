"""Per report window: fps, renderer CPU/GPU per frame, main-thread busy, share of it in the D3D wait.

    python -I waitshare.py LOG...
"""
import re, sys
# The spin body, its caller and the timer read. The register save/restore helpers it calls
# (sub_8287B4CC/sub_8287B51C) are left out: every other function uses them too.
WAIT = ('sub_82973C88', 'sub_8298A2F0', 'sub_82965B38')
for path in sys.argv[1:]:
    print('==', path)
    swaps = draws = rcpu = gpu = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'last 10 s: \d+ packets, (\d+) draws, \d+ copies, (\d+) swaps', line)
        if m:
            draws, swaps = int(m.group(1)), int(m.group(2))
        m = re.search(r'per frame: renderer CPU ([\d.]+) ms, GPU ([\d.]+) ms', line)
        if m:
            rcpu, gpu = float(m.group(1)), float(m.group(2))
        m = re.search(r'\[profile\]\s+([\d.]+)% of a core  Main XThread[^:]*: (.*)', line)
        if m and swaps:
            busy = float(m.group(1))
            parts = dict((k, float(v)) for k, v in re.findall(r'(\S+) (\d+)%', m.group(2)))
            wait = sum(v for k, v in parts.items() if k in WAIT)
            print('fps %5.1f draws/frame %5.0f rcpu %4.1f gpu %4.1f | main %5.1f%% of a core, D3D wait %3.0f%% of it -> game work %4.1f%% of a core'
                  % (swaps / 10, draws / max(swaps, 1), rcpu or 0, gpu or 0, busy, wait, busy * (100 - wait) / 100))
