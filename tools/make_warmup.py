"""Write a game's shader warm-up list (#53) from the renderer's prewarm files.

    python -I tools/make_warmup.py <game assets> <image.bin> <out.inc> <prewarm.bin>...

    F.E.A.R.:       python -I tools/make_warmup.py port-nx/assets private/image/default.bin
                        port-nx/warmup/shader_warmup.inc port-nx/cache/shaders/driver/prewarm-*.bin
    F.E.A.R. Files: python -I tools/make_warmup.py port-files/assets private/files/image/default.bin
                        port-files/warmup/shader_warmup.inc port-files/cache/shaders/driver/prewarm-*.bin

The list has no shader code. For each shader seen in play it says where the game keeps it: a
compiled effect in Game/uberCatalog.cat (offset in the unpacked .fxo) or the executable (offset in
the image, i.e. guest memory from 0x82000000). The game's D3D patches a vertex shader's vertex
fetch instructions for the vertex layout it is drawn with, so a vertex shader's entry also gives
the dwords that differ from the effect's copy (as XORs). At startup the renderer rebuilds each
shader from the player's own files and keeps it only if its hash matches (src/shader_warmup.cpp).
With the shaders go their translation variants and the pipelines (PipelineKey, as 32-bit words)
seen with them. Several prewarm files merge, e.g. from different PCs; record them with the default
depth settings and MSAA on (the renderer drops the sample count where MSAA is off).

The prewarm file (native_renderer.cpp, "Shader pre-warm"): magic, payload size, XXH3 of the payload;
the payload: version, count, then per shader its microcode hash, type, dword count, modification
count, a reserved word, the microcode (host order) and the modifications; from version 2 the
number of pipeline keys, the key size and the keys.
"""
import os
import struct
import sys
import zlib

MAGIC = 0x314D525750524546  # "FEPRWRM1"


def read_prewarm(path, shaders, keys):
    """Adds a prewarm file's shaders ({hash: [type, ucode, set(mods)]}) and keys; the key size."""
    data = open(path, 'rb').read()
    magic, size, _ = struct.unpack_from('<QQQ', data, 0)
    if magic != MAGIC:
        raise SystemExit(f'{path}: not a prewarm file')
    p = 24
    version, count = struct.unpack_from('<II', data, p)
    p += 8
    for _ in range(count):
        h, t, dwords, mod_count, _reserved = struct.unpack_from('<QIIII', data, p)
        p += 24
        ucode = struct.unpack_from(f'<{dwords}I', data, p)
        p += dwords * 4
        mods = struct.unpack_from(f'<{mod_count}Q', data, p)
        p += mod_count * 8
        entry = shaders.setdefault(h, [t, ucode, set()])
        entry[2].update(mods)
    key_size = 0
    if version >= 2:
        key_count, key_size = struct.unpack_from('<II', data, p)
        p += 8
        for _ in range(key_count):
            keys.add(data[p:p + key_size])
            p += key_size
    return key_size


def read_effects(assets):
    """The compiled effects (.fxo) in Game/uberCatalog.cat, unpacked: {name: bytes}."""
    path = os.path.join(assets, 'Game', 'uberCatalog.cat')
    effects = {}
    with open(path, 'rb') as f:
        # Header (big-endian): version, entry table offset, entry count, name table offset and
        # size. Entries: name offset, data offset, size, stored size (zlib when smaller).
        _version, table, count, names_at, names_size = struct.unpack('>5I', f.read(20))
        f.seek(table)
        entries = [struct.unpack('>4I', f.read(16)) for _ in range(count)]
        f.seek(names_at)
        names = f.read(names_size)
        for name_at, offset, size, stored in entries:
            name = names[name_at:names.index(b'\0', name_at)].decode('latin-1')
            if not name.lower().endswith('.fxo'):
                continue
            f.seek(offset)
            raw = f.read(stored)
            data = zlib.decompress(raw) if stored != size else raw
            if len(data) != size or any(c.isspace() for c in name):
                raise SystemExit(f'{name}: unexpected entry')
            effects[name] = data
    return effects


def locate(ucode, effects, image):
    """Where a shader's microcode is: (source, place, offset, patches) or None."""
    n = len(ucode)
    whole = struct.pack(f'>{n}I', *ucode)
    for name, data in effects.items():
        at = data.find(whole)
        if at >= 0:
            return 'cat', name, at, []
    at = image.find(whole)
    if at >= 0:
        return 'exe', None, at, []
    # A bound vertex shader: the copy with the same first words and the fewest differing dwords.
    head = whole[:16]
    best = None
    for source, name, data in [('cat', k, v) for k, v in effects.items()] + [('exe', None, image)]:
        at = data.find(head)
        while at >= 0:
            if at + n * 4 <= len(data):
                copy = struct.unpack_from(f'>{n}I', data, at)
                patches = [(i, copy[i] ^ ucode[i]) for i in range(n) if copy[i] != ucode[i]]
                if best is None or len(patches) < len(best[3]):
                    best = (source, name, at, patches)
            at = data.find(head, at + 4)
    # More than a quarter of it different is not the same shader.
    if best and len(best[3]) <= max(4, n // 4):
        return best
    return None


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    assets, image_path, out = sys.argv[1:4]
    shaders, keys = {}, set()
    key_size = 0
    for path in sys.argv[4:]:
        size = read_prewarm(path, shaders, keys)
        if key_size and size and size != key_size:
            raise SystemExit(f'{path}: pipeline keys of {size} bytes, others {key_size}')
        key_size = key_size or size
    effects = read_effects(assets)
    image = open(image_path, 'rb').read()
    title = {22806528: '565507D9', 24248320: '565507E8'}.get(len(image), '00000000')

    lines, placed = [], set()
    counts = {'cat': 0, 'exe': 0, 'patched': 0, 'missing': 0}
    for h, (t, ucode, mods) in sorted(shaders.items()):
        found = locate(ucode, effects, image)
        if not found:
            counts['missing'] += 1
            print(f'  {h:016X} ({"pixel" if t else "vertex"}, {len(ucode)} dwords): not found, left out')
            continue
        source, name, at, patches = found
        counts[source] += 1
        counts['patched'] += bool(patches)
        placed.add(h)
        where = f'cat {name} {at:x}' if source == 'cat' else f'exe {at:x}'
        line = f's {h:016x} {t} {len(ucode)} {",".join(f"{m:x}" for m in sorted(mods))} {where}'
        lines.append(line + ''.join(f' {i}:{x:x}' for i, x in patches))
    kept = 0
    for key in sorted(keys):
        vs, _vs_mod, ps = struct.unpack_from('<QQQ', key, 0)
        if vs not in placed or (ps and ps not in placed):
            continue
        words = struct.unpack(f'<{len(key) // 4}I', key)
        lines.append('p ' + ' '.join(f'{w:x}' for w in words))
        kept += 1

    body = '\n'.join([f'warmup 1 {title} {key_size}'] + lines)
    if ')WARMUP"' in body:
        raise SystemExit('the list would end its own string')
    with open(out, 'w', newline='\n') as f:
        f.write(f'// Shader warm-up list for title {title}, written by tools/make_warmup.py from '
                f'{len(sys.argv) - 4} prewarm file(s):\n'
                '// where each shader seen in play is in the game data, with its variants and '
                'pipelines. No shader\n'
                '// code: the renderer rebuilds the shaders from the player\'s own files at startup '
                '(#53).\n')
        f.write('R"WARMUP(' + body + '\n)WARMUP"\n')
    print(f'{out}: {len(placed)} shaders ({counts["cat"]} in effects, {counts["exe"]} in the '
          f'executable, {counts["patched"]} patched vertex shaders), {counts["missing"]} left out; '
          f'{kept} of {len(keys)} pipelines')


if __name__ == '__main__':
    main()
