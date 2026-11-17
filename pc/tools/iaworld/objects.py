"""The object list: every placed object with its class and properties.

The same layout is in the 360's .wldsrvr (after magic "SDLW" and a version word,
big-endian) and in the object section of a PC .World00p (little-endian):

  u32 object count, then per object
    u16 class name length, class name (no terminator)
    u32 property count, u32 blob size, blob
    per property: u32 name offset in the blob, u32 type, u32 value

A string, vector, colour or rotation value is an offset into the blob; a float, bool or
integer is the value itself. Names and strings in the blob are NUL-terminated and padded
to four bytes.

Here a property is the tuple (name, type, value): str for the string types, a tuple of
float bit patterns for vectors, colours (3) and rotations (4), an int otherwise. Floats
stay as bit patterns so a value is written back exactly as it was read.
"""
import struct

from . import WorldError

STRING, VECTOR, COLOR, FLOAT, BOOL, LONG, ROTATION, COMMAND, STRING_ID = range(9)
TEXT_TYPES = (STRING, COMMAND, STRING_ID)
TUPLE_SIZE = {VECTOR: 3, COLOR: 3, ROTATION: 4}
TYPE_NAMES = ('string', 'vector', 'color', 'float', 'bool', 'long', 'rotation', 'command', 'string id')


class WorldObject:
    __slots__ = ('cls', 'props')

    def __init__(self, cls, props):
        self.cls = cls
        self.props = props  # list of (name, type, value)

    @property
    def name(self):
        for name, kind, value in self.props:
            if name == 'Name' and kind in TEXT_TYPES:
                return value
        return ''

    def get(self, wanted):
        """The property's (type, value), or None."""
        for name, kind, value in self.props:
            if name == wanted:
                return kind, value
        return None

    def text(self, wanted):
        """A text property's value, or None when it is absent or not text."""
        hit = self.get(wanted)
        return hit[1] if hit and hit[0] in TEXT_TYPES else None

    def set(self, wanted, value):
        for index, (name, kind, _old) in enumerate(self.props):
            if name == wanted:
                self.props[index] = (name, kind, value)
                return
        raise WorldError(f'{self.cls} "{self.name}" has no property {wanted}')

    def label(self):
        return f'{self.cls} "{self.name}"'


def parse(data, pos, big, what='object list'):
    """Returns (objects, offset after the list)."""
    order = '>' if big else '<'
    size = len(data)

    def need(count):
        if pos + count > size:
            raise WorldError(f'{what}: ends early at 0x{pos:X}')

    def text_at(blob, offset):
        end = blob.find(b'\0', offset)
        if offset >= len(blob) or end < 0:
            raise WorldError(f'{what}: a string offset points outside its object')
        return blob[offset:end].decode('latin-1')

    need(4)
    count = struct.unpack_from(order + 'I', data, pos)[0]
    pos += 4
    if count * 10 > size - pos:
        raise WorldError(f'{what}: object count {count} is larger than the file')
    objects = []
    for _ in range(count):
        need(2)
        class_size = struct.unpack_from(order + 'H', data, pos)[0]
        pos += 2
        need(class_size + 8)
        cls = data[pos:pos + class_size].decode('latin-1')
        pos += class_size
        prop_count, blob_size = struct.unpack_from(order + '2I', data, pos)
        pos += 8
        need(blob_size + 12 * prop_count)
        blob = bytes(data[pos:pos + blob_size])
        pos += blob_size
        table = struct.unpack_from(order + f'{3 * prop_count}I', data, pos)
        pos += 12 * prop_count
        props = []
        for index in range(prop_count):
            name_at, kind, value = table[3 * index:3 * index + 3]
            if kind in TEXT_TYPES:
                value = text_at(blob, value)
            elif kind in TUPLE_SIZE:
                width = TUPLE_SIZE[kind]
                if value + 4 * width > blob_size:
                    raise WorldError(f'{what}: a vector offset points outside its object')
                value = struct.unpack_from(order + f'{width}I', blob, value)
            elif kind == BOOL:
                value = 1 if value else 0
            props.append((text_at(blob, name_at), kind, value))
        objects.append(WorldObject(cls, props))
    return objects, pos


def parse_console(data, what='.wldsrvr'):
    if len(data) < 12 or data[:4] != b'SDLW':
        raise WorldError(f'{what}: not a 360 object file (no SDLW magic)')
    version = struct.unpack_from('>I', data, 4)[0]
    if version != 0x71:
        raise WorldError(f'{what}: version 0x{version:X}, expected 0x71')
    objects, end = parse(data, 8, True, what)
    if end != len(data):
        raise WorldError(f'{what}: {len(data) - end} bytes follow the object list')
    return objects


def parse_pc_world(data, what='PC world'):
    """The objects of a PC .World00p."""
    if len(data) < 20 or struct.unpack_from('<I', data, 0)[0] != 0x71:
        raise WorldError(f'{what}: not a version 0x71 PC world')
    start, end = struct.unpack_from('<2I', data, 12)
    objects, stop = parse(data, start, False, what)
    if stop != end:
        raise WorldError(f'{what}: the object list does not end where the blind data starts')
    return objects


def serialise(objects):
    """The little-endian object list."""

    def padded(text):
        raw = text.encode('latin-1') + b'\0'
        return raw + b'\0' * (-len(raw) % 4)

    out = bytearray(struct.pack('<I', len(objects)))
    for obj in objects:
        blob = bytearray()
        table = []
        for name, kind, value in obj.props:
            name_at = len(blob)
            blob += padded(name)
            if kind in TEXT_TYPES:
                stored = len(blob)
                blob += padded(value)
            elif kind in TUPLE_SIZE:
                stored = len(blob)
                blob += struct.pack(f'<{len(value)}I', *value)
            else:
                stored = value
            table += (name_at, kind, stored)
        cls = obj.cls.encode('latin-1')
        out += struct.pack('<H', len(cls)) + cls
        out += struct.pack('<2I', len(table) // 3, len(blob)) + blob
        out += struct.pack(f'<{len(table)}I', *table)
    return bytes(out)
