"""Compare texture names between the 360 catalogue and the PC archives (read-only).

The 360 build keeps its textures in Game/uberCatalog.cat as `.tex` entries; the PC build keeps
them in FEAR*.Arch00 as `.dds`. If the file stems match, PC textures can replace 360 ones by
name. This prints the overlap and samples; nothing is extracted or written.

    python -I tools/texture_names.py <port/assets/Game/uberCatalog.cat> <PC install folder>
"""
import glob
import os
import re
import sys


def stems(path, ext, limit=None):
    with open(path, 'rb') as f:
        data = f.read(limit) if limit else f.read()
    found = set()
    pattern = rb'([A-Za-z0-9_\-\. \\/]{2,160})\.' + ext.encode() + rb'(?![A-Za-z0-9])'
    for m in re.finditer(pattern, data, re.I):
        name = m.group(1).decode('latin-1').strip().lower().replace('\\', '/')
        found.add(name.rsplit('/', 1)[-1])
    return found


def main():
    catalog, pc_root = sys.argv[1], sys.argv[2]
    x360 = stems(catalog, 'tex')
    pc = set()
    for path in sorted(glob.glob(os.path.join(pc_root, '*.Arch00'))):
        # Name tables sit at the start of each archive.
        pc |= stems(path, 'dds', 64 << 20)
    both = x360 & pc
    print(f'360 .tex stems {len(x360)}, PC .dds stems {len(pc)}, exact matches {len(both)} '
          f'({100 * len(both) / max(1, len(x360)):.0f}% of 360)')
    print('matches:', sorted(both)[:15])
    print('360 only:', sorted(x360 - pc)[:15])
    print('PC only:', sorted(pc - x360)[:15])


if __name__ == '__main__':
    main()
