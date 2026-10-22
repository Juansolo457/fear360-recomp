"""Find brief flashes (white flicker, a frame of wrong content) in a gameplay video.

Reads every frame, measures brightness and how different each frame is from its neighbours,
and flags frames that jump away from both the frame before and the frame after (a one- or
two-frame event, not a cut or a camera move, which only differ from one side). For each event
it writes a strip of the frames around it, full resolution, into <out>/.

  python -I tools/flicker_scan.py <video> <out folder> [--start s] [--end s] [--threshold 18]
"""
import sys
from pathlib import Path

import cv2
import numpy as np


def main():
    video, out = sys.argv[1], Path(sys.argv[2])
    opts = dict(zip(sys.argv[3::2], sys.argv[4::2]))
    start = float(opts.get('--start', 0))
    end = float(opts.get('--end', 1e9))
    threshold = float(opts.get('--threshold', 18))
    out.mkdir(parents=True, exist_ok=True)

    cap = cv2.VideoCapture(video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 60.0
    cap.set(cv2.CAP_PROP_POS_MSEC, start * 1000)
    frames, small, stamps = [], [], []
    while True:
        ok, frame = cap.read()
        t = cap.get(cv2.CAP_PROP_POS_MSEC) / 1000
        if not ok or t > end:
            break
        frames.append(frame)
        small.append(cv2.cvtColor(cv2.resize(frame, (160, 90)), cv2.COLOR_BGR2GRAY).astype(np.float32))
        stamps.append(t)
        # Keep memory bounded on long clips: full frames only for a sliding window.
        if len(frames) > 600:
            frames[len(frames) - 601] = None
    print(f'{len(small)} frames at {fps:.1f} fps from {video}')

    events = []
    for i in range(1, len(small) - 2):
        before = float(np.abs(small[i] - small[i - 1]).mean())
        # A one- or two-frame event: back to normal one or two frames later.
        after1 = float(np.abs(small[i] - small[i + 1]).mean())
        back1 = float(np.abs(small[i - 1] - small[i + 1]).mean())
        back2 = float(np.abs(small[i - 1] - small[i + 2]).mean())
        back = min(back1, back2)
        if before > threshold and before > 2.5 * back and (after1 > threshold or back2 < back1):
            brighter = float(small[i].mean() - small[i - 1].mean())
            events.append((i, before, back, brighter))
    print(f'{len(events)} events (threshold {threshold})')
    for n, (i, before, back, brighter) in enumerate(events[:40]):
        print(f'  {stamps[i]:8.3f} s  frame {i}: jump {before:.1f}, neighbours differ {back:.1f}, '
              f'brightness {brighter:+.1f}')
        strip = [frames[j] for j in range(max(0, i - 2), min(len(frames), i + 3))
                 if frames[j] is not None]
        if strip:
            h = 360
            strip = [cv2.resize(f, (int(f.shape[1] * h / f.shape[0]), h)) for f in strip]
            cv2.imwrite(str(out / f'event-{n:02d}-{stamps[i]:08.3f}s.png'), np.hstack(strip))


if __name__ == '__main__':
    main()
