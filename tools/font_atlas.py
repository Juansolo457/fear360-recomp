"""Sharper text (#42): the 360's font atlases redrawn from their TrueType faces at 4x.

The 360 draws text from bitmap fonts made for a 1280x720 screen (interface\\fonts\\*.xpr: an
Xbox "XPR2" resource holding a k_4_4_4_4 glyph atlas and an ATG font table). At 4K every glyph
is magnified about 3x from 16 levels of coverage, so its edges stair-step. The PC install ships
the same faces (FEARL.Arch00: interface/fonts/*.TTF; Arial is the system's), so each atlas is
drawn again from its face, glyph for glyph at the same place, 4x larger. The game samples the
atlas with normalized coordinates, so the texture pack can put the larger one in its place.

Per atlas: the face's size and baseline are fitted to a few glyphs of the 360's (least squares
on the coverage, each glyph shifted to its best x), then its weight (a stroke, for the 360's
heavier hinted rendering) and, where the alpha channel is an outline, the outline's width. Each
glyph is then drawn in its cell at its best x. Glyphs past U+E000 (the controller's buttons) and
those the face lacks keep the 360's, upscaled.

Runs on the player's machine from their own copies, like the rest of the pack (tools/texture_pack.py
build): the faces belong to their owners and are never committed or shipped.
"""
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SCALE = 4
# The atlases (by file name, lower case) and their faces: a file in the PC archives, or 'arial'
# for the system's Arial. max_weight caps the fitted stroke: Neuropol's fit overshoots, and the
# 360's NeuropoX glyphs match the face drawn as it is.
FACES = {
    'arial_12.xpr': ('arial', 0.5),
    'arial_16.xpr': ('arial', 0.5),
    'arial_18.xpr': ('arial', 0.5),
    'digital_24.xpr': ('interface/fonts/DIGIT___.TTF', 0.5),
    'l_10646_12.xpr': ('interface/fonts/L_10646.TTF', 0.5),
    'neuropox_36.xpr': ('interface/fonts/NEUROPOL.TTF', 0.0),
    'neuropoxnail_36.xpr': ('interface/fonts/NEUROPOL.TTF', 0.0),
    'typist_12.xpr': ('interface/fonts/TYPIST.TTF', 0.5),
}
# Arial, or a face with its metrics (Liberation Sans), where the system has one.
ARIAL = [r'C:\Windows\Fonts\arial.ttf',
         '/usr/share/fonts/truetype/msttcorefonts/Arial.ttf',
         '/usr/share/fonts/liberation/LiberationSans-Regular.ttf',
         '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
         '/usr/share/fonts/TTF/LiberationSans-Regular.ttf']
# Glyphs the size and baseline are fitted on.
SAMPLE = 'AHOabgxyQ8%'


def is_font(name):
    name = name.replace('\\', '/').lower()
    return name.startswith('interface/fonts/') and name.endswith('.xpr')


def face_of(name):
    return FACES.get(name.replace('\\', '/').lower().rsplit('/', 1)[-1])


def texture_payload(xpr):
    """The atlas as fear_texindex reads a .tex: the 0x34-byte texture header (fetch constant at
    0x1C) and then its data. None if the file holds no texture."""
    _, header_size, data_size, count = struct.unpack_from('>4s3I', xpr, 0)
    for i in range(count):
        kind, offset, _, _ = struct.unpack_from('>4s3I', xpr, 16 + 16 * i)
        if kind == b'TX2D':
            head = xpr[12 + offset:12 + offset + 0x34]
            base = (struct.unpack_from('>I', head, 0x1C + 4)[0] >> 12) << 12
            return head + xpr[12 + header_size + base:12 + header_size + data_size]
    return None


def glyph_table(xpr):
    """[(x1, y1, x2, y2), ...] in atlas texels and {glyph index: first character}, from the ATG
    font data ("FontData": version, height, paddings, advance, the highest character, a
    character-to-glyph table, then 16-byte glyphs)."""
    _, _, _, count = struct.unpack_from('>4s3I', xpr, 0)
    data = None
    for i in range(count):
        kind, offset, size, _ = struct.unpack_from('>4s3I', xpr, 16 + 16 * i)
        if kind == b'USER':
            data = xpr[12 + offset:12 + offset + size]
    if data is None:
        return [], {}
    highest = struct.unpack_from('>H', data, 20)[0]
    table = struct.unpack_from(f'>{highest + 1}H', data, 22)
    at = 22 + 2 * (highest + 1)
    glyphs = struct.unpack_from('>I', data, at)[0]
    boxes = [struct.unpack_from('>4H', data, at + 4 + 16 * i) for i in range(glyphs)]
    chars = {}
    for char, glyph in enumerate(table):
        if glyph and glyph < glyphs:
            chars.setdefault(glyph, char)
    return boxes, chars


def _render(font, char, base, dx, width, height, stroke=0.0):
    img = Image.new('L', (width * SCALE, height * SCALE), 0)
    ImageDraw.Draw(img).text((dx * SCALE, base * SCALE), char, font=font, fill=255, anchor='ls',
                             stroke_width=int(round(stroke * SCALE)), stroke_fill=255)
    return np.asarray(img, np.float32) / 255


def _down(a):
    h, w = a.shape
    return a.reshape(h // SCALE, SCALE, w // SCALE, SCALE).mean((1, 3))


def _shift(a, k):
    out = np.zeros_like(a)
    if k > 0:
        out[:, k:] = a[:, :-k]
    elif k < 0:
        out[:, :k] = a[:, -k:]
    else:
        out[:] = a
    return out


def _err(a, b):
    return float(((a - b) ** 2).sum())


def _best_shift(r, t):
    return min(_err(_shift(r, k), t) for k in range(-8, 9))


def redraw(texels, width, height, xpr, ttf, max_weight=0.5):
    """The atlas (linear k_4_4_4_4 texels, as fear_texindex writes them) redrawn at SCALE x:
    an (H, W, 4) uint8 array, R G B A = the 360's X Y Z W components. Also a summary line."""
    px = np.frombuffer(texels, '<u2').reshape(height, width)
    ch = [((px >> (4 * c)) & 15).astype(np.float32) / 15 for c in range(4)]
    fill, alpha = ch[0], ch[3]
    boxes, chars = glyph_table(xpr)
    boxes = [(x1, y1, min(x2, width), min(y2, height)) for x1, y1, x2, y2 in boxes]
    fonts = {}

    def font(size):
        if size not in fonts:
            fonts[size] = ImageFont.truetype(ttf, int(round(size * SCALE)))
        return fonts[size]

    probe = ImageFont.truetype(ttf, 64)
    notdef = probe.getmask('\U0010FFFD')
    notdef = (notdef.size, bytes(notdef))

    def has(char):
        mask = probe.getmask(char)
        return mask.getbbox() is not None and (mask.size, bytes(mask)) != notdef

    text = [i for i, b in enumerate(boxes)
            if 0x20 < chars.get(i, 0) < 0xE000 and b[2] > b[0] and b[3] > b[1]
            and has(chr(chars[i]))]
    sample = [i for i in text if chr(chars[i]) in SAMPLE]

    def fit(target, size, base, stroke):
        total = 0.0
        for i in sample:
            x1, y1, x2, y2 = boxes[i]
            t = target[y1:y2, x1:x2]
            total += _best_shift(_down(_render(font(size), chr(chars[i]), base, 0, x2 - x1,
                                               y2 - y1, stroke)), t)
        return total

    # Size and baseline, then the weight, then the outline (when alpha is more than the fill).
    _, size, base = min((fit(fill, s, b, 0), s, b) for s in np.arange(10, 40, 0.5)
                        for b in np.arange(8, 40, 0.5)
                        if all(b <= boxes[i][3] - boxes[i][1] for i in sample))
    _, weight = min((fit(fill, size, base, w), w) for w in np.arange(0, max_weight + 0.01, 0.125))
    outline = alpha.sum() > fill.sum() * 1.05
    stroke = 0.0
    if outline:
        _, stroke = min((fit(alpha, size, base, weight + s), s) for s in np.arange(0, 4.01, 0.25))

    # The 360's atlas, upscaled, for what isn't redrawn; empty around the redrawn glyphs.
    up = [np.clip(np.asarray(Image.fromarray(c, 'F').resize((width * SCALE, height * SCALE),
                                                           Image.BICUBIC)), 0, 1) for c in ch]
    out = [np.zeros_like(u) for u in up]
    redrawn = set(text)
    for i, (x1, y1, x2, y2) in enumerate(boxes):
        if i not in redrawn:
            area = (slice(y1 * SCALE, y2 * SCALE), slice(x1 * SCALE, x2 * SCALE))
            for c in range(4):
                out[c][area] = up[c][area]
    error = total = 0.0
    for i in text:
        x1, y1, x2, y2 = boxes[i]
        char, t = chr(chars[i]), fill[y1:y2, x1:x2]
        w, h = x2 - x1, y2 - y1
        coarse = _down(_render(font(size), char, base, 0, w, h, weight))
        _, k = min((_err(_shift(coarse, k), t), k) for k in range(-8, 9))
        e, dx = min((_err(_down(_render(font(size), char, base, dx, w, h, weight)), t), dx)
                    for dx in np.arange(k - 1, k + 1.01, 0.25))
        error, total = error + e, total + float((t ** 2).sum())
        glyph = _render(font(size), char, base, dx, w, h, weight)
        edge = _render(font(size), char, base, dx, w, h, weight + stroke) if outline else glyph
        area = (slice(y1 * SCALE, y2 * SCALE), slice(x1 * SCALE, x2 * SCALE))
        for c in range(3):
            out[c][area] = glyph
        out[3][area] = edge
    rgba = (np.stack(out, -1) * 255 + 0.5).astype(np.uint8)
    summary = (f'size={size} base={base} weight={weight} outline={stroke if outline else "-"} '
               f'glyphs={len(text)}/{len(boxes)} fit={100 * error / max(total, 1):.0f}%')
    return rgba, summary
