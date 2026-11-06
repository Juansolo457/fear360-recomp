"""Every hook's address, with a fingerprint of the game code there (port-nx/src/image_check.h).

    python -I tools/hook_sites.py            write the tables from the dumped images
        [--fear-image private/image/default.bin] [--files-image private/files/image/default.bin]
    python -I tools/hook_sites.py --check    the tables list exactly the hooks in the manifests
                                             and the source (no game files needed; CI runs it)

A hook is one of:
  - an [[entrypoint.midasm_hook]] in port-nx/fear_manifest.toml (F.E.A.R.) or
    port-files/fear_manifest.toml (F.E.A.R. Files): a host function called inside a game function;
  - a REX_HOOK_RAW(sub_XXXXXXXX), or a FEAR_..._HOOK(...) macro that makes one, in port-nx/src:
    a host function in place of a whole game function. Where a file has one list per game
    (#if defined(FEAR_TITLE_FILES) ... #else ... #endif), each game gets its own.

The tables (port-nx/src/hook_sites_fear.inc, hook_sites_files.inc) hold, per hook, its address, a
64-bit hash (FNV-1a) of the 32 bytes of the game's code there (8 instructions) and its name. They
hold no game code: only the hashes. The game checks them once at startup against the executable
it loaded, so a different or altered executable is reported instead of every hook quietly landing
on the wrong code.

The images are the loaded executables, written by the game with --fear_dump_image <path>.
"""
import argparse
import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'port-nx', 'src')
IMAGE_BASE = 0x82000000
SITE_BYTES = 32

TITLES = {
    'fear': {
        'name': 'F.E.A.R.',
        'manifest': 'port-nx/fear_manifest.toml',
        'table': 'port-nx/src/hook_sites_fear.inc',
        'image': 'private/image/default.bin',
    },
    'files': {
        'name': 'F.E.A.R. Files',
        'manifest': 'port-files/fear_manifest.toml',
        'table': 'port-nx/src/hook_sites_files.inc',
        'image': 'private/files/image/default.bin',
    },
}

# REX_HOOK_RAW(sub_82363350), FEAR_WEAPON_SLOT_HOOK(sub_823E5AC8), FEAR_D3D12_HOOK(82988340, ...
HOOK = re.compile(r'\b(?:REX_HOOK\w*|FEAR_\w+_HOOK)\(\s*(?:sub_)?([0-9A-Fa-f]{8})\b')
ROW = re.compile(r'^FEAR_HOOK_SITE\(0x([0-9A-F]{8}), 0x([0-9A-F]{16})ull, "([^"]*)"\)$')


def manifest_hooks(title):
    """The (address, name) of every [[entrypoint.midasm_hook]] in this game's manifest."""
    with open(os.path.join(ROOT, TITLES[title]['manifest']), 'rb') as file:
        manifest = tomllib.load(file)
    return {(hook['address'], hook['name']) for hook in manifest['entrypoint']['midasm_hook']}


def source_hooks(title):
    """Whole-function hooks in port-nx/src for this game, following FEAR_TITLE_FILES."""
    files = title == 'files'
    hooks = set()
    for folder, _, names in os.walk(SOURCE):
        for name in sorted(names):
            if not name.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(folder, name)
            # One entry per open #if: True or False when it tests FEAR_TITLE_FILES alone (whether
            # this game's lines are the ones inside), None for any other condition.
            conditions = []
            with open(path, encoding='utf-8') as file:
                for line in file:
                    text = line.strip()
                    if text.startswith('#'):
                        directive = text[1:].strip()
                        if re.match(r'(if\s+defined\s*\(\s*FEAR_TITLE_FILES\s*\)|ifdef\s+FEAR_TITLE_FILES)\s*(//.*)?$', directive):
                            conditions.append(files)
                        elif re.match(r'(if\s+!\s*defined\s*\(\s*FEAR_TITLE_FILES\s*\)|ifndef\s+FEAR_TITLE_FILES)\s*(//.*)?$', directive):
                            conditions.append(not files)
                        elif directive.startswith('if') and 'FEAR_TITLE_FILES' in directive:
                            # With other conditions, all of which must hold (&&): the lines are
                            # left out for the game the condition excludes, and its #else kept
                            # for both.
                            if '||' in directive:
                                sys.exit(f'{path}: a FEAR_TITLE_FILES condition this script '
                                         f'does not read: {text}')
                            negated = re.search(r'!\s*defined\s*\(\s*FEAR_TITLE_FILES', directive)
                            conditions.append('excluded' if bool(negated) == files else None)
                        elif directive.startswith('if'):
                            conditions.append(None)
                        elif directive.startswith('else') and conditions:
                            if isinstance(conditions[-1], bool):
                                conditions[-1] = not conditions[-1]
                            else:
                                conditions[-1] = None
                        elif directive.startswith('elif') and conditions:
                            conditions[-1] = None
                        elif directive.startswith('endif') and conditions:
                            conditions.pop()
                        continue  # a #define's own text names no address
                    if any(condition in (False, 'excluded') for condition in conditions):
                        continue
                    for match in HOOK.finditer(line.split('//')[0]):
                        address = int(match.group(1), 16)
                        hooks.add((address, f'sub_{address:08X} ({name})'))
    return hooks


def all_hooks(title):
    """Every hook of this game: the manifest's and the source's, as (address, name)."""
    return manifest_hooks(title) | source_hooks(title)


def fnv1a64(data):
    """FNV-1a, 64 bits: image_check.cpp's Hash computes the same over the loaded image."""
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def read_table(title):
    """The (address, name) rows of this game's table, or None when the file is missing."""
    path = os.path.join(ROOT, TITLES[title]['table'])
    rows = set()
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            match = ROW.match(line)
            if not match:
                sys.exit(f'{path}: not a table row: {line}')
            rows.add((int(match.group(1), 16), match.group(3)))
    return rows


def write_table(title, image_path):
    """Write this game's table: one row per hook, hashed from the dumped image."""
    with open(image_path, 'rb') as file:
        image = file.read()
    lines = [
        f'// {TITLES[title]["name"]}: every hook\'s address, a hash of the 32 bytes of game code there and',
        '// the hook\'s name. Written by tools/hook_sites.py from the loaded executable; read by',
        '// image_check.cpp. Hashes only: no game code.',
    ]
    hooks = sorted(all_hooks(title))
    for address, name in hooks:
        offset = address - IMAGE_BASE
        if offset < 0 or offset + SITE_BYTES > len(image):
            sys.exit(f'{name} at {address:08X} is outside the image {image_path}')
        if address % 4:
            sys.exit(f'{name} at {address:08X} is not at an instruction')
        digest = fnv1a64(image[offset:offset + SITE_BYTES])
        lines.append(f'FEAR_HOOK_SITE(0x{address:08X}, 0x{digest:016X}ull, "{name}")')
    path = os.path.join(ROOT, TITLES[title]['table'])
    with open(path, 'w', encoding='utf-8', newline='\n') as file:
        file.write('\n'.join(lines) + '\n')
    print(f'{TITLES[title]["table"]}: {len(hooks)} hooks')


def check(title):
    """Whether this game's table lists exactly its hooks; prints each difference."""
    hooks = all_hooks(title)
    rows = read_table(title)
    table = TITLES[title]['table']
    if rows is None:
        print(f'{table} is missing')
        return False
    for address, name in sorted(hooks - rows):
        print(f'{table}: no row for {name} at {address:08X}')
    for address, name in sorted(rows - hooks):
        print(f'{table}: a row for {name} at {address:08X}, which is no longer a hook')
    return hooks == rows


def main():
    """Write both tables, or with --check compare them with the hooks (exit 1 on a difference)."""
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--check', action='store_true')
    for title, info in TITLES.items():
        parser.add_argument(f'--{title}-image', default=os.path.join(ROOT, info['image']))
    arguments = parser.parse_args()
    if arguments.check:
        good = [check(title) for title in TITLES]
        if not all(good):
            sys.exit('The hook tables are out of date: run tools/hook_sites.py (it needs the '
                     'dumped images, see its header).')
        print('Hook tables: every hook has a row.')
        return
    for title in TITLES:
        write_table(title, getattr(arguments, f'{title}_image'))


if __name__ == '__main__':
    main()
