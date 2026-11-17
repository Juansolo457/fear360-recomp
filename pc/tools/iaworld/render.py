"""The 360 .wldclnt: what the renderer draws, and the client's sector data.

File: magic "CDLW", version 0x71, then the render data and, after it, the sector data.
A PC .World00p has the same two things as its second and third sections.

Render data
  nine header words (word 0: number of block groups, word 3: number of meshes), one more
  word, then the meshes and the block groups.
  mesh:   a zero word, two header words (the second is the material count), vertex buffer
          size, index buffer size, the vertex buffer, the index buffer (16-bit), the vertex
          declarations (each a byte size and Direct3D 9 vertex elements), the sections, the
          material names (16-bit length, no terminator).
  section: nine words (first vertex, vertex count, vertex size, first index, base vertex,
          triangle count, material, size of the extra bytes, declaration) and the extra bytes.
  block:  two counts, records of nine words, then outlines (a byte count and points).

What differs on the 360
  Everything is big-endian. Normals, tangents and binormals are one packed word (declaration
  type 14: three signed 10-bit fields, 511 = 1.0) where the PC has three floats, so the 360
  vertex is smaller and every vertex is rebuilt. Materials are named .mat, not .Mat00.
  And a PC material can read vertex inputs (a vertex colour, a tangent frame) that the 360
  build of the same material did not need. The PC refuses to draw a section whose vertices
  lack an input its material reads, so the missing ones are made up here: white for the
  colour, and a normal, tangent and binormal worked out from the triangles.

Sector data: all 16- and 32-bit values apart from the sector names.
"""
import math
import struct

from . import WorldError
from .binio import Copier, Reader, swap16

VERSION = 0x71
END_OF_DECLARATION = 0xFF            # stream number of the closing element
PACKED_NORMAL = 14                   # 360-only element type
FLOAT2, FLOAT3, COLOUR = 1, 2, 4     # Direct3D 9 element types
ELEMENT_SIZE = {0: 4, 1: 8, 2: 12, 3: 16, 4: 4, PACKED_NORMAL: 4}
# Direct3D 9 usages, as (usage, index).
POSITION, NORMAL, TEXCOORD, TANGENT, BINORMAL, VERTEX_COLOUR = (0, 0), (3, 0), (5, 0), (6, 0), (7, 0), (10, 0)

# Section record fields.
FIRST_VERTEX, VERTEX_COUNT, VERTEX_SIZE, FIRST_INDEX, BASE_VERTEX, TRIANGLES, MATERIAL, EXTRA_SIZE, DECLARATION = range(9)

# Limits of the PC loader's section and declaration records.
MAX_DECLARATION_BYTES = 0x200
MAX_EXTRA_BYTES = 0x18

# One packed 10-bit field to its value.
_TEN_BIT = [(v - 1024 if v & 512 else v) / 511 for v in range(1024)]


class Mesh:
    __slots__ = ('header', 'material_count', 'vertices', 'indices', 'declarations', 'sections', 'extras', 'materials')


def _elements(raw, big):
    """Vertex elements as (stream, offset, type, method, usage, usage index)."""
    layout = struct.Struct('>HHBBBB' if big else '<HHBBBB')
    return [layout.unpack_from(raw, at) for at in range(0, len(raw) - 7, 8)]


def read_render(data, pos, big, what):
    """Returns (header words, meshes, block groups, offset after the render data)."""
    r = Reader(data, pos, big, what)
    header = r.words(10)
    meshes = []
    if header[3] > len(data) or header[0] > len(data):
        raise WorldError(f'{what}: render header counts are larger than the file')
    for _ in range(header[3]):
        if r.u32() != 0:
            raise WorldError(f'{what}: a render mesh of a kind this tool does not convert')
        mesh = Mesh()
        mesh.header, mesh.material_count, vertex_bytes, index_bytes = r.words(4)
        mesh.vertices = r.take(vertex_bytes)
        mesh.indices = r.take(index_bytes)
        mesh.declarations = [_elements(r.take(r.count()), big) for _ in range(r.count(4))]
        mesh.sections, mesh.extras = [], []
        for _ in range(r.count(36)):
            section = r.words(9)
            if section[EXTRA_SIZE] > MAX_EXTRA_BYTES:
                raise WorldError(f'{what}: a section has {section[EXTRA_SIZE]} extra bytes')
            mesh.sections.append(section)
            mesh.extras.append(r.take(section[EXTRA_SIZE]))
        if mesh.material_count * 2 > len(data):
            raise WorldError(f'{what}: material count is larger than the file')
        mesh.materials = [r.take(r.u16()).decode('latin-1') for _ in range(mesh.material_count)]
        meshes.append(mesh)
    groups = []
    for _ in range(header[0]):
        group = []
        for _ in range(r.count(8)):
            record_count, outline_count = r.count(36), r.count(1)
            records = r.words(9 * record_count)
            outlines = []
            for _ in range(outline_count):
                outlines.append(r.words(3 * r.u8()))
            group.append((records, outlines))
        groups.append(group)
    return header, meshes, groups, r.pos


def material_inputs(pc_world, what='PC world'):
    """For each material a PC world uses (lower case): the vertex inputs its sections have."""
    if len(pc_world) < 20 or struct.unpack_from('<I', pc_world, 0)[0] != VERSION:
        raise WorldError(f'{what}: not a version 0x71 PC world')
    _header, meshes, _groups, _end = read_render(pc_world, struct.unpack_from('<I', pc_world, 4)[0], False, what)
    inputs = {}
    for mesh in meshes:
        for section in mesh.sections:
            if section[DECLARATION] >= len(mesh.declarations) or section[MATERIAL] >= len(mesh.materials):
                raise WorldError(f'{what}: a section names a declaration or material that is not there')
            wanted = inputs.setdefault(mesh.materials[section[MATERIAL]].lower(), set())
            for stream, _offset, _kind, _method, usage, index in mesh.declarations[section[DECLARATION]]:
                if stream != END_OF_DECLARATION:
                    wanted.add((usage, index))
    return inputs


def pc_material_name(name):
    return name[:-4] + '.Mat00' if name.lower().endswith('.mat') else name


def _pc_declaration(elements, what):
    """The PC form of a 360 declaration: packed normals widened, offsets recomputed."""
    out, offset = [], 0
    for stream, _old_offset, kind, method, usage, index in elements:
        if stream == END_OF_DECLARATION:
            out.append((stream, 0, kind, method, usage, index))
            continue
        if kind not in ELEMENT_SIZE:
            raise WorldError(f'{what}: vertex element type {kind} is not handled')
        if kind == PACKED_NORMAL:
            kind = FLOAT3
        out.append((stream, offset, kind, method, usage, index))
        offset += ELEMENT_SIZE[kind]
    return out, offset


def _pc_vertices(source, elements, size, count, what):
    """Rebuilds `count` 360 vertices in the PC layout. Returns (bytes, declaration, vertex size)."""
    declaration, pc_size = _pc_declaration(elements, what)
    out = bytearray(pc_size * count)
    if not count:
        return out, declaration, pc_size
    for (stream, offset, kind, *_), (_s, pc_offset, *_rest) in zip(elements, declaration):
        if stream == END_OF_DECLARATION:
            continue
        if offset + ELEMENT_SIZE[kind] > size:
            raise WorldError(f'{what}: a vertex element lies outside its vertex')
        if kind == PACKED_NORMAL:
            column = bytearray(4 * count)
            for byte in range(4):
                column[byte::4] = source[offset + byte::size]
            floats = []
            for word in struct.unpack(f'>{count}I', column):
                floats += (_TEN_BIT[word & 1023], _TEN_BIT[(word >> 10) & 1023], _TEN_BIT[(word >> 20) & 1023])
            packed = struct.pack(f'<{3 * count}f', *floats)
            for byte in range(12):
                out[pc_offset + byte::pc_size] = packed[byte::12]
        else:
            # Floats and colours: whole words, reversed.
            for word in range(0, ELEMENT_SIZE[kind], 4):
                for byte in range(4):
                    out[pc_offset + word + byte::pc_size] = source[offset + word + 3 - byte::size]
    return out, declaration, pc_size


def _unit(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if not length > 1e-12:
        return None
    return (v[0] / length, v[1] / length, v[2] / length)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    """A dot product summed with error compensation (Neumaier), so its last bit is steady."""
    total = 0.0 + a[0] * b[0]
    lost = 0.0
    for term in (a[1] * b[1], a[2] * b[2]):
        new = total + term
        if abs(total) >= abs(term):
            lost += (total - new) + term
        else:
            lost += (term - new) + total
        total = new
    if lost and math.isfinite(lost):
        total += lost
    return total


def _tangent_frames(positions, uvs, normals, triangles, count, what):
    """A normal, tangent and binormal for each vertex, from the triangles that use it.

    Normals given by the vertex data are kept. Otherwise a normal is the sum of the face
    normals (so larger faces count for more). Tangent and binormal follow the texture
    coordinates across each triangle and are then squared up against the normal.
    """
    zero = (0.0, 0.0, 0.0)
    sum_n = [list(zero) for _ in range(count)]
    sum_t = [list(zero) for _ in range(count)]
    sum_b = [list(zero) for _ in range(count)]
    for a, b, c in triangles:
        if a >= count or b >= count or c >= count:
            raise WorldError(f'{what}: a triangle uses a vertex outside its block')
        pa, pb, pc = positions[a], positions[b], positions[c]
        e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        face = _cross(e1, e2)
        for vertex in (a, b, c):
            acc = sum_n[vertex]
            acc[0] += face[0]
            acc[1] += face[1]
            acc[2] += face[2]
        if uvs:
            ua, ub, uc = uvs[a], uvs[b], uvs[c]
            du1, dv1 = ub[0] - ua[0], ub[1] - ua[1]
            du2, dv2 = uc[0] - ua[0], uc[1] - ua[1]
            area = du1 * dv2 - du2 * dv1
            if abs(area) > 1e-12:
                tangent = [(e1[i] * dv2 - e2[i] * dv1) / area for i in range(3)]
                binormal = [(e2[i] * du1 - e1[i] * du2) / area for i in range(3)]
                for vertex in (a, b, c):
                    acc = sum_t[vertex]
                    acc[0] += tangent[0]
                    acc[1] += tangent[1]
                    acc[2] += tangent[2]
                    acc = sum_b[vertex]
                    acc[0] += binormal[0]
                    acc[1] += binormal[1]
                    acc[2] += binormal[2]
    if normals is None:
        normals = [_unit(n) or (0.0, 0.0, 1.0) for n in sum_n]
    tangents, binormals = [], []
    for k in range(count):
        n, t, b = normals[k], sum_t[k], sum_b[k]
        along = _dot(t, n)
        tangent = _unit((t[0] - n[0] * along, t[1] - n[1] * along, t[2] - n[2] * along))
        if tangent is None:
            # No texture gradient to follow: any direction square to the normal will do.
            axis = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
            tangent = _unit(_cross(axis, n))
            if tangent is None:
                raise WorldError(f'{what}: cannot make a tangent for a vertex')
        along_n, along_t = _dot(b, n), _dot(b, tangent)
        binormal = _unit(tuple(b[i] - n[i] * along_n - tangent[i] * along_t for i in range(3)))
        if binormal is None:
            binormal = _unit(_cross(n, tangent))
            if binormal is None:
                raise WorldError(f'{what}: cannot make a binormal for a vertex')
        tangents.append(tangent)
        binormals.append(binormal)
    return normals, tangents, binormals


def _column(vertices, size, offset, width, count):
    """`width` floats at `offset` of each vertex, as a list of tuples."""
    raw = bytearray(4 * width * count)
    for byte in range(4 * width):
        raw[byte::4 * width] = vertices[offset + byte::size]
    flat = struct.unpack(f'<{width * count}f', raw)
    return [flat[i:i + width] for i in range(0, len(flat), width)]


def _add_inputs(vertices, declaration, size, count, missing, triangles, what):
    """Appends the `missing` inputs to PC vertices. Returns (bytes, declaration, vertex size)."""
    where = {}
    for stream, offset, kind, _method, usage, index in declaration:
        if stream != END_OF_DECLARATION:
            where[(usage, index)] = offset
    added = sorted(missing)
    columns = {}
    if any(usage in (NORMAL, TANGENT, BINORMAL) for usage in added):
        if POSITION not in where:
            raise WorldError(f'{what}: a vertex block has no position')
        positions = _column(vertices, size, where[POSITION], 3, count)
        uvs = _column(vertices, size, where[TEXCOORD], 2, count) if TEXCOORD in where else None
        normals = _column(vertices, size, where[NORMAL], 3, count) if NORMAL in where else None
        frames = _tangent_frames(positions, uvs, normals, triangles, count, what)
        for usage, vectors in zip((NORMAL, TANGENT, BINORMAL), frames):
            if usage in added:
                columns[usage] = struct.pack(f'<{3 * count}f', *[x for v in vectors for x in v])
    body = [element for element in declaration if element[0] != END_OF_DECLARATION]
    closing = [element for element in declaration if element[0] == END_OF_DECLARATION]
    if not closing:
        raise WorldError(f'{what}: a vertex declaration has no closing element')
    new_size = size
    placed = []
    for usage in added:
        if usage == VERTEX_COLOUR:
            kind = COLOUR
        elif usage in (NORMAL, TANGENT, BINORMAL):
            kind = FLOAT3
        else:
            kind = FLOAT2        # e.g. a texture coordinate set the 360 vertex lacks: zeros
        body.append((0, new_size, kind, 0, usage[0], usage[1]))
        placed.append((usage, new_size, ELEMENT_SIZE[kind]))
        new_size += ELEMENT_SIZE[kind]
    out = bytearray(new_size * count)
    for byte in range(size):
        out[byte::new_size] = vertices[byte::size]
    for usage, offset, width in placed:
        if usage == VERTEX_COLOUR:
            data = b'\xFF' * (4 * count)
        elif usage in columns:
            data = columns[usage]
        else:
            continue
        for byte in range(width):
            out[offset + byte::new_size] = data[byte::width]
    return out, body + closing[:1], new_size


def _convert_mesh(mesh, wanted_inputs, what, notes):
    materials = [pc_material_name(name) for name in mesh.materials]
    indices = struct.unpack(f'>{len(mesh.indices) // 2}H', mesh.indices[:len(mesh.indices) // 2 * 2])
    # Vertices are shared by the sections with the same base vertex and declaration; such a
    # group is rebuilt as one block.
    blocks = {}
    for section in mesh.sections:
        if section[DECLARATION] >= len(mesh.declarations) or section[MATERIAL] >= len(materials):
            raise WorldError(f'{what}: a section names a declaration or material that is not there')
        first, last = section[FIRST_INDEX], min(section[FIRST_INDEX] + 3 * section[TRIANGLES], len(indices))
        if first < last:
            used = indices[first:last]
            lowest = section[FIRST_VERTEX] - section[BASE_VERTEX]
            if min(used) < lowest or max(used) >= lowest + section[VERTEX_COUNT]:
                raise WorldError(f'{what}: a section\'s indices leave its vertex range')
        key = (section[BASE_VERTEX], section[DECLARATION])
        block = blocks.setdefault(key, {'start': section[FIRST_VERTEX], 'end': 0, 'sections': [],
                                        'size': section[VERTEX_SIZE]})
        block['start'] = min(block['start'], section[FIRST_VERTEX])
        block['end'] = max(block['end'], section[FIRST_VERTEX] + section[VERTEX_COUNT])
        block['sections'].append(section)
    declarations = [_pc_declaration(elements, what)[0] for elements in mesh.declarations]
    buffer = bytearray()
    moved = {}
    previous_end = 0
    for key in sorted(blocks, key=lambda k: k[0] * blocks[k]['size']):   # in buffer order
        base, declaration_index = key
        block = blocks[key]
        size = block['size']
        if base * size < previous_end or block['start'] < base:
            raise WorldError(f'{what}: vertex blocks overlap')
        previous_end = block['end'] * size
        if previous_end > len(mesh.vertices):
            raise WorldError(f'{what}: a vertex block lies outside the vertex buffer')
        count = block['end'] - base
        vertices, declaration, pc_size = _pc_vertices(
            mesh.vertices[base * size:block['end'] * size], mesh.declarations[declaration_index], size, count, what)
        present = {(e[4], e[5]) for e in declaration if e[0] != END_OF_DECLARATION}
        missing = set()
        for section in block['sections']:
            missing |= wanted_inputs.get(materials[section[MATERIAL]].lower(), set()) - present
        if missing:
            triangles = []
            for section in block['sections']:
                first = section[FIRST_INDEX]
                last = first + 3 * section[TRIANGLES]
                if last > len(indices):
                    raise WorldError(f'{what}: a section\'s triangles run past the index buffer')
                triangles += zip(indices[first:last:3], indices[first + 1:last:3], indices[first + 2:last:3])
            vertices, declaration, pc_size = _add_inputs(vertices, declaration, pc_size, count, missing, triangles, what)
            if declaration in declarations:
                declaration_index = declarations.index(declaration)
            else:
                declarations.append(declaration)
                declaration_index = len(declarations) - 1
            notes['made_up_vertices'] += count
            for usage in missing:
                notes['made_up_inputs'][usage] = notes['made_up_inputs'].get(usage, 0) + count
        buffer += b'\0' * (-len(buffer) % pc_size)
        moved[key] = (len(buffer) // pc_size, pc_size, declaration_index)
        buffer += vertices
    out = bytearray(struct.pack('<5I', 0, mesh.header, mesh.material_count, len(buffer), len(mesh.indices)))
    out += buffer
    out += swap16(mesh.indices)
    out += struct.pack('<I', len(declarations))
    for declaration in declarations:
        raw = b''.join(struct.pack('<HHBBBB', *element) for element in declaration)
        if len(raw) > MAX_DECLARATION_BYTES:
            raise WorldError(f'{what}: a vertex declaration is too large for the PC loader')
        out += struct.pack('<I', len(raw)) + raw
    out += struct.pack('<I', len(mesh.sections))
    for section, extra in zip(mesh.sections, mesh.extras):
        base, pc_size, declaration_index = moved[(section[BASE_VERTEX], section[DECLARATION])]
        if pc_size > 0xFF or section[MATERIAL] > 0xFFFF:
            raise WorldError(f'{what}: a section is too large for the PC loader\'s record')
        out += struct.pack('<9I', base + section[FIRST_VERTEX] - section[BASE_VERTEX], section[VERTEX_COUNT],
                           pc_size, section[FIRST_INDEX], base, section[TRIANGLES], section[MATERIAL],
                           section[EXTRA_SIZE], declaration_index)
        out += extra
    for name in materials:
        raw = name.encode('latin-1')
        out += struct.pack('<H', len(raw)) + raw
    notes['vertex_bytes'] += len(buffer)
    notes['materials'].update(materials)
    return out


def _convert_sectors(data, pos, what):
    c = Copier(data, pos, what)
    header = c.words(8)
    c.pos = pos
    c.skip32(8)
    portal_count, sector_count, list_count = header[1], header[0], header[2]
    if (portal_count + sector_count + list_count) * 2 > len(data):
        raise WorldError(f'{what}: sector counts are larger than the file')
    for _ in range(portal_count):
        c.skip32(3 * c.u16() + 8)
    for _ in range(sector_count):
        c.bytes(c.u16())             # name
        c.skip32(6)
        c.skip32(4 * c.count(16))
        c.skip32(c.count(4))
        c.u32()
    for _ in range(list_count):
        c.skip32(c.count(4))
        c.skip32(4)
    if c.pos != len(data):
        raise WorldError(f'{what}: {len(data) - c.pos} bytes follow the sector data')
    return bytes(c.out)


def convert_render(clnt, wanted_inputs, what='.wldclnt'):
    """Returns (PC render section, PC sector section, notes)."""
    if len(clnt) < 8 or clnt[:4] != b'CDLW':
        raise WorldError(f'{what}: not a 360 client world (no CDLW magic)')
    version = struct.unpack_from('>I', clnt, 4)[0]
    if version != VERSION:
        raise WorldError(f'{what}: version 0x{version:X}, expected 0x{VERSION:X}')
    header, meshes, groups, end = read_render(clnt, 8, True, what)
    notes = {'meshes': len(meshes), 'vertex_bytes': 0, 'made_up_vertices': 0, 'made_up_inputs': {}, 'materials': set()}
    out = bytearray(struct.pack('<10I', *header))
    for mesh in meshes:
        out += _convert_mesh(mesh, wanted_inputs, what, notes)
    for group in groups:
        out += struct.pack('<I', len(group))
        for records, outlines in group:
            out += struct.pack('<2I', len(records) // 9, len(outlines))
            out += struct.pack(f'<{len(records)}I', *records)
            for points in outlines:
                out += bytes((len(points) // 3,)) + struct.pack(f'<{len(points)}I', *points)
    return bytes(out), _convert_sectors(clnt, end, what), notes
