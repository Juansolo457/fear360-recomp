"""Read a LithTech game database (.gamedb) from the Xbox 360 F.E.A.R. ("BDAG", big-endian).

Layout, as read from the 360 files:
  header      magic "BDAG", version 3, string table size, then counts (categories,
              records, attributes, values)
  strings     NUL-terminated, 4-byte aligned; ASCII names, UTF-16BE string values
  categories  count, then per category: name, record count, records
  record      name, attribute count, then attributes
  attribute   name, type, flags, value count, values (one 32-bit word each)
Attribute names are flat ("Bindings.0.DeadZoneMin" is member DeadZoneMin of struct
Bindings, element 0). Value words by type: 0 bool, 1 int, 2 float, 4 string offset, 5 wide
string offset, 10 struct (element count), others as raw words.

  python -I tools/gamedb.py <file.gamedb> [name filter regex]
"""
import re
import struct
import sys

TYPE_NAMES = {0: 'bool', 1: 'int', 2: 'float', 4: 'string', 5: 'wstring', 10: 'struct'}


def read(path):
    """Yields (category, record, attribute, type, values, value_offsets)."""
    data = open(path, 'rb').read()
    magic, version, string_size = struct.unpack_from('>4sII', data, 0)
    if magic != b'BDAG':
        raise ValueError(f'{path}: not a 360 game database')
    strings_at = 0x1C

    def text(offset):
        start = strings_at + offset
        return data[start:data.index(b'\0', start)].decode('latin-1')

    def wide(offset):
        start = strings_at + offset
        end = start
        while data[end:end + 2] != b'\0\0':
            end += 2
        return data[start:end].decode('utf-16-be')

    pos = strings_at + string_size

    def word():
        nonlocal pos
        value = struct.unpack_from('>I', data, pos)[0]
        pos += 4
        return value

    for _ in range(word()):
        category = text(word())
        for _ in range(word()):
            record = text(word())
            for _ in range(word()):
                name, kind, _flags, count = word(), word(), word(), word()
                offsets, values = [], []
                for _ in range(count):
                    offsets.append(pos)
                    raw = word()
                    if kind == 2:
                        values.append(struct.unpack('>f', struct.pack('>I', raw))[0])
                    elif kind == 4:
                        values.append(text(raw))
                    elif kind == 5:
                        values.append(wide(raw))
                    else:
                        values.append(raw)
                yield category, record, text(name), kind, values, offsets


def main():
    pattern = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
    for category, record, name, kind, values, offsets in read(sys.argv[1]):
        if pattern and not pattern.search(f'{category}/{record}/{name}'):
            continue
        shown = ', '.join(f'{v:g}' if isinstance(v, float) else repr(v) for v in values)
        print(f'{category}/{record}  {name} [{TYPE_NAMES.get(kind, kind)}] = {shown}'
              f'  @0x{offsets[0]:X}' if offsets else f'{category}/{record}  {name} [] (none)')


if __name__ == '__main__':
    main()
