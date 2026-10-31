"""Build a local texture pack: PC F.E.A.R. textures in place of the 360's (see docs/TEXTURES.md).

Runs entirely on the player's machine from their own copies; writes only under the output
folder (gitignored private/ by default).

  python -I tools/texture_pack.py bundle  <uberCatalog.cat> <work>          # 360 .tex -> bundle
  <port-nx>/fear_texindex <work>/x360.bundle <work>/x360.tsv <work>/x360_base.bin
  python -I tools/texture_pack.py build   <work> <PC install> <pack> [--formats dxt] [--limit N]
                                          [--expansions FEARXP2,FEARXP]   # F.E.A.R. Files
  python -I tools/texture_pack.py compare <work> <runtime.log> [--pack <pack>]  # keys seen in a run

`build` matches 360 textures to PC files by path, decodes both base levels, and keeps a pair
only if the PC texture, scaled down to the 360 size, looks like the 360 one (the quality gate).
It also redraws the 360's font atlases (interface\\fonts\\*.xpr) at 4x from the PC install's
faces (tools/font_atlas.py, #42): sharper text.
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

# Run with -I, which leaves this folder off the path: its own modules.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import font_atlas  # noqa: E402

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
# PC block formats a pack can carry as they are.
PC_BLOCKS = {b'DXT1': (VK_BC1_RGBA, 8), b'DXT3': (VK_BC2, 16), b'DXT5': (VK_BC3, 16)}
# Host swizzle override (xenos encoding, 3 bits per guest component) for an RGB-ordered
# replacement of a 360 k_8_8_8_8 texture, whose texels sit B, G, R, A in memory: guest X (blue)
# reads host B, guest Z (red) host R.
SWIZZLE_BGRA = 2 | 1 << 3 | 0 << 6 | 3 << 9


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
    texs = [e for e in read_catalog(catalog)
            if e[0].lower().endswith('.tex') or font_atlas.is_font(e[0])]
    (work / 'fonts').mkdir(exist_ok=True)
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
            if font_atlas.is_font(name):
                # A font: its atlas as a .tex for fear_texindex, the file itself for `build`.
                (work / 'fonts' / re.split(r'[\\/]', name)[-1]).write_bytes(data)
                data = font_atlas.texture_payload(data) or b''
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


def pc_file(pc_root, path):
    """A file from the base game's archives by its path (case ignored), or None."""
    for arch in sorted(Path(pc_root).glob('*.Arch00')):
        for name, off, size in read_ltar(arch):
            if name.lower() == path.lower():
                with open(arch, 'rb') as f:
                    f.seek(off)
                    return f.read(size)
    return None


def pc_textures(pc_root, expansions=()):
    found = {}
    # The base game's archives (FEAR.Arch00, FEAR_n.Arch00) win over the others. With
    # expansions (folders of the install, e.g. FEARXP2, FEARXP: the first listed wins), their
    # archives come first: F.E.A.R. Files is the Perseus Mandate build, carrying Extraction Point.
    base_first = lambda p: (not p.name.upper().startswith(('FEAR.', 'FEAR_')), p.name.upper())
    archives = []
    for folder in expansions:
        archives += sorted((Path(pc_root) / folder).glob('*.Arch00'), key=base_first)
    archives += sorted(Path(pc_root).glob('*.Arch00'), key=base_first)
    for arch in archives:
        for path, off, size in read_ltar(arch):
            if path.lower().endswith('.dds'):
                found.setdefault(path.lower()[:-4], (arch, off, size, path))
    return found


# --- decoding ----------------------------------------------------------------------------

def dds_header(width, height, mips, fourcc=None, dxgi=None, rgba=False, linear_size=0, bgra=False):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x20000 if mips > 1 else 0) | (0x80000 if not rgba else 0x8)
    red, blue = (0xFF0000, 0xFF) if bgra else (0xFF, 0xFF0000)
    pf = (struct.pack('<II4s5I', 32, 0x4, b'DX10' if dxgi else fourcc, 0, 0, 0, 0, 0) if not rgba
          else struct.pack('<II4s5I', 32, 0x41, b'\0\0\0\0', 32, red, 0xFF00, blue, 0xFF000000))
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


def pc_kind(data):
    """'DXT1'/'DXT3'/'DXT5', 'BGRA' or 'BGRX' (32-bit uncompressed, B G R A in memory, without
    an alpha mask for BGRX), or what else the DDS holds."""
    flags, fourcc, bits, r, g, b, a = struct.unpack_from('<I4s5I', data, 80)
    if flags & 0x4:
        return fourcc.decode('latin-1').strip('\0')
    if bits == 32 and (r, g, b) == (0xFF0000, 0xFF00, 0xFF):
        return 'BGRA' if a == 0xFF000000 else 'BGRX' if a == 0 else 'rgb-other'
    return f'rgb{bits}'


def mip_chain(img, mips):
    """The levels of an (H, W, C) uint8 array, each box-filtered from the one before."""
    levels = [img]
    for level in range(1, mips):
        h, w = img.shape[:2]
        img = np.rint(box_down(img, max(w // 2, 1), max(h // 2, 1))).astype(np.uint8)
        levels.append(img)
    return levels


def bc4_encode(channel):
    """BC4 (unsigned) blocks of an (H, W) uint8 array: min/max endpoints, 8-value mode."""
    h, w = channel.shape
    ph, pw = (h + 3) // 4 * 4, (w + 3) // 4 * 4
    if (ph, pw) != (h, w):
        channel = np.pad(channel, ((0, ph - h), (0, pw - w)), mode='edge')
    blocks = channel.reshape(ph // 4, 4, pw // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    blocks = blocks.astype(np.float32)
    hi, lo = blocks.max(axis=1), blocks.min(axis=1)
    # Palette of the red0 > red1 mode: red0, red1, then six steps from red0 to red1.
    weights = np.array([0, 7, 1, 2, 3, 4, 5, 6], np.float32) / 7
    palette = hi[:, None] * (1 - weights) + lo[:, None] * weights
    index = np.abs(blocks[:, :, None] - palette[:, None, :]).argmin(axis=2).astype(np.uint64)
    index[hi == lo] = 0
    bits = (index << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1)
    out = np.zeros((len(blocks), 8), np.uint8)
    out[:, 0], out[:, 1] = hi.astype(np.uint8), lo.astype(np.uint8)
    for byte in range(6):
        out[:, 2 + byte] = (bits >> np.uint64(8 * byte)) & np.uint64(0xFF)
    return out


def bc5_encode(rg):
    """BC5 blocks (a BC4 block for red, then one for green) of an (H, W, 2) uint8 array."""
    return np.concatenate([bc4_encode(rg[..., 0]), bc4_encode(rg[..., 1])], axis=1).tobytes()


def correlation(a, b):
    """Mean Pearson correlation over the channels that aren't flat in either array."""
    scores = []
    for c in range(a.shape[-1]):
        x, y = a[..., c].ravel().astype(np.float64), b[..., c].ravel().astype(np.float64)
        if x.std() > 2 and y.std() > 2:
            scores.append(np.corrcoef(x, y)[0, 1])
    return float(np.mean(scores)) if scores else None


def chain_bytes(width, height, mips, block_bytes, block):
    total = 0
    for level in range(mips):
        w, h = max(width >> level, 1), max(height >> level, 1)
        total += ((w + block - 1) // block) * ((h + block - 1) // block) * block_bytes
    return total


# --- build -------------------------------------------------------------------------------

def plan_replacement(fmt, kind):
    """How a PC texture of `kind` replaces a 360 one of format `fmt`: ('copy', vkformat, swizzle)
    for PC blocks or texels used as they are, ('bc5', ...) for a normal map re-encoded to the
    360's two channels, or None."""
    if fmt == K_DXN:
        return ('bc5', VK_BC5, None) if kind in ('DXT1', 'DXT3', 'DXT5', 'BGRA', 'BGRX') else None
    block = PC_BLOCKS.get(kind.encode('latin-1'))
    if fmt in DXT_PLAN and block:
        return ('copy', block[0], None)
    if fmt == K_8_8_8_8:
        if block:
            return ('copy', block[0], SWIZZLE_BGRA)
        if kind in ('BGRA', 'BGRX'):
            return ('rgba8', VK_R8G8B8A8_UNORM, None)
    return None


def build_font(r, work, pc_root, pack, blob, note, report):
    """A font atlas redrawn from its face (font_atlas.py): its index line, or None."""
    name = re.split(r'[\\/]', r['name'])[-1]
    face = font_atlas.face_of(r['name'])
    if not face or int(r['format']) != 15:
        note('font-unknown')
        return None
    path, max_weight = face
    if path == 'arial':
        ttf = next((p for p in font_atlas.ARIAL if os.path.exists(p)), None)
    else:
        data = pc_file(pc_root, path)
        ttf = None
        if data:
            ttf = Path(work) / 'fonts' / Path(path).name
            ttf.write_bytes(data)
    if not ttf:
        note('font-face-missing')
        return None
    xpr = (Path(work) / 'fonts' / name).read_bytes()
    width, height = int(r['width']), int(r['height'])
    blob.seek(int(r['linear_offset']))
    rgba, summary = font_atlas.redraw(blob.read(int(r['linear_size'])), width, height, xpr,
                                      str(ttf), max_weight)
    h, w = rgba.shape[:2]
    out_name = f"fonts/{r['key']}.dds"
    (pack / 'fonts').mkdir(exist_ok=True)
    head = dds_header(w, h, 1, rgba=True)
    (pack / out_name).write_bytes(head + rgba.tobytes())
    note('font-redrawn')
    report[-1] += f'\t{width}x{height}->{w}x{h}\t{summary}'
    print(f'  font {name}: {summary}')
    return f"{r['key']}\t{VK_R8G8B8A8_UNORM}\t{w}\t{h}\t1\t{len(head)}\t{out_name}"


def cmd_build(work, pc_root, pack, formats='all', limit=None, min_psnr=24.0, min_corr=0.9,
              expansions=()):
    work, pack = Path(work), Path(pack)
    (pack / 'tex').mkdir(parents=True, exist_ok=True)
    rows = []
    with open(work / 'x360.tsv', encoding='latin-1') as f:
        header = f.readline().rstrip('\n').split('\t')
        for line in f:
            rows.append(dict(zip(header, line.rstrip('\n').split('\t'))))
    pc = pc_textures(pc_root, expansions)
    blob = open(work / 'x360_base.bin', 'rb')
    stats = {}
    report = []
    index_lines = ['# key\tvkformat\twidth\theight\tmips\tdata_offset\tfile[\tswizzle]']
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
        if font_atlas.is_font(r['name']):
            if formats != 'dxt' and r['key'] not in seen_keys:
                line = build_font(r, work, pc_root, pack, blob, note, report)
                if line:
                    seen_keys.add(r['key'])
                    index_lines.append(line)
                    done += 1
            continue
        if formats == 'dxt' and fmt not in DXT_PLAN:
            note('format-not-in-this-pass')
            continue
        if fmt not in DXT_PLAN and fmt not in (K_8_8_8_8, K_DXN):
            note(f'360-format-{fmt}')
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
        kind = pc_kind(pc_data)
        plan = plan_replacement(fmt, kind)
        if formats == 'dxt' and (not plan or kind != DXT_PLAN[fmt][0].decode()):
            plan = None
        if not plan:
            note(f'pc-format-{kind}')
            continue
        how, vkformat, swizzle = plan
        # Quality gate: the PC base level, scaled to the 360 size, against the 360 base level,
        # in the 360's logical channel order (R, G, B, A; a normal map's X, Y).
        blob.seek(int(r['linear_offset']))
        x360 = decode_blocks(blob.read(int(r['linear_size'])), fmt, width, height)
        if fmt == K_8_8_8_8:
            x360 = x360[..., [2, 1, 0, 3]]
        try:
            pc_img = decode_pc(pc_data)
        except Exception:
            note('pc-decode-failed')
            continue
        if pc_img.shape[:2] != (ph, pw):
            note('pc-decode-size')
            continue
        small = box_down(pc_img, width, height)
        channels = 2 if fmt == K_DXN else 3 if fmt == K_DXT1 and kind == 'DXT1' else 4
        score = psnr(small[..., :channels], x360[..., :channels])
        corr = correlation(small[..., :channels], x360[..., :channels]) if fmt == K_DXN else None
        # Normal maps: the 360's were re-derived (their strength differs a little), so a
        # strongly correlated pair passes with a lower PSNR.
        if score < min_psnr and not (corr is not None and corr >= min_corr):
            note('quality-gate')
            report[-1] += f'\tpsnr={score:.1f}' + (f'\tcorr={corr:.2f}' if corr is not None else '')
            continue
        key = r['key']
        if key in seen_keys:
            note('duplicate-key')
            continue
        seen_keys.add(key)
        mips = pmips
        out_name = f'tex/{key}.dds'
        if how == 'copy':
            block_bytes, block = (4, 1) if vkformat == VK_R8G8B8A8_UNORM else \
                (8 if vkformat == VK_BC1_RGBA else 16, 4)
            if poff + chain_bytes(pw, ph, mips, block_bytes, block) > len(pc_data):
                note('pc-truncated')
                continue
            (pack / out_name).write_bytes(pc_data)
            data_offset = poff
        elif how == 'rgba8':
            need = chain_bytes(pw, ph, mips, 4, 1)
            if poff + need > len(pc_data):
                note('pc-truncated')
                continue
            texels = pc_data[poff:poff + need]
            if kind == 'BGRX':
                texels = np.frombuffer(texels, np.uint8).copy()
                texels[3::4] = 255
                texels = texels.tobytes()
            head = dds_header(pw, ph, mips, rgba=True, bgra=True)
            (pack / out_name).write_bytes(head + texels)
            data_offset = len(head)
        else:  # bc5
            mips = min(mips, max(pw, ph).bit_length())
            data = b''.join(bc5_encode(level) for level in mip_chain(pc_img[..., :2], mips))
            head = dds_header(pw, ph, mips, dxgi=DXGI_BC5)
            (pack / out_name).write_bytes(head + data)
            data_offset = len(head)
        line = f'{key}\t{vkformat}\t{pw}\t{ph}\t{mips}\t{data_offset}\t{out_name}'
        index_lines.append(line + (f'\t{swizzle:X}' if swizzle is not None else ''))
        note('packed')
        report[-1] += f'\t{width}x{height}->{pw}x{ph}\tpsnr={score:.1f}\t{pc_path}\t{how}:{kind}'
        done += 1
        if limit and done >= limit:
            break
    (pack / 'index.tsv').write_text('\n'.join(index_lines) + '\n', encoding='utf-8')
    (pack / 'report.tsv').write_text('\n'.join(report) + '\n', encoding='utf-8')
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print(f'  {k}: {v}')
    print(f'pack: {pack} ({done} replacements)')


def cmd_compare(work, log, pack=None):
    """Runtime keys (a log with fear_texture_log_keys) against the offline index and, given the
    pack, what the build decided for each catalogue texture the game used."""
    keys, by_shape = {}, {}
    with open(Path(work) / 'x360.tsv', encoding='latin-1') as f:
        header = f.readline().rstrip('\n').split('\t')
        for line in f:
            r = dict(zip(header, line.rstrip('\n').split('\t')))
            if r.get('key'):
                keys[r['key']] = r['name']
            shape = (r['format'], r['width'], r['height'])
            by_shape.setdefault(shape, []).append(r['name'])
    decided = {}
    if pack:
        with open(Path(pack) / 'report.tsv', encoding='latin-1') as f:
            for line in f:
                parts = line.rstrip('\n').split('\t')
                if len(parts) > 1:
                    decided[parts[1]] = parts[0]
    seen = []
    pattern = re.compile(r'\[texpack\] key=([0-9A-F]{16}) format=(\d+) size=(\d+)x(\d+)')
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                seen.append(m.groups())
    hit = [s for s in seen if s[0] in keys]
    print(f'{len(seen)} distinct runtime keys, {len(hit)} found in the offline index')
    outcome = {}
    for s in hit:
        status = decided.get(keys[s[0]], '?')
        outcome[status] = outcome.get(status, 0) + 1
    for status, n in sorted(outcome.items(), key=lambda kv: -kv[1]):
        print(f'  {status}: {n}')
    # Keys the index doesn't know: dynamic content (videos, render targets) or a catalogue
    # texture whose runtime bytes differ from the file. A shape with catalogue candidates is
    # worth a look.
    missing = {}
    for s in seen:
        if s[0] not in keys:
            shape = (s[1], s[2], s[3])
            missing[shape] = missing.get(shape, 0) + 1
    print(f'{len(seen) - len(hit)} not in the index, by format/size (catalogue textures of that shape):')
    for shape, n in sorted(missing.items(), key=lambda kv: -kv[1])[:25]:
        candidates = by_shape.get(shape, [])
        print(f'  {n:5d}  fmt={shape[0]:>2} {shape[1]}x{shape[2]}  ({len(candidates)})',
              ', '.join(candidates[:3]))


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
        cmd_build(args[0], args[1], args[2], formats=opts.get('--formats', 'all'),
                  limit=int(opts['--limit']) if '--limit' in opts else None,
                  expansions=[e for e in opts.get('--expansions', '').split(',') if e])
    elif cmd == 'compare':
        cmd_compare(args[0], args[1], pack=opts.get('--pack'))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
