"""The PC game's files: its archives and an index of what it has.

A PC archive (.Arch00) is "LTAR" version 3, little-endian:
  header 0x30   magic, version, name table size, directory count, file count, 12 unused
                bytes, 16-byte hash
  name table    NUL-terminated names
  file table    32 bytes each: name offset, u64 data offset, u64 stored size, u64 real size,
                u32 compressed flag (zlib)
  dir table     16 bytes each: name offset, first child, next sibling, file count
Files belong to directories in table order. Default.archcfg in the game folder lists the
archives in mount order; a file in a later archive replaces the same path in an earlier one.
"""
import os
import struct
import zlib

from . import WorldError

# Names the 360 gives a resource and the name the same resource has on PC.
PC_EXTENSION = {'tex': 'dds', 'mat': 'Mat00', 'mdl': 'Model00p', 'txanm': 'TxAnm00', 'vid': 'bik'}
# Resources that exist only as console builds (shaders, wave banks, lip sync, baked meshes).
CONSOLE_ONLY = {'fxo', 'gcmfx', 'mesh', 'xwb', 'lip'}


class Archive:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.entries = []  # (path, offset, stored size, real size, compressed)
        with open(path, 'rb') as file:
            head = file.read(0x30)
            if len(head) < 0x30 or head[:4] != b'LTAR':
                raise WorldError(f'{path}: not a PC archive (no LTAR magic)')
            version, names_size, dir_count, file_count = struct.unpack_from('<4I', head, 4)
            if version != 3:
                raise WorldError(f'{path}: archive version {version}, expected 3')
            tables = file.read(names_size + 32 * file_count + 16 * dir_count)
        if len(tables) != names_size + 32 * file_count + 16 * dir_count:
            raise WorldError(f'{path}: archive tables are cut short')
        names = tables[:names_size]

        def name_at(offset):
            end = names.find(b'\0', offset)
            if offset >= names_size or end < 0:
                raise WorldError(f'{path}: bad name offset in the archive tables')
            return names[offset:end].decode('latin-1')

        next_file = 0
        dirs_at = names_size + 32 * file_count
        for index in range(dir_count):
            dir_name_at, _child, _sibling, files_here = struct.unpack_from('<4I', tables, dirs_at + 16 * index)
            folder = name_at(dir_name_at)
            if next_file + files_here > file_count:
                raise WorldError(f'{path}: directories list more files than the archive has')
            for _ in range(files_here):
                name_offset, offset, stored, real, packed = struct.unpack_from(
                    '<IQQQI', tables, names_size + 32 * next_file)
                next_file += 1
                name = name_at(name_offset)
                self.entries.append((f'{folder}\\{name}' if folder else name, offset, stored, real, bool(packed)))

    def read(self, entry):
        path, offset, stored, real, packed = entry
        with open(self.path, 'rb') as file:
            file.seek(offset)
            data = file.read(stored)
        if len(data) != stored:
            raise WorldError(f'{self.name}: {path} is cut short')
        if packed:
            data = zlib.decompress(data)
        if len(data) != real:
            raise WorldError(f'{self.name}: {path} unpacks to {len(data)} bytes, expected {real}')
        return data


def mount_order(game_dir):
    """Archive paths in the order the game mounts them."""
    config = os.path.join(game_dir, 'Default.archcfg')
    on_disk = {name.lower(): name for name in os.listdir(game_dir)}
    order = []
    if os.path.isfile(config):
        with open(config, encoding='latin-1') as file:
            for line in file:
                name = line.strip()
                if name.lower().endswith('.arch00') and name.lower() in on_disk:
                    order.append(os.path.join(game_dir, on_disk[name.lower()]))
    if not order:
        raise WorldError(f'{game_dir}: no archives listed in Default.archcfg were found; '
                         'this should be the folder that holds FEAR.exe and FEAR.Arch00')
    return order


class PcFiles:
    """What the PC game has, by case-insensitive path, with the PC's own spelling."""

    def __init__(self, game_dir, extra_dirs=(), fx_prefix=True):
        self.fx_prefix = fx_prefix   # see resolve
        self.spelling = {}   # lower-case path -> path as the PC spells it
        self.source = {}     # lower-case path -> (Archive, entry), or (None, path) for a loose file
        self.archives = [Archive(path) for path in mount_order(game_dir)]
        for archive in self.archives:
            for entry in archive.entries:
                key = entry[0].lower()
                self.spelling[key] = entry[0]
                self.source[key] = (archive, entry)
        # Loose folders mounted after the archives (a mod's folder, or our own extra files).
        for folder in extra_dirs:
            if not os.path.isdir(folder):
                raise WorldError(f'{folder}: not a folder')
            for base, _dirs, names in os.walk(folder):
                for name in names:
                    relative = os.path.relpath(os.path.join(base, name), folder).replace('/', '\\')
                    self.spelling[relative.lower()] = relative
                    self.source[relative.lower()] = (None, os.path.join(base, name))

    def read(self, path):
        hit = self.source.get(path.replace('/', '\\').lower())
        if not hit:
            raise WorldError(f'{path}: not in the PC game\'s archives')
        if hit[0] is None:
            with open(hit[1], 'rb') as file:
                return file.read()
        return hit[0].read(hit[1])

    def find(self, path):
        return self.spelling.get(path.replace('/', '\\').lower())

    def resolve(self, console_path):
        """The PC file a console resource name means, in the PC's spelling, or None."""
        path = console_path.replace('/', '\\')
        stem, dot, extension = path.rpartition('.')
        if dot:
            first = stem + '.' + PC_EXTENSION.get(extension.lower(), extension)
        else:
            first = path
        candidates = [first]
        # The 360 kept some work-in-progress content under Prototype\; the PC has it at the root.
        if path.lower().startswith('prototype\\'):
            candidates.append(first[len('prototype\\'):])
        # Likewise its effects content under Prototype\FX\ (sounds, textures, videos).
        if self.fx_prefix and path.lower().startswith('prototype\\fx\\'):
            candidates.append(first[len('prototype\\fx\\'):])
        # Compiled console models have a PC build under the same name.
        if dot and extension.lower() == 'model00c':
            candidates.append(stem + '.Model00p')
        for candidate in candidates:
            hit = self.find(candidate)
            if hit:
                return hit
        return None


def extension_of(name):
    return name.rpartition('.')[2].lower() if '.' in name else ''
