"""Pair screenshots from two autoplay runs by similarity and lay the best pairs side by side.

Runs aren't frame-aligned (loading takes a little longer or shorter), so each frame of run A is
matched to the most similar frame of run B (small greyscale thumbnails, mean absolute
difference). Writes a sheet of the closest pairs and, for each, a full-resolution crop.

    python -I tools/ab_pairs.py <dir A> <dir B> <out.png> [--pairs 6] [--crop x0,y0,x1,y1]
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def thumb(path):
    return np.asarray(Image.open(path).convert('L').resize((96, 54)), dtype=np.float32)


def main():
    a_dir, b_dir, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    opts = dict(zip(sys.argv[4::2], sys.argv[5::2]))
    count = int(opts.get('--pairs', 6))
    a = sorted(a_dir.glob('*.png'))
    b = sorted(b_dir.glob('*.png'))
    ta = [thumb(p) for p in a]
    tb = [thumb(p) for p in b]
    pairs = []
    min_detail = float(opts.get('--min-detail', 30))
    for i, x in enumerate(ta):
        # Skip near-black and flat frames (fades, loading, credits on black): they match
        # trivially and show no textures.
        if x.mean() < 12 or x.std() < min_detail:
            continue
        diffs = [float(np.abs(x - y).mean()) for y in tb]
        j = int(np.argmin(diffs))
        pairs.append((diffs[j], i, j))
    pairs.sort()
    chosen, used = [], set()
    for d, i, j in pairs:
        if j in used:
            continue
        used.add(j)
        chosen.append((d, a[i], b[j]))
        if len(chosen) == count:
            break
    w, h = 960, 540
    sheet = Image.new('RGB', (2 * w, h * len(chosen)))
    for row, (d, pa, pb) in enumerate(chosen):
        sheet.paste(Image.open(pa).convert('RGB').resize((w, h)), (0, row * h))
        sheet.paste(Image.open(pb).convert('RGB').resize((w, h)), (w, row * h))
        print(f'{d:6.2f}  {pa.name}  {pb.name}')
    sheet.save(out)
    if '--crop' in opts:
        x0, y0, x1, y1 = map(int, opts['--crop'].split(','))
        for row, (d, pa, pb) in enumerate(chosen):
            ca = Image.open(pa).convert('RGB').crop((x0, y0, x1, y1))
            cb = Image.open(pb).convert('RGB').crop((x0, y0, x1, y1))
            pair = Image.new('RGB', (ca.width * 2, ca.height))
            pair.paste(ca, (0, 0))
            pair.paste(cb, (ca.width, 0))
            pair.save(out.with_name(f'{out.stem}-crop{row}.png'))
    print('wrote', out)


if __name__ == '__main__':
    main()
