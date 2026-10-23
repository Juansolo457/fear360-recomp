"""Game-thread work per frame from profiler logs, and an A/B of two runs (#34).

    python -I abcompare.py LOG [LOG_B] [--min-draws 100] [--windows]

Each 10 s report window gives the frames (swaps), the main thread's share of a core and its
modules (the profiler's "modules:" line, fear_profile on). Work per frame is the main thread's
busy time minus the Direct3D wait (the game waiting for the GPU or the frame limit), divided by the
frames: the number the translator's options should shrink. Menus and loading screens (fewer than
--min-draws draws per frame) are left out. With two logs: medians side by side, B against A.
"""
import re
import statistics
import sys

WINDOW_S = 10.0


def windows(path):
    swaps = draws = gpu = rcpu = None
    out = []
    busy = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'last 10 s: \d+ packets, (\d+) draws, \d+ copies, (\d+) swaps', line)
        if m:
            draws, swaps = int(m.group(1)), int(m.group(2))
            continue
        m = re.search(r'per frame: renderer CPU ([\d.]+) ms, GPU ([\d.]+) ms', line)
        if m:
            rcpu, gpu = float(m.group(1)), float(m.group(2))
            continue
        m = re.search(r'\[profile\]\s+([\d.]+)% of a core  Main XThread', line)
        if m:
            busy = float(m.group(1))
            continue
        m = re.search(r'\[profile\]\s+modules: (.*)', line)
        if m and busy is not None:
            if swaps:
                mods = {k.strip(): float(v) for k, v in re.findall(r'([^,]+?) (\d+)%', m.group(1))}
                frame_ms = WINDOW_S * 1000 / swaps
                core_ms = busy / 100 * frame_ms  # main thread busy per frame
                wait = mods.pop('Direct3D wait', 0.0)
                out.append({
                    'fps': swaps / WINDOW_S, 'draws': draws / swaps, 'gpu': gpu, 'rcpu': rcpu,
                    'work': core_ms * (100 - wait) / 100,
                    'mods': {k: core_ms * v / 100 for k, v in mods.items()},
                })
            busy = None
    return out


def summary(ws):
    s = {'n': len(ws), 'fps': statistics.median(w['fps'] for w in ws),
         'work': statistics.median(w['work'] for w in ws),
         'draws': statistics.median(w['draws'] for w in ws),
         'gpu': statistics.median(w['gpu'] or 0 for w in ws),
         'rcpu': statistics.median(w['rcpu'] or 0 for w in ws), 'mods': {}}
    names = {k for w in ws for k in w['mods']}
    for k in names:
        s['mods'][k] = statistics.median(w['mods'].get(k, 0.0) for w in ws)
    return s


def main():
    args = sys.argv[1:]
    min_draws = 100
    if '--min-draws' in args:
        i = args.index('--min-draws')
        min_draws = float(args[i + 1])
        del args[i:i + 2]
    show = '--windows' in args
    logs = [a for a in args if not a.startswith('--')]
    sums = []
    for path in logs:
        ws = [w for w in windows(path) if w['draws'] >= min_draws]
        if show:
            print('==', path)
            for w in ws:
                print('  fps %6.1f  draws %5.0f  work %5.2f ms  gpu %5.2f ms' %
                      (w['fps'], w['draws'], w['work'], w['gpu'] or 0))
        if not ws:
            sys.exit(f'{path}: no gameplay windows with the profiler on (fear_profile true)')
        sums.append(summary(ws))
    labels = ['A', 'B'][:len(sums)]
    print('median over gameplay windows (%s)' % ', '.join(f'{l}: {p}' for l, p in zip(labels, logs)))
    row = lambda name, vals, fmt: print(f'  {name:<28}' + ''.join(fmt % v for v in vals) + (
        '  %+5.1f%%' % ((vals[1] / vals[0] - 1) * 100) if len(vals) == 2 and vals[0] else ''))
    row('windows', [s['n'] for s in sums], '%10d')
    row('fps', [s['fps'] for s in sums], '%10.1f')
    row('draws per frame', [s['draws'] for s in sums], '%10.0f')
    row('game thread work, ms/frame', [s['work'] for s in sums], '%10.2f')
    row('renderer CPU, ms/frame', [s['rcpu'] for s in sums], '%10.2f')
    row('GPU, ms/frame', [s['gpu'] for s in sums], '%10.2f')
    names = sorted({k for s in sums for k in s['mods']}, key=lambda k: -sums[0]['mods'].get(k, 0))
    print('  modules, ms/frame:')
    for k in names[:14]:
        row('  ' + k, [s['mods'].get(k, 0.0) for s in sums], '%10.2f')


if __name__ == '__main__':
    main()
