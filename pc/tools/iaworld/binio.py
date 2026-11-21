"""Byte order helpers.

The 360 files are big-endian and the PC ones little-endian, and most of a world is plain
arrays of 32-bit words (ints and floats alike). So most of the conversion is "copy this
many words, reversing each", which Copier does while the caller walks the structure.
Floats are never decoded on that path, so nothing is rounded.
"""
import struct

from . import WorldError


def swap32(data):
    """Reverses the bytes of every 32-bit word."""
    if len(data) % 4:
        raise WorldError('a block of 32-bit words has a length that is not a multiple of 4')
    out = bytearray(len(data))
    out[0::4] = data[3::4]
    out[1::4] = data[2::4]
    out[2::4] = data[1::4]
    out[3::4] = data[0::4]
    return out


def swap16(data):
    """Reverses the bytes of every 16-bit word."""
    if len(data) % 2:
        raise WorldError('a block of 16-bit words has an odd length')
    out = bytearray(len(data))
    out[0::2] = data[1::2]
    out[1::2] = data[0::2]
    return out


class Reader:
    """A cursor over bytes in one byte order. Reads past the end raise WorldError."""

    def __init__(self, data, pos=0, big=True, what='data'):
        self.data = data
        self.pos = pos
        self.big = big
        self.what = what
        self._u32 = struct.Struct('>I' if big else '<I')
        self._u16 = struct.Struct('>H' if big else '<H')

    def need(self, size):
        if size < 0 or self.pos < 0 or self.pos + size > len(self.data):
            raise WorldError(f'{self.what}: ends early at 0x{self.pos:X} (wanted {size} more bytes)')

    def u32(self):
        self.need(4)
        value = self._u32.unpack_from(self.data, self.pos)[0]
        self.pos += 4
        return value

    def u16(self):
        self.need(2)
        value = self._u16.unpack_from(self.data, self.pos)[0]
        self.pos += 2
        return value

    def u8(self):
        self.need(1)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def words(self, count):
        self.need(4 * count)
        values = struct.unpack_from(('>' if self.big else '<') + f'{count}I', self.data, self.pos)
        self.pos += 4 * count
        return values

    def take(self, size):
        self.need(size)
        chunk = self.data[self.pos:self.pos + size]
        self.pos += size
        return chunk

    def peek32(self, ahead=0):
        self.need(ahead + 4)
        return self._u32.unpack_from(self.data, self.pos + ahead)[0]

    def count(self, unit=1):
        """A 32-bit element count; refuses one the rest of the data could not hold."""
        value = self.u32()
        if value * unit > len(self.data) - self.pos:
            raise WorldError(f'{self.what}: count {value} at 0x{self.pos - 4:X} is larger than the file')
        return value


class Copier(Reader):
    """A Reader that also writes what it reads, little-endian.

    With write off it only walks, which is how a structure in a PC file is measured.
    """

    def __init__(self, data, pos=0, what='data', big=True, write=True):
        super().__init__(data, pos, big, what)
        self.write = write
        self.out = bytearray()

    def u32(self):
        value = super().u32()
        if self.write:
            self.out += struct.pack('<I', value)
        return value

    def u16(self):
        value = super().u16()
        if self.write:
            self.out += struct.pack('<H', value)
        return value

    def skip32(self, count):
        """Copies `count` words without looking at them."""
        if count:
            chunk = self.take(4 * count)
            if self.write:
                self.out += swap32(chunk) if self.big else chunk

    def skip16(self, count):
        if count:
            chunk = self.take(2 * count)
            if self.write:
                self.out += swap16(chunk) if self.big else chunk

    def bytes(self, size):
        """Copies bytes that have no byte order (text, bit streams, Havok data)."""
        chunk = self.take(size)
        if self.write:
            self.out += chunk
        return chunk

    def read_only_u32(self):
        """Reads a word without writing it, for the caller to write in another form."""
        return Reader.u32(self)
