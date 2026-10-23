"""Label guest functions with C++ class names from the MSVC RTTI in a dumped 360 image.

    python -I rtti_map.py <image.bin> <out.tsv> [CODE_LO CODE_HI]

The image is the loaded executable (base 0x82000000, big-endian, 32-bit pointers). RTTI type
descriptors are {vftable, spare, ".?AV<name>@@"}; complete object locators are {0, offset,
cdOffset, type descriptor, class hierarchy}; a vtable starts right after a pointer to its locator.
Writes one line per (function, class, vtable slot, object offset) and prints a summary. The output
comes from the game binary: keep it out of the repo.
"""
import re
import struct
import sys

BASE = 0x82000000

image_path, out_path, *code_range = sys.argv[1:]
code_lo, code_hi = (int(code_range[0], 16), int(code_range[1], 16)) if code_range else (0x82170000, 0x83100000)
image = open(image_path, 'rb').read()


def word(va):
    o = va - BASE
    return struct.unpack_from('>I', image, o)[0] if 0 <= o <= len(image) - 4 else None


type_descriptors = {}
for m in re.finditer(rb'\.\?A[VU][\x21-\x7e]{1,200}?@@\x00', image):
    type_descriptors[BASE + m.start() - 8] = m.group()[4:-3].decode()

locators = {}
for off in range(0, len(image) - 20, 4):
    td = struct.unpack_from('>I', image, off + 12)[0]
    if td in type_descriptors and struct.unpack_from('>I', image, off)[0] == 0:
        locators[BASE + off] = (type_descriptors[td], struct.unpack_from('>I', image, off + 4)[0])

rows = []
vtables = 0
for off in range(0, len(image) - 8, 4):
    locator = struct.unpack_from('>I', image, off)[0]
    if locator not in locators:
        continue
    name, object_offset = locators[locator]
    vtable = BASE + off + 4
    slot = 0
    while True:
        target = word(vtable + slot * 4)
        if target is None or not (code_lo <= target < code_hi) or target & 3:
            break
        rows.append((target, name, slot, object_offset))
        slot += 1
        if word(vtable + slot * 4) in locators:  # the next vtable's locator
            break
    vtables += bool(slot)

with open(out_path, 'w') as out:
    for target, name, slot, object_offset in sorted(rows):
        out.write('%08X\t%s\t%d\t%d\n' % (target, name, slot, object_offset))
print('type descriptors', len(type_descriptors), 'locators', len(locators), 'vtables', vtables,
      'functions labelled', len({r[0] for r in rows}))
