"""Object classes and their properties, read from the PC game's GameServer.dll.

The engine asks the game DLL for a table of classes. Each class record sits in the DLL's
.data section and starts with these 32-bit fields:

  0  pointer to the class name          8  number of properties
  1  pointer to an empty string         9  pointer to the property records
  2  pointer to the parent class, or 0

A property record is 44 bytes: name pointer, default string pointer, type, a default
vector (3 floats), a default float, then four words not used here. The type numbers are
the ones in an object list (see objects.py). A class has its parents' properties too.

The DLL has no directory of these records, so they are found by their shape: every aligned
position in .data is tried as a class record and kept if all its fields make sense.
"""
import math
import struct

from . import WorldError
from . import objects as O

MOST_PROPERTIES = 400
_NAME_CHARS = frozenset(b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_')


class _Image:
    """Just enough of a PE file to follow pointers."""

    def __init__(self, data, what):
        self.data = data
        if len(data) < 0x40 or data[:2] != b'MZ':
            raise WorldError(f'{what}: not a Windows DLL')
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if pe + 24 + 96 > len(data) or data[pe:pe + 4] != b'PE\0\0':
            raise WorldError(f'{what}: not a Windows DLL')
        section_count = struct.unpack_from('<H', data, pe + 6)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        self.sections = []  # (name, address, size in memory, size in file, file offset)
        for index in range(section_count):
            at = pe + 24 + optional_size + 40 * index
            if at + 40 > len(data):
                raise WorldError(f'{what}: section table is cut short')
            name = data[at:at + 8].rstrip(b'\0').decode('latin-1')
            virtual_size, address, raw_size, raw_at = struct.unpack_from('<4I', data, at + 8)
            self.sections.append((name, base + address, max(virtual_size, raw_size), raw_size, raw_at))

    def section_of(self, address):
        for name, start, size, _raw_size, _raw_at in self.sections:
            if start <= address < start + size:
                return name
        return None

    def offset(self, address):
        for _name, start, _size, raw_size, raw_at in self.sections:
            if start <= address < start + raw_size:
                return address - start + raw_at
        return None

    def text(self, address):
        """The printable ASCII string at an address, or None."""
        at = self.offset(address)
        if at is None:
            return None
        end = self.data.find(b'\0', at)
        if end < 0:
            return None
        raw = self.data[at:end]
        if any(byte < 32 or byte > 126 for byte in raw):
            return None
        return raw.decode('ascii')

    def words(self, address, count):
        at = self.offset(address)
        if at is None or at + 4 * count > len(self.data):
            return None
        return struct.unpack_from(f'<{count}I', self.data, at)


def default_value(kind, text, vector, number):
    """A property's default as an object list value, or None if it cannot be written."""
    as_float = struct.unpack('<f', struct.pack('<I', number))[0]
    if kind in O.TEXT_TYPES:
        return text or ''
    if kind in (O.VECTOR, O.COLOR):
        return tuple(vector)
    if kind == O.FLOAT:
        return number
    if kind == O.BOOL:
        return 1 if as_float != 0 else 0
    if kind == O.LONG:
        if not math.isfinite(as_float) or not 0 <= math.trunc(as_float) <= 0xFFFFFFFF:
            return None
        return math.trunc(as_float)
    if kind == O.ROTATION:
        return (0, 0, 0, 0x3F800000)   # no rotation
    return None


def load(dll, what='GameServer.dll'):
    """Returns {class name: [(property name, type, default value), ...]}, parents' first."""
    image = _Image(dll, what)
    data_section = next((s for s in image.sections if s[0] == '.data'), None)
    if data_section is None:
        raise WorldError(f'{what}: no .data section')
    _name, start, _size, raw_size, raw_at = data_section
    word_count = min(raw_size, len(dll) - raw_at) // 4
    words = struct.unpack_from(f'<{word_count}I', dll, raw_at)
    found = {}   # address -> (name, parent address, own properties)
    for index in range(word_count - 12):
        name_at, empty_at, parent = words[index:index + 3]
        if image.section_of(name_at) != '.rdata':
            continue
        name = image.text(name_at)
        if not name or not _NAME_CHARS.issuperset(name.encode('ascii')):
            continue
        if parent and image.section_of(parent) != '.data':
            continue
        if not empty_at or image.text(empty_at) != '':
            continue
        property_count, properties_at = words[index + 8], words[index + 9]
        if property_count > MOST_PROPERTIES or (property_count and image.section_of(properties_at) != '.data'):
            continue
        properties = []
        for number in range(property_count):
            record = image.words(properties_at + 44 * number, 11)
            prop_name = image.text(record[0]) if record else None
            if prop_name is None:
                properties = None
                break
            text = image.text(record[1]) if record[1] else None
            properties.append((prop_name, record[2], default_value(record[2], text, record[3:6], record[6])))
        if properties is not None:
            found[start + 4 * index] = (name, parent, properties)
    classes = {}
    for address, (name, _parent, _own) in found.items():
        chain, seen = [], set()
        while address and address in found and address not in seen:
            seen.add(address)
            chain.append(found[address][2])
            address = found[address][1]
        classes[name] = [prop for own in reversed(chain) for prop in own]
    if not classes:
        raise WorldError(f'{what}: no object classes found; is this the PC game\'s GameServer.dll?')
    return classes
