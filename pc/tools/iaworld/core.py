"""The 360 .wld: collision and physics, the precache list and the blind data.

File: magic "PDLW", version 0x71, then (all big-endian unless noted)

  world box (6 floats) and world offset (3 floats)
  world tree: box, two words, then a quadtree as a bit stream (one bit per node, set = it
    has four children), byte for byte the same on both machines
  eight counts; the PC stores each XORed with COUNT_KEY, the 360 stores them plain. The
    first four are: surface names, bytes of surface name text, plane normals, world models
  surface name text, a pair of words per name, the normals
  world models: header, one byte per polygon giving its vertex count, the polygons, BSP
    nodes, vertices
  blockers: count, total vertices, then polygons
  physics shapes: seven words each, then a shape tree (see _shape). Mesh shapes end in
    Havok MOPP code, which is little-endian bytes on the 360 too and is copied as it is
  precache list: every resource the world wants loaded, as (folder, file) name pairs. The
    360 header has 12 words and the PC's 11, and the names differ, so it is rebuilt
  particle blockers, then one closing word
  blind data, to the end of the file: count, blob size, blob, then per entry (size, type,
    offset in the blob). Three types occur: keyframer paths, breakable geometry and the AI
    navigation mesh. Each is words except for the text it carries.

The PC .World00p holds the same things in the same order, little-endian: this file's
content up to the closing word is its first section and the blind data its last.
"""
import struct

from . import WorldError
from .binio import Copier, swap32

VERSION = 0x71
# What the PC XORs the eight counts with: the version plus the bytes of "FEAR".
COUNT_KEY = VERSION + sum(b'FEAR')

BLIND_KEYFRAMER = 0x6AAF0884
BLIND_BREAKABLE = 0x00000A85
BLIND_NAVMESH = 0x83F47C31
NAVMESH_VERSION = 6
# Words per element of the navigation mesh's twelve counted arrays, in file order.
NAVMESH_ARRAYS = (18, 1, 1, 3, 18, 1, 7, 4, 1, 3, 3, 2)


def _tree(c):
    c.skip32(6)
    c.skip32(2)
    # Walk the bit stream only to find where it ends, then copy it whole.
    data, start = c.data, c.pos
    pos, bit, open_nodes = start, 8, 1
    current = 0
    while open_nodes:
        if bit == 8:
            if pos >= len(data):
                raise WorldError(f'{c.what}: the world tree runs off the end of the file')
            current = data[pos]
            pos += 1
            bit = 0
        open_nodes -= 1
        if (current >> bit) & 1:
            open_nodes += 4
        bit += 1
    c.bytes(pos - start)


def _world_model(c):
    c.u32()                      # flags
    vertex_count = c.count(12)
    polygon_count = c.count(1)
    c.u32()                      # total polygon indices
    node_count = c.count(12)
    c.skip32(7)                  # box, root node
    sizes = c.bytes(polygon_count)
    for size in sizes:
        c.bytes(2)               # two byte fields
        c.skip16(1)              # surface flags
        c.skip32(2 + size)       # plane, distance, vertex indices
    c.skip32(3 * node_count)
    c.skip32(3 * vertex_count)


def _polygon_list(c):
    """Blockers and particle blockers: four words and a counted list of points each."""
    c.skip32(4)
    c.skip32(3 * c.count(12))


def _shape(c, depth=0):
    """One physics shape; compound kinds contain further shapes."""
    if depth > 64:
        raise WorldError(f'{c.what}: physics shapes nest too deeply')
    kind = c.u32()
    if kind == 1:                # triangle mesh
        c.skip32(16)
        vertex_count = c.count(12)
        index_count = c.count(2)
        c.skip32(3 * vertex_count)
        if vertex_count < 0x10000:
            c.skip16(index_count)
        else:
            c.skip32(index_count)
        # Havok MOPP: a 20-byte header whose last word is the code size, little-endian.
        c.need(20)
        code_size = struct.unpack_from('<I', c.data, c.pos + 16)[0]
        c.bytes(20 + code_size)
    elif kind == 2:
        c.skip32(6)
    elif kind == 3:
        c.skip32(3)
    elif kind == 4:              # convex hull: points, then planes
        c.skip32(16)
        vertex_count = c.count(12)
        plane_count = c.count(16)
        c.skip32(3 * vertex_count + 4 * plane_count)
    elif kind == 5:              # a transformed shape
        c.skip32(7)
        _shape(c, depth + 1)
    elif kind == 6:              # a list of shapes
        for _ in range(c.count(4)):
            _shape(c, depth + 1)
    elif kind == 7:
        c.skip32(9)
    elif kind == 8:              # a pair of shapes
        _shape(c, depth + 1)
        _shape(c, depth + 1)
    # 0 and anything above 8: no body.


def _precache_pairs(c, header_words):
    """Reads (without copying) a precache list; returns its (folder, file) pairs."""
    header = c.words(header_words)
    if header[3] or header[5] or header[8]:
        raise WorldError(f'{c.what}: the precache list has streaming tables, which this tool does not convert')
    if header[0] > len(c.data):
        raise WorldError(f'{c.what}: precache list size is larger than the file')
    blob = c.take(header[0])
    pair_count = c.read_only_u32()
    if 8 * pair_count > len(c.data) - c.pos:
        raise WorldError(f'{c.what}: precache list is larger than the file')
    offsets = c.words(2 * pair_count)

    def text(offset):
        end = blob.find(b'\0', offset)
        if offset >= len(blob) or end < 0:
            raise WorldError(f'{c.what}: bad name offset in the precache list')
        return blob[offset:end].decode('latin-1')

    return [(text(offsets[i]), text(offsets[i + 1])) for i in range(0, len(offsets), 2)]


def pc_precache(pairs):
    """The PC precache list for (folder, file) pairs."""
    blob = bytearray()
    placed = {}
    table = []
    for pair in pairs:
        for text in pair:
            if text not in placed:
                placed[text] = len(blob)
                raw = text.encode('latin-1') + b'\0'
                blob += raw + b'\0' * (-len(raw) % 4)
            table.append(placed[text])
    header = (len(blob), 0, 0, 0, len(pairs), 0, 0, 0, 0, 0xFFFFFFFF, 0)
    return (struct.pack('<11I', *header) + blob + struct.pack('<I', len(pairs))
            + struct.pack(f'<{len(table)}I', *table))


def _to_precache(c, pc_source):
    """Walks from the world box to the start of the precache list."""
    c.skip32(9)
    _tree(c)
    counts = []
    for _ in range(8):
        value = c.read_only_u32()
        if pc_source:
            value ^= COUNT_KEY
        counts.append(value)
        if c.write:
            c.out += struct.pack('<I', value ^ COUNT_KEY)
    name_count, name_bytes, normal_count, model_count = counts[:4]
    if name_bytes + 8 * name_count + 12 * normal_count > len(c.data):
        raise WorldError(f'{c.what}: surface tables are larger than the file')
    c.bytes(name_bytes)
    c.skip32(2 * name_count)
    c.skip32(3 * normal_count)
    for _ in range(model_count):
        _world_model(c)
    blocker_count = c.count(20)
    c.u32()                      # total blocker vertices
    for _ in range(blocker_count):
        _polygon_list(c)
    for _ in range(c.count(32)):
        c.skip32(7)
        _shape(c)


def read_pc_precache(world, what='PC world'):
    """A PC world's precache list: ((folder, file) pairs, start offset, end offset)."""
    c = Copier(world, 20, what, big=False, write=False)
    _to_precache(c, True)
    start = c.pos
    pairs = _precache_pairs(c, 11)
    return pairs, start, c.pos


def convert_core(wld, make_precache, what='.wld'):
    """Returns (PC core section, offset of the blind data in the .wld).

    make_precache(pairs) gets the 360's precache pairs and returns the PC list's bytes.
    """
    if len(wld) < 8 or wld[:4] != b'PDLW':
        raise WorldError(f'{what}: not a 360 world (no PDLW magic)')
    version = struct.unpack_from('>I', wld, 4)[0]
    if version != VERSION:
        raise WorldError(f'{what}: version 0x{version:X}, expected 0x{VERSION:X}')
    c = Copier(wld, 8, what)
    _to_precache(c, False)
    c.out += make_precache(_precache_pairs(c, 12))
    for _ in range(c.count(20)):
        _polygon_list(c)
    c.u32()                      # closing word
    return bytes(c.out), c.pos


def _navmesh_text(segment, what):
    """The (start, end) of the node name text inside a navigation mesh."""
    if len(segment) < 12 or struct.unpack_from('>I', segment, 4)[0] != NAVMESH_VERSION:
        raise WorldError(f'{what}: navigation mesh is not version {NAVMESH_VERSION}')
    pos = 12
    for words in NAVMESH_ARRAYS:
        if pos + 4 > len(segment):
            raise WorldError(f'{what}: navigation mesh is cut short')
        pos += 4 + 4 * words * struct.unpack_from('>I', segment, pos)[0]
    if pos + 4 > len(segment):
        raise WorldError(f'{what}: navigation mesh is cut short')
    start = pos + 4
    end = start + struct.unpack_from('>I', segment, pos)[0]
    if end > len(segment) or end % 4 or len(segment) % 4:
        raise WorldError(f'{what}: navigation mesh name text is misplaced')
    return start, end


def convert_blind(wld, pos, what='.wld'):
    """The PC blind data section from the .wld's blind data at `pos`."""
    if pos + 8 > len(wld):
        raise WorldError(f'{what}: no blind data')
    count, blob_size = struct.unpack_from('>2I', wld, pos)
    table_at = pos + 8 + blob_size
    if table_at + 12 * count != len(wld):
        raise WorldError(f'{what}: the blind data does not end at the end of the file')
    blob = bytearray(wld[pos + 8:table_at])
    table = struct.unpack_from(f'>{3 * count}I', wld, table_at)
    kinds = {}
    for index in range(count):
        size, kind, offset = table[3 * index:3 * index + 3]
        if offset + size > blob_size:
            raise WorldError(f'{what}: a blind data entry lies outside the blob')
        segment = bytes(blob[offset:offset + size])
        kinds[kind] = kinds.get(kind, 0) + 1
        if kind == BLIND_KEYFRAMER:
            # Version, key count, end of keys; nine words per key; then text.
            if size < 12:
                raise WorldError(f'{what}: a keyframer path is cut short')
            version, keys, keys_end = struct.unpack_from('>3I', segment, 0)
            if version != 1 or keys_end != 12 + 36 * keys or keys_end > size:
                raise WorldError(f'{what}: unexpected keyframer path header')
            spans = [(0, keys_end)]
        elif kind == BLIND_BREAKABLE:
            # Count; per piece ten words (the last a point count) and five floats a point.
            if size < 4:
                raise WorldError(f'{what}: breakable data is cut short')
            at = 4
            for _ in range(struct.unpack_from('>I', segment, 0)[0]):
                if at + 40 > size:
                    raise WorldError(f'{what}: breakable data is cut short')
                at += 40 + 20 * struct.unpack_from('>I', segment, at + 36)[0]
            if at != size:
                raise WorldError(f'{what}: breakable data has an unexpected size')
            spans = [(0, size)]
        elif kind == BLIND_NAVMESH:
            text_start, text_end = _navmesh_text(segment, what)
            spans = [(0, text_start), (text_end, size)]
        else:
            raise WorldError(f'{what}: blind data of unknown type 0x{kind:08X}')
        for start, end in spans:
            blob[offset + start:offset + end] = swap32(segment[start:end])
    out = struct.pack('<2I', count, blob_size) + bytes(blob) + struct.pack(f'<{len(table)}I', *table)
    return out, kinds
