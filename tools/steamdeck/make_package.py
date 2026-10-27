"""Build the Steam Deck test package: both games' Windows builds for Proton, as one zip.

    python -I tools/steamdeck/make_package.py [out.zip]     (default private/steamdeck/FEAR360-SteamDeck.zip)
    python -I tools/steamdeck/make_package.py --programs [out.zip]
        just the programs and DLLs (default private/steamdeck/FEAR360-SteamDeck-update.zip), to
        unpack over an existing install: no data, saves or settings
    python -I tools/steamdeck/make_package.py --windows [out.zip]
        the whole package for another Windows PC (default private/windows/FEAR360-Windows.zip),
        with a Play F.E.A.R..cmd instead of the Deck's Steam setup: a private test copy
    python -I tools/steamdeck/make_package.py --settings [out.zip]
        just both games' user/settings.toml (default private/steamdeck/FEAR360-SteamDeck-settings.zip),
        to unpack over an existing install: the Deck settings below, saves untouched
    python3 -I tools/steamdeck/make_package.py --linux [out.zip]     (on Linux: WSL)
        the native Linux programs from tools/build-deck.sh (default
        private/steamdeck/FEAR360-SteamDeck-linux.zip), to unpack over an existing install: each
        game's program and its libraries, fear-linux.sh (the launcher, no Proton) and
        README-SteamDeck-Linux.txt. The Proton programs stay; data, saves and settings untouched

Layout (FEAR360-SteamDeck/):
  README-SteamDeck.txt   setup on the Deck (tools/steamdeck/README-SteamDeck.txt)
  collection.toml        the collection menu, with paths relative to each game's folder
  fear/, fearfiles/      each game's program, rexruntimerd.dll, TracyClientrd.dll and the Visual
                         C++ runtime (Proton's builtin one lacks msvcp140_atomic_wait.dll), its
                         assets/ (game data) and user/ (saves; settings.toml replaced with the
                         Deck's, see DECK_SETTINGS). No PC texture pack: the Deck baseline is the
                         360's own textures (SHIP_TEXTURE_PACKS).

The game reads its paths from the command line only, so the Steam launch options in the README pass
them relative to the game's folder (Steam's Start In). No shader cache: it is per GPU, and the Deck
builds its own over the first runs. Stored (not compressed) and Zip64: the data is ~13 GB and
mostly compressed already. The game data is the user's own disc's: the package stays in private/.
These packages contain game data and programs built from the game's code: never upload one, share
it or attach it to a release. Releases are source only.
"""
import glob
import os
import sys
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ARGV = [a for a in sys.argv[1:] if a not in ('--programs', '--settings', '--windows', '--linux')]
PROGRAMS_ONLY = '--programs' in sys.argv[1:]
SETTINGS_ONLY = '--settings' in sys.argv[1:]
# --windows: the same package for another Windows PC (a private test copy): a Play F.E.A.R..cmd
# with the launch options below instead of the Deck's Steam setup.
WINDOWS = '--windows' in sys.argv[1:]
# --linux: the native Linux build (tools/build-deck.sh), no Proton (docs/LINUX_NATIVE.md).
LINUX = '--linux' in sys.argv[1:]
OUT = ARGV[0] if ARGV else os.path.join(
    REPO, 'private', 'windows' if WINDOWS else 'steamdeck',
    'FEAR360-Windows.zip' if WINDOWS else
    'FEAR360-SteamDeck-linux.zip' if LINUX else
    'FEAR360-SteamDeck-update.zip' if PROGRAMS_ONLY else
    'FEAR360-SteamDeck-settings.zip' if SETTINGS_ONLY else 'FEAR360-SteamDeck.zip')
TOP = 'FEAR360-Windows' if WINDOWS else 'FEAR360-SteamDeck'
CRT_DLLS = ['msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']
ARGS = ['--resolution', '720p', '--anisotropic_override', '5', '--game_data_root', 'assets',
        '--user_data_root', 'user', '--cache_root', 'cache']
# The PC texture packs (4x the pixels) cost the Deck's shared memory bandwidth; the baseline is the
# 360's own textures. True ships them and passes --fear_texture_pack again.
SHIP_TEXTURE_PACKS = False
if SHIP_TEXTURE_PACKS:
    ARGS += ['--fear_texture_pack', 'texture_pack']
DECK_SETTINGS = ('# F.E.A.R. PC Settings, written by the panel in the main menu.\n'
                 '# Loaded at startup; a flag on the command line still wins for that run.\n'
                 '# Uncapped to 120 fps (#28): the vblank from a timer at 240 Hz instead of the\n'
                 '# display (the game presents every second one), mailbox presentation (no\n'
                 '# tearing; on a 60 Hz screen the extra frames are dropped). 120 is the tested\n'
                 '# ceiling: EchoPatch lists physics and particle trouble above it. For 60 again,\n'
                 '# replace these three lines with: fear_frame_rate_limit = 60\n'
                 'fear_vblank_source = "timer"\n'
                 'video_mode_refresh_rate = 240\n'
                 "# Deck baseline (#28): SDL's HIDAPI drivers off (the controls come through Steam\n"
                 "# Input's pad, the usual Proton path; #29) and the profiler on for the logs\n"
                 '# (about 6% of a core). Saving PC Settings in the game rewrites this file\n'
                 '# without them. No MSAA (the game\'s 2x/4x costs the Deck\'s GPU; PC Settings >\n'
                 '# Anti-aliasing).\n'
                 'fear_d3d12_msaa = false\n'
                 'hid_sdl_hidapi = "off"\n'
                 'fear_profile = true\n'
                 'fear_profile_hz = 200\n')
# Windows: the Deck's settings without hid_sdl_hidapi = "off", which is for Proton's SDL. On
# Windows the games read controllers through XInput by default (fear_app.h): SDL found no usable
# pad in the game on a work laptop, 2026-10-23 (#54).
WINDOWS_SETTINGS = DECK_SETTINGS.replace(
    "# Deck baseline (#28): SDL's HIDAPI drivers off (the controls come through Steam\n"
    "# Input's pad, the usual Proton path; #29) and the profiler on for the logs\n"
    '# (about 6% of a core).',
    '# Deck baseline (#28): the profiler on for the logs (about 6% of a core).').replace(
    'hid_sdl_hidapi = "off"\n', '')
assert WINDOWS_SETTINGS != DECK_SETTINGS and 'hid_sdl_hidapi' not in WINDOWS_SETTINGS
WINDOWS_LAUNCHER = ('@echo off\r\n'
                    'rem F.E.A.R. 360 recomp, private test copy: the collection menu.\r\n'
                    'cd /d "%~dp0fear"\r\n'
                    'start "" fear.exe ' + ' '.join(ARGS) + ' --fear_collection ../collection.toml\r\n')
WINDOWS_README = """F.E.A.R. 360 recomp (F.E.A.R. + F.E.A.R. Files): private Windows test copy
=========================================================================

The same builds and settings as the Steam Deck package, for another Windows PC. It contains your
own game data and programs made from your discs: keep it to yourself.

Needs: Windows 10/11 64-bit, a CPU with AVX2 (Intel Haswell / AMD Excavator or newer), a
Direct3D 12 GPU, and an Xbox controller.

Play: extract the zip anywhere, then double-click "Play F.E.A.R..cmd". It opens the collection
menu; pick a game. If the controller doesn't react there, click the game window once.

First start of each game: shaders and pipelines are built as they first appear (brief hitches
the first time an area or effect is seen) and kept in the cache folder for later starts.

Settings (each game's user/settings.toml, as on the Deck): up to 120 fps, no PC texture pack,
the profiler on for the logs. Controllers are read through
XInput, the Windows default; an input_backend line from an earlier copy can stay. PC
Settings in each game's main menu changes them (it then rewrites the file without the profiler
lines).

Logs: fear/logs and fearfiles/logs, one file per run; copy the newest back for a read.
"""
GAMES = [
    # (folder in the zip, project, program, texture pack, id, name, subtitle)
    ('fear', 'port-nx', 'fear.exe', os.path.join('private', 'texture_pack'),
     'fear', 'F.E.A.R.', 'First Encounter Assault Recon'),
    ('fearfiles', 'port-files', 'fearfiles.exe', os.path.join('private', 'files', 'texture_pack'),
     'files', 'F.E.A.R. Files', 'Extraction Point and Perseus Mandate'),
]


def crt_folder():
    """The newest x64 Visual C++ runtime in a Visual Studio 2022 install."""
    found = sorted(glob.glob(r'C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Redist\MSVC'
                             r'\14.*\x64\Microsoft.VC14*.CRT'))
    found = [f for f in found if all(os.path.exists(os.path.join(f, d)) for d in CRT_DLLS)]
    if not found:
        raise SystemExit('No Visual C++ runtime found (Visual Studio 2022 VC\\Redist).')
    return found[-1]


def main_linux():
    """The native Linux update: tools/build-deck.sh's programs and libraries (the build's own
    shared libraries, found next to each program through its $ORIGIN run path) and the launcher."""
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    count = total = 0
    versions = []
    with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        def add(src, arc, executable=False):
            nonlocal count, total
            info = zipfile.ZipInfo.from_file(src, f'{TOP}/{arc}')
            # Unix permissions, so the Deck's unzip keeps the programs runnable.
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(src, 'rb') as f:
                z.writestr(info, f.read())
            count += 1
            total += os.path.getsize(src)

        steamdeck = os.path.join(REPO, 'tools', 'steamdeck')
        add(os.path.join(steamdeck, 'fear-linux.sh'), 'fear-linux.sh', executable=True)
        add(os.path.join(steamdeck, 'README-SteamDeck-Linux.txt'), 'README-SteamDeck-Linux.txt')
        for folder, project, exe, *_ in GAMES:
            program = exe.removesuffix('.exe')
            build = os.path.join(REPO, project, 'out', 'build', 'deck-amd64-relwithdebinfo')
            if not os.path.exists(os.path.join(build, program)):
                raise SystemExit(f'{build}/{program} is missing: run tools/build-deck.sh'
                                 + ('' if project == 'port-nx' else f' --project {project}'))
            add(os.path.join(build, program), f'{folder}/{program}', executable=True)
            for lib in sorted(glob.glob(os.path.join(build, '*.so*'))):
                add(lib, f'{folder}/{os.path.basename(lib)}', executable=True)
            text = 'unknown'
            header = os.path.join(build, 'fear_version', 'fear_version_info.h')
            if os.path.exists(header):
                for line in open(header, encoding='utf-8'):
                    if line.startswith('#define FEAR_VERSION_TEXT '):
                        text = line.split('"')[1]
            versions.append(f'{program} (Linux): {text}')
        z.writestr(f'{TOP}/VERSION-linux.txt', '\n'.join(versions) + '\n')
    print('version: ' + '; '.join(versions))
    print(f'{OUT}: {count} files, {total / 2**20:.0f} MB before compression')


def main():
    if LINUX:
        main_linux()
        return
    if SETTINGS_ONLY:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED) as z:
            for folder, *_ in GAMES:
                z.writestr(f'{TOP}/{folder}/user/settings.toml', DECK_SETTINGS)
        print(f'{OUT}: settings.toml for {len(GAMES)} games')
        return
    crt = crt_folder()
    collection =["# The F.E.A.R. collection menu, Steam Deck layout: paths relative to each "
                  "game's folder."]
    for folder, _, exe, _, cid, name, subtitle in GAMES:
        collection += ['', '[[title]]', f"id = '{cid}'", f"name = '{name}'",
                       f"subtitle = '{subtitle}'", f"exe = '../{folder}/{exe}'",
                       f"working_dir = '../{folder}'",
                       'args = [' + ', '.join(f"'{a}'" for a in ARGS) + ']']
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    count = total = 0
    with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_STORED, allowZip64=True) as z:
        def add(src, arc):
            nonlocal count, total
            z.write(src, f'{TOP}/{arc}')
            count += 1
            total += os.path.getsize(src)

        def add_tree(src_root, arc_root, skip=()):
            for dirpath, dirnames, filenames in os.walk(src_root, followlinks=True):
                dirnames.sort()
                for f in sorted(filenames):
                    src = os.path.join(dirpath, f)
                    rel = os.path.relpath(src, src_root).replace('\\', '/')
                    if rel not in skip:
                        add(src, f'{arc_root}/{rel}')

        if WINDOWS:
            z.writestr(f'{TOP}/README-Windows.txt', WINDOWS_README.replace('\n', '\r\n'))
            z.writestr(f'{TOP}/Play F.E.A.R..cmd', WINDOWS_LAUNCHER)
        else:
            add(os.path.join(REPO, 'tools', 'steamdeck', 'README-SteamDeck.txt'),
                'README-SteamDeck.txt')
        if not PROGRAMS_ONLY:
            z.writestr(f'{TOP}/collection.toml', '\n'.join(collection) + '\n')
        # Which build each program is (port-nx/src/fear_version.h), as the game shows it.
        versions = []
        for folder, project, exe, *_ in GAMES:
            header = os.path.join(REPO, project, 'out', 'build', 'win-amd64-relwithdebinfo',
                                  'fear_version', 'fear_version_info.h')
            text = 'unknown'
            if os.path.exists(header):
                for line in open(header, encoding='utf-8'):
                    if line.startswith('#define FEAR_VERSION_TEXT '):
                        text = line.split('"')[1]
            versions.append(f'{exe}: {text}')
        z.writestr(f'{TOP}/VERSION.txt', '\r\n'.join(versions) + '\r\n')
        print('version: ' + '; '.join(versions))
        for folder, project, exe, pack, *_ in GAMES:
            build = os.path.join(REPO, project, 'out', 'build', 'win-amd64-relwithdebinfo')
            for f in (exe, 'rexruntimerd.dll', 'TracyClientrd.dll'):
                add(os.path.join(build, f), f'{folder}/{f}')
            for f in CRT_DLLS:
                add(os.path.join(crt, f), f'{folder}/{f}')
            if PROGRAMS_ONLY:
                continue
            add_tree(os.path.join(REPO, project, 'assets'), f'{folder}/assets')
            add_tree(os.path.join(REPO, project, 'user'), f'{folder}/user', skip={'settings.toml'})
            z.writestr(f'{TOP}/{folder}/user/settings.toml',
                       WINDOWS_SETTINGS if WINDOWS else DECK_SETTINGS)
            if SHIP_TEXTURE_PACKS and os.path.exists(os.path.join(REPO, pack, 'index.tsv')):
                add_tree(os.path.join(REPO, pack), f'{folder}/texture_pack')
            print(f'{folder}: {count} files, {total / 2**30:.1f} GB so far', flush=True)
    print(f'{OUT}: {count} files, {total / 2**30:.2f} GB')


if __name__ == '__main__':
    main()
