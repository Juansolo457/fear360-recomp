"""Build a local texture pack: PC F.E.A.R. textures in place of the 360's (see docs/TEXTURES.md).

Runs entirely on the player's machine from their own copies; writes only under the output
folder (gitignored private/ by default).

  python -I tools/texture_pack.py bundle  <uberCatalog.cat> <work>          # 360 .tex -> bundle
  <port-nx>/fear_texindex <work>/x360.bundle <work>/x360.tsv <work>/x360_base.bin
  python -I tools/texture_pack.py build   <work> <PC install> <pack> [--formats dxt] [--limit N]
  python -I tools/texture_pack.py compare <work> <runtime.log>             # keys seen in a run

`build` matches 360 textures to PC files by path, decodes both base levels, and keeps a pair
only if the PC texture, scaled down to the 360 size, looks like the 360 one (the quality gate).
"""
import io
import os
import re
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

# Xenos TextureFormat numbers (rex::graphics::xenos::TextureFormat).
K_8_8_8_8, K_DXT1, K_DXT2_3, K_DXT4_5, K_DXN = 6, 18, 19, 20, 49
# VkFormat numbers.
VK_R8G8B8A8_UNORM, VK_BC1_RGBA, VK_BC2, VK_BC3, VK_BC5 = 37, 133, 135, 137, 141
DXGI_BC1, DXGI_BC2, DXGI_BC3, DXGI_BC5 = 71, 74, 77, 83

# The 360 format, what the PC DDS must be to copy its blocks as they are, and the pack format.
DXT_PLAN = {
    K_DXT1: (b'DXT1', VK_BC1_RGBA, DXGI_BC1),
    K_DXT2_3: (b'DXT3', VK_BC2, DXGI_BC2),
    K_DXT4_5: (b'DXT5', VK_BC3, DXGI_BC3),
}


# --- 360 catalogue -----------------------------------------------------------------------

def read_catalog(path):
    with open(path, 'rb') as f:
        _, _, count, names_off, names_size, _, _, _ = struct.unpack('>8I', f.read(32))
        entries = f.read(16 * count)
        f.seek(names_off)
        names = f.read(names_size)
    for i in range(count):
        name_off, data_off, size, csize = struct.unpack_from('>4I', entries, 16 * i)
        name = names[name_off:names.find(b'\0', name_off)].decode('latin-1')
        yield name, data_off, size, csize


def cmd_bundle(catalog, work):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    texs = [e for e in read_catalog(catalog) if e[0].lower().endswith('.tex')]
    with open(catalog, 'rb') as f, open(work / 'x360.bundle', 'wb') as out:
        out.write(b'FTB1' + struct.pack('<I', len(texs)))
        stored = 0
        for name, off, size, csize in texs:
            f.seek(off)
            # Entries that didn't shrink are stored as they are (compressed size 0 or = size).
            if csize == 0 or csize >= size:
                data = f.read(size)
                stored += 1
            else:
                data = zlib.decompress(f.read(csize))
            raw = name.encode('latin-1')
            out.write(struct.pack('<I', len(raw)) + raw + struct.pack('<I', len(data)) + data)
    print(f'bundled {len(texs)} textures ({stored} stored uncompressed) into {work / "x360.bundle"}')


# --- PC archives -------------------------------------------------------------------------

def read_ltar(path):
    """Yields (full path, offset, size) for every file in a LithTech LTAR v3 archive."""
    with open(path, 'rb') as f:
        magic, _, nsize, ndirs, nfiles, _, _, _ = struct.unpack('<4s7I', f.read(32))
        if magic != b'LTAR':
            return
        f.seek(48)
        names = f.read(nsize)
        files = f.read(32 * nfiles)
        dirs = f.read(16 * ndirs)
    name = lambda o: names[o:names.find(b'\0', o)].decode('latin-1')
    # Folder entries: name offset (the folder's full path), first subfolder, next sibling,
    # file count. File entries are grouped by folder in folder-table order.
    file_index = 0
    for d in range(ndirs):
        name_off, _, _, nfile = struct.unpack_from('<4I', dirs, 16 * d)
        folder = name(name_off).replace('\\', '/')
        for _ in range(nfile):
            if file_index >= nfiles:
                return
            name_off, off, _, size, _ = struct.unpack_from('<IQQQI', files, 32 * file_index)
            file_index += 1
            yield (folder + '/' + name(name_off)).strip('/'), off, size


def pc_textures(pc_root):
    found = {}
    # The base game's archives (FEAR.Arch00, FEAR_n.Arch00) win over the others.
    base_first = lambda p: (not p.name.upper().startswith(('FEAR.', 'FEAR_')), p.name.upper())
    for arch in sorted(Path(pc_root).glob('*.Arch00'), key=base_first):
        for path, off, size in read_ltar(arch):
            if path.lower().endswith('.dds'):
                found.setdefault(path.lower()[:-4], (arch, off, size, path))
    return found


# --- decoding ----------------------------------------------------------------------------

def dds_header(width, height, mips, fourcc=None, dxgi=None, rgba=False, linear_size=0):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x20000 if mips > 1 else 0) | (0x80000 if not rgba else 0x8)
    pf = (struct.pack('<II4s5I', 32, 0x4, b'DX10' if dxgi else fourcc, 0, 0, 0, 0, 0) if not rgba
          else struct.pack('<II4s5I', 32, 0x41, b'\0\0\0\0', 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000))
    caps = 0x1000 | (0x400008 if mips > 1 else 0)
    head = b'DDS ' + struct.pack('<7I', 124, flags, height, width, linear_size, 0, mips)
    head += b'\0' * 44 + pf + struct.pack('<5I', caps, 0, 0, 0, 0)
    if dxgi:
        head += struct.pack('<5I', dxgi, 3, 0, 1, 0)
    return head


def decode_blocks(blocks, fmt, width, height):
    """Decodes a linear base level (as fear_texindex writes it) to an RGBA array."""
    if fmt == K_8_8_8_8:
        return np.frombuffer(blocks, np.uint8).reshape(height, width, 4).copy()
    dxgi = {K_DXT1: DXGI_BC1, K_DXT2_3: DXGI_BC2, K_DXT4_5: DXGI_BC3, K_DXN: DXGI_BC5}[fmt]
    bw, bh = (width + 3) // 4 * 4, (height + 3) // 4 * 4
    img = Image.open(io.BytesIO(dds_header(bw, bh, 1, dxgi=dxgi) + blocks))
    return np.asarray(img.convert('RGBA'))[:height, :width]


def decode_pc(data):
    img = Image.open(io.BytesIO(data))
    img.load()
    return np.asarray(img.convert('RGBA'))


def box_down(img, width, height):
    """Box-filters an RGBA array to width x height, channels independently. (Pillow's resize
    premultiplies alpha, which blackens colour under transparent texels and failed good pairs.)"""
    h, w = img.shape[:2]
    if w % width == 0 and h % height == 0:
        fx, fy = w // width, h // height
        return img.reshape(height, fy, width, fx, -1).astype(np.float32).mean(axis=(1, 3))
    channels = [np.asarray(Image.fromarray(img[..., c]).resize((width, height), Image.BOX))
                for c in range(img.shape[2])]
    return np.stack(channels, axis=-1).astype(np.float32)


def psnr(a, b):
    mse = np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2)
    return 99.0 if mse < 1e-6 else 10 * np.log10(255 * 255 / mse)


def parse_dds(data):
    """(width, height, mips, fourcc or 'RGBA', data offset)."""
    height, width = struct.unpack_from('<II', data, 12)
    mips = max(1, struct.unpack_from('<I', data, 28)[0])
    fourcc = data[84:88]
    offset = 128
    if fourcc == b'DX10':
        offset += 20
    return width, height, mips, fourcc if fourcc.strip(b'\0') else b'RGBA', offset


def chain_bytes(width, height, mips, block_bytes, block):
    total = 0
    for level in range(mips):
        w, h = max(width >> level, 1), max(height >> level, 1)
        total += ((w + block - 1) // block) * ((h + block - 1) // block) * block_bytes
    return total


# --- build -------------------------------------------------------------------------------

def cmd_build(work, pc_root, pack, formats='dxt', limit=None, min_psnr=24.0):
    work, pack = Path(work), Path(pack)
    (pack / 'tex').mkdir(parents=True, exist_ok=True)
    rows = []
    with open(work / 'x360.tsv', encoding='latin-1') as f:
        header = f.readline().rstrip('\n').split('\t')
        for line in f:
            rows.append(dict(zip(header, line.rstrip('\n').split('\t'))))
    pc = pc_textures(pc_root)
    blob = open(work / 'x360_base.bin', 'rb')
    stats = {}
    report = []
    index_lines = ['# key\tvkformat\twidth\theight\tmips\tdata_offset\tfile']
    seen_keys = set()
    done = 0
    for r in rows:
        def note(status):
            stats[status] = stats.get(status, 0) + 1
            report.append(f"{status}\t{r['name']}")
        if r['status'] != 'ok':
            note('360-' + r['status'])
            continue
        fmt = int(r['format'])
        if formats == 'dxt' and fmt not in DXT_PLAN:
            note('format-not-in-this-pass')
            continue
        stem = r['name'].replace('\\', '/').lower()[:-4]
        match = pc.get(stem)
        if not match:
            note('no-pc-file')
            continue
        arch, off, size, pc_path = match
        with open(arch, 'rb') as f:
            f.seek(off)
            pc_data = f.read(size)
        width, height = int(r['width']), int(r['height'])
        pw, ph, pmips, pfour, poff = parse_dds(pc_data)
        if pw * ph <= width * height:
            note('pc-not-larger')
            continue
        if pw * height != ph * width:
            note('aspect-differs')
            continue
        want_fourcc, vkformat, _ = DXT_PLAN[fmt]
        if pfour != want_fourcc:
            note(f'pc-format-{pfour.decode("latin-1").strip(chr(0))}')
            continue
        # Quality gate: the PC base level, scaled to the 360 size, against the 360 base level.
        blob.seek(int(r['linear_offset']))
        x360 = decode_blocks(blob.read(int(r['linear_size'])), fmt, width, height)
        try:
            pc_img = decode_pc(pc_data)
        except Exception:
            note('pc-decode-failed')
            continue
        small = box_down(pc_img, width, height)
        channels = 3 if fmt == K_DXT1 else 4
        score = psnr(small[..., :channels], x360[..., :channels])
        if score < min_psnr:
            note('quality-gate')
            report[-1] += f'\tpsnr={score:.1f}'
            continue
        key = r['key']
        if key in seen_keys:
            note('duplicate-key')
            continue
        seen_keys.add(key)
        mips = pmips
        need = chain_bytes(pw, ph, mips, 8 if vkformat == VK_BC1_RGBA else 16, 4)
        if poff + need > len(pc_data):
            note('pc-truncated')
            continue
        out_name = f'tex/{key}.dds'
        (pack / out_name).write_bytes(pc_data)
        index_lines.append(f'{key}\t{vkformat}\t{pw}\t{ph}\t{mips}\t{poff}\t{out_name}')
        note('packed')
        report[-1] += f'\t{width}x{height}->{pw}x{ph}\tpsnr={score:.1f}\t{pc_path}'
        done += 1
        if limit and done >= limit:
            break
    (pack / 'index.tsv').write_text('\n'.join(index_lines) + '\n', encoding='utf-8')
    (pack / 'report.tsv').write_text('\n'.join(report) + '\n', encoding='utf-8')
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print(f'  {k}: {v}')
    print(f'pack: {pack} ({done} replacements)')


def cmd_compare(work, log):
    keys = {}
    with open(Path(work) / 'x360.tsv', encoding='latin-1') as f:
        f.readline()
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) > 1 and parts[1]:
                keys[parts[1]] = parts[0]
    seen = []
    pattern = re.compile(r'\[texpack\] key=([0-9A-F]{16}) format=(\d+) size=(\d+)x(\d+)')
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                seen.append(m.groups())
    hit = [s for s in seen if s[0] in keys]
    print(f'{len(seen)} distinct runtime keys, {len(hit)} found in the offline index')
    for s in seen[:10]:
        print('  ', s, keys.get(s[0], '(not in index)'))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    args, opts, rest = [], {}, sys.argv[2:]
    while rest:
        a = rest.pop(0)
        if a.startswith('--'):
            opts[a] = rest.pop(0) if rest else ''
        else:
            args.append(a)
    if cmd == 'bundle':
        cmd_bundle(*args[:2])
    elif cmd == 'build':
        cmd_build(args[0], args[1], args[2], formats=opts.get('--formats', 'dxt'),
                  limit=int(opts['--limit']) if '--limit' in opts else None)
    elif cmd == 'compare':
        cmd_compare(*args[:2])
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
