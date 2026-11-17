"""Draws the controller button picture the PC build shows in prompts.

    python -I pc/tools/make_glyphs.py <out.dds> [--png preview.png]

One 1024x1024 picture of 128-pixel cells, 8 across: the Xbox buttons in cells 0 to 31 and the
PlayStation buttons in cells 32 to 63. A cell's number within its set is the 360 game's
character for the button less U+E000 (pc/core/include/fearpc/layout.h): 0 A, 1 B, 2 X, 3 Y,
4 Start, 5 Back, 6 L3, 7 R3, 8 LB, 9 RB, 10 to 13 D-pad up/down/left/right, 14 left stick,
15 right stick, 16 LT, 17 RT. The art is drawn here from shapes and a system font; nothing
comes from either console's files.
"""
import argparse
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

CELL = 128
COLUMNS = 8
SETS = 2
PER_SET = 32
SCALE = 4  # drawn this much larger, then reduced
S = CELL * SCALE

DARK = (34, 34, 36, 255)
RIM = (12, 12, 14, 255)
LIGHT = (224, 224, 228, 255)
WHITE = (255, 255, 255, 255)
MID = (96, 96, 102, 255)


def font(size):
    for name in ('arialbd.ttf', 'Arial Bold.ttf', 'LiberationSans-Bold.ttf', 'DejaVuSans-Bold.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    sys.exit('No bold sans-serif font found (Arial, Liberation Sans or DejaVu Sans).')


def cell():
    image = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    return image, ImageDraw.Draw(image)


def u(value):
    """A length given in 128ths of the cell."""
    return value * SCALE


def text(draw, label, size, fill, center=(64, 64)):
    draw.text((u(center[0]), u(center[1])), label, font=font(u(size)), fill=fill, anchor='mm')


def disc(draw, fill, radius=54, rim=RIM, rim_width=5):
    draw.ellipse((u(64 - radius), u(64 - radius), u(64 + radius), u(64 + radius)), fill=rim)
    inner = radius - rim_width
    draw.ellipse((u(64 - inner), u(64 - inner), u(64 + inner), u(64 + inner)), fill=fill)


def face(label, fill):
    image, draw = cell()
    disc(draw, fill)
    text(draw, label, 74, WHITE, (64, 66))
    return image


def triangle(draw, points, fill):
    draw.polygon([(u(x), u(y)) for x, y in points], fill=fill)


def start_back(direction):
    image, draw = cell()
    disc(draw, LIGHT, radius=40)
    if direction > 0:
        triangle(draw, [(52, 44), (52, 84), (86, 64)], DARK)
    else:
        triangle(draw, [(76, 44), (76, 84), (42, 64)], DARK)
    return image


def stick(label, moving, size=50):
    image, draw = cell()
    radius = 40 if moving else 54
    disc(draw, LIGHT, radius=radius)
    disc(draw, DARK, radius=radius - 9, rim=DARK, rim_width=0)
    text(draw, label, size if not moving else size * 0.8, WHITE, (64, 66))
    if moving:
        for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            tip = (64 + dx * 62, 64 + dy * 62)
            base = (64 + dx * 47, 64 + dy * 47)
            side = (dy * 12, dx * 12)
            triangle(draw, [tip, (base[0] + side[0], base[1] + side[1]),
                            (base[0] - side[0], base[1] - side[1])], LIGHT)
    return image


def shoulder(label):
    image, draw = cell()
    draw.rounded_rectangle((u(6), u(32), u(122), u(96)), radius=u(22), fill=RIM)
    draw.rounded_rectangle((u(11), u(37), u(117), u(91)), radius=u(18), fill=LIGHT)
    text(draw, label, 48, DARK, (64, 66))
    return image


def trigger(label):
    image, draw = cell()
    # Taller than a shoulder button, with the top rounded over like a trigger seen from behind.
    draw.rounded_rectangle((u(20), u(8), u(108), u(120)), radius=u(40), fill=RIM)
    draw.rectangle((u(20), u(80), u(108), u(104)), fill=RIM)
    draw.rounded_rectangle((u(20), u(80), u(108), u(120)), radius=u(14), fill=RIM)
    draw.rounded_rectangle((u(25), u(13), u(103), u(115)), radius=u(36), fill=LIGHT)
    draw.rectangle((u(25), u(80), u(103), u(102)), fill=LIGHT)
    draw.rounded_rectangle((u(25), u(80), u(103), u(115)), radius=u(10), fill=LIGHT)
    text(draw, label, 46, DARK, (64, 70))
    return image


def dpad(direction):
    image, draw = cell()
    arm, reach = 19, 56
    for grow, fill in ((5, RIM), (0, MID)):
        draw.rounded_rectangle((u(64 - arm - grow), u(64 - reach - grow), u(64 + arm + grow), u(64 + reach + grow)),
                               radius=u(8), fill=fill)
        draw.rounded_rectangle((u(64 - reach - grow), u(64 - arm - grow), u(64 + reach + grow), u(64 + arm + grow)),
                               radius=u(8), fill=fill)
    dx, dy = {'up': (0, -1), 'down': (0, 1), 'left': (-1, 0), 'right': (1, 0)}[direction]
    near, far = arm, reach
    box = (64 + dx * near - (arm if dx == 0 else 0), 64 + dy * near - (arm if dy == 0 else 0),
           64 + dx * far + (arm if dx == 0 else 0), 64 + dy * far + (arm if dy == 0 else 0))
    box = (min(box[0], box[2]), min(box[1], box[3]), max(box[0], box[2]), max(box[1], box[3]))
    draw.rounded_rectangle(tuple(u(v) for v in box), radius=u(8), fill=WHITE)
    return image


def ps_face(shape):
    image, draw = cell()
    disc(draw, DARK, rim=LIGHT, rim_width=5)
    width = u(9)
    if shape == 'cross':
        color = (124, 178, 232, 255)
        draw.line((u(40), u(40), u(88), u(88)), fill=color, width=width)
        draw.line((u(88), u(40), u(40), u(88)), fill=color, width=width)
    elif shape == 'circle':
        draw.ellipse((u(36), u(36), u(92), u(92)), outline=(255, 102, 102, 255), width=width)
    elif shape == 'square':
        draw.rectangle((u(39), u(39), u(89), u(89)), outline=(255, 128, 232, 255), width=width)
    else:
        color = (64, 226, 160, 255)
        points = [(64, 34), (93, 86), (35, 86)]
        draw.line([(u(x), u(y)) for x, y in points + points[:2]], fill=color, width=width, joint='curve')
    return image


def ps_start():
    image, draw = cell()
    draw.rounded_rectangle((u(22), u(40), u(106), u(88)), radius=u(12), fill=RIM)
    triangle(draw, [(30, 46), (30, 82), (98, 64)], LIGHT)
    return image


def ps_select():
    image, draw = cell()
    draw.rounded_rectangle((u(20), u(42), u(108), u(86)), radius=u(10), fill=RIM)
    draw.rounded_rectangle((u(26), u(48), u(102), u(80)), radius=u(6), fill=LIGHT)
    return image


def sets():
    pads = ('up', 'down', 'left', 'right')
    xbox = {
        0: face('A', (86, 176, 52, 255)), 1: face('B', (214, 50, 44, 255)),
        2: face('X', (44, 112, 214, 255)), 3: face('Y', (232, 176, 24, 255)),
        4: start_back(1), 5: start_back(-1),
        6: stick('L', False), 7: stick('R', False),
        8: shoulder('LB'), 9: shoulder('RB'),
        14: stick('L', True), 15: stick('R', True),
        16: trigger('LT'), 17: trigger('RT'),
    }
    playstation = {
        0: ps_face('cross'), 1: ps_face('circle'), 2: ps_face('square'), 3: ps_face('triangle'),
        4: ps_start(), 5: ps_select(),
        6: stick('L3', False, 42), 7: stick('R3', False, 42),
        8: shoulder('L1'), 9: shoulder('R1'),
        14: stick('L', True), 15: stick('R', True),
        16: trigger('L2'), 17: trigger('R2'),
    }
    for index, direction in enumerate(pads):
        xbox[10 + index] = dpad(direction)
        playstation[10 + index] = dpad(direction)
    return xbox, playstation


def picture():
    rows = SETS * PER_SET // COLUMNS
    sheet = Image.new('RGBA', (COLUMNS * CELL, rows * CELL), (0, 0, 0, 0))
    for set_index, cells in enumerate(sets()):
        for number, image in cells.items():
            index = set_index * PER_SET + number
            small = image.resize((CELL, CELL), Image.LANCZOS)
            sheet.paste(small, ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
    return sheet


def write_dds(sheet, path):
    """Uncompressed A8R8G8B8 with a full mip chain."""
    levels = [sheet]
    while levels[-1].size != (1, 1):
        width, height = levels[-1].size
        levels.append(levels[-1].resize((max(width // 2, 1), max(height // 2, 1)), Image.BOX))
    width, height = sheet.size
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000  # caps, height, width, pitch, format, mips
    pixel_format = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32,
                               0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<IIIIIII44s', 124, flags, height, width, width * 4, 0, len(levels), b'')
    header += pixel_format + struct.pack('<IIIII', 0x8 | 0x1000 | 0x400000, 0, 0, 0, 0)
    assert len(header) == 124
    with open(path, 'wb') as out:
        out.write(b'DDS ' + header)
        for level in levels:
            red, green, blue, alpha = level.split()
            out.write(Image.merge('RGBA', (blue, green, red, alpha)).tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('out')
    parser.add_argument('--png', help='also write the picture as a PNG, to look at')
    args = parser.parse_args()
    sheet = picture()
    write_dds(sheet, args.out)
    if args.png:
        sheet.save(args.png)


if __name__ == '__main__':
    main()
