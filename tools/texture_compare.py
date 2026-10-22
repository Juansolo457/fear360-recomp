"""Compare texture sizes between the 360 catalogue and the PC archives (read-only).

360: Game/uberCatalog.cat (big-endian): 32-byte header (version, header size, entry count, name
table offset/size, data offset), then 16-byte entries (name offset, data offset, size,
compressed size) of zlib streams. A `.tex` payload starts with a 0x1C-byte header followed by
the 6-dword Xenos texture fetch constant (format, size, dimension).

PC: FEAR*.Arch00, LithTech `LTAR` v3: 48-byte header (magic, version, name table size, folder
count, file count, ...), the name table, then 32-byte file entries (name offset, offset,
stored size, size, flags). Textures are plain DDS.

    python -I tools/texture_compare.py <uberCatalog.cat> <PC install folder> [--csv out.csv]
"""
import collections
import glob
import os
import struct
import sys
import zlib

XENOS_FORMATS = {0x02: '8', 0x06: '8_8_8_8', 0x07: '2_10_10_10', 0x12: 'DXT1', 0x13: 'DXT2_3',
                 0x14: 'DXT4_5', 0x31: 'DXN', 0x3A: 'DXT5A', 0x3B: 'CTX1', 0x04: '1_5_5_5',
                 0x05: '5_6_5', 0x08: '8_8', 0x0A: '4_4_4_4'}


def read_360(catalog):
    out = {}
    with open(catalog, 'rb') as f:
        head = f.read(32)
        _, _, count, names_off, names_size, _, _, _ = struct.unpack('>8I', head)
        entries = f.read(16 * count)
        f.seek(names_off)
        names = f.read(names_size)
        for i in range(count):
            name_off, data_off, size, csize = struct.unpack_from('>4I', entries, 16 * i)
            end = names.find(b'\0', name_off)
            name = names[name_off:end].decode('latin-1')
            if not name.lower().endswith('.tex'):
                continue
            f.seek(data_off)
            blob = f.read(min(csize, 4096))
            try:
                header = zlib.decompressobj().decompress(blob, 64)
            except zlib.error:
                continue
            if len(header) < 0x34:
                continue
            fetch = struct.unpack_from('>6I', header, 0x1C)
            fmt = fetch[1] & 0x3F
            width = (fetch[2] & 0x1FFF) + 1
            height = ((fetch[2] >> 13) & 0x1FFF) + 1
            dim = (fetch[5] >> 9) & 3
            key = os.path.splitext(name.replace('\\', '/').rsplit('/', 1)[-1])[0].lower()
            out[key] = (name, width, height, XENOS_FORMATS.get(fmt, hex(fmt)), dim, size)
    return out


def read_pc(root):
    out = {}
    for path in sorted(glob.glob(os.path.join(root, '*.Arch00'))):
        with open(path, 'rb') as f:
            magic, _, nsize, _, nfiles, _, _, _ = struct.unpack('<4s7I', f.read(32))
            if magic != b'LTAR':
                continue
            f.seek(48)
            names = f.read(nsize)
            table = f.read(32 * nfiles)
            for i in range(nfiles):
                name_off, off, _, size, _ = struct.unpack_from('<IQQQI', table, 32 * i)
                end = names.find(b'\0', name_off)
                name = names[name_off:end].decode('latin-1')
                if not name.lower().endswith('.dds'):
                    continue
                f.seek(off)
                dds = f.read(128)
                if dds[:4] != b'DDS ':
                    continue
                height, width = struct.unpack_from('<II', dds, 12)
                fourcc = dds[84:88].decode('latin-1').strip('\0') or 'RGBA'
                key = os.path.splitext(name)[0].lower()
                prev = out.get(key)
                if not prev or width * height > prev[1] * prev[2]:
                    out[key] = (os.path.basename(path) + ':' + name, width, height, fourcc, size)
    return out


def main():
    catalog, pc_root = sys.argv[1], sys.argv[2]
    csv = sys.argv[sys.argv.index('--csv') + 1] if '--csv' in sys.argv else None
    x = read_360(catalog)
    pc = read_pc(pc_root)
    both = sorted(set(x) & set(pc))
    verdict = collections.Counter()
    ratio = collections.Counter()
    rows = []
    for key in both:
        _, xw, xh, xf, xd, _ = x[key]
        pname, pw, ph, pf, _ = pc[key]
        r = (pw * ph) / max(1, xw * xh)
        verdict['PC larger' if r > 1 else 'same size' if r == 1 else 'PC smaller'] += 1
        if r > 1:
            ratio[f'{r:g}x pixels'] += 1
        rows.append((key, xw, xh, xf, pw, ph, pf, r))
    print(f'360 textures {len(x)}, PC textures {len(pc)}, matched {len(both)}')
    for k, v in verdict.most_common():
        print(f'  {k}: {v}')
    print('  where PC is larger:', dict(ratio.most_common(8)))
    print('360 formats:', dict(collections.Counter(x[k][3] for k in x).most_common(8)))
    print('360 sizes:', dict(collections.Counter(f'{x[k][1]}x{x[k][2]}' for k in x).most_common(8)))
    print('PC sizes (matched):', dict(collections.Counter(f'{pc[k][1]}x{pc[k][2]}' for k in both).most_common(8)))
    print('largest gains:', [(r[0], f'{r[1]}x{r[2]}', f'{r[4]}x{r[5]}') for r in sorted(rows, key=lambda r: -r[7])[:10]])
    if csv:
        with open(csv, 'w', encoding='utf-8') as out:
            out.write('name,x360_w,x360_h,x360_fmt,pc_w,pc_h,pc_fmt,pixel_ratio\n')
            for r in rows:
                out.write(','.join(map(str, r)) + '\n')
        print('wrote', csv)


if __name__ == '__main__':
    main()
