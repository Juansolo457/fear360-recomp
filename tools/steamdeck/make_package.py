"""Build the Steam Deck test package: both games' Windows builds for Proton, as one zip.

    python -I tools/steamdeck/make_package.py [out.zip]     (default private/steamdeck/FEAR360-SteamDeck.zip)

Layout (FEAR360-SteamDeck/):
  README-SteamDeck.txt   setup on the Deck (tools/steamdeck/README-SteamDeck.txt)
  collection.toml        the collection menu, with paths relative to each game's folder
  fear/, fearfiles/      each game's program, rexruntimerd.dll, TracyClientrd.dll and the Visual
                         C++ runtime (Proton's builtin one lacks msvcp140_atomic_wait.dll), its
                         assets/ (game data), user/ (saves; settings.toml replaced with the Deck's:
                         render scale 1x) and texture_pack/ when one is built (docs/TEXTURES.md)

The game reads its paths from the command line only, so the Steam launch options in the README pass
them relative to the game's folder (Steam's Start In). No shader cache: it is per GPU, and the Deck
builds its own over the first runs. Stored (not compressed) and Zip64: the data is ~13 GB and
mostly compressed already. The game data is the user's own disc's: the package stays in private/.
"""
import glob
import os
import sys
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'private', 'steamdeck',
                                                         'FEAR360-SteamDeck.zip')
TOP = 'FEAR360-SteamDeck'
CRT_DLLS = ['msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']
ARGS = ['--resolution', '720p', '--anisotropic_override', '5', '--game_data_root', 'assets',
        '--user_data_root', 'user', '--cache_root', 'cache', '--nx_renderer', 'native',
        '--fear_texture_pack', 'texture_pack']
DECK_SETTINGS = ('# F.E.A.R. PC Settings, written by the panel in the main menu.\n'
                 '# Loaded at startup; a flag on the command line still wins for that run.\n'
                 "# Steam Deck: 1x (1280x720 for the Deck's 1280x800 screen), and 60 fps:\n"
                 '# the Deck reported 120 Hz through gamescope when docked, and the game\n'
                 "# paced to 120 while the Deck's own limit showed 60 (play-test, #28).\n"
                 'nx_native_resolution_scale = 1\n'
                 'fear_frame_rate_limit = 60\n')
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


def main():
    crt = crt_folder()
    collection = ["# The F.E.A.R. collection menu, Steam Deck layout: paths relative to each "
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

        add(os.path.join(REPO, 'tools', 'steamdeck', 'README-SteamDeck.txt'), 'README-SteamDeck.txt')
        z.writestr(f'{TOP}/collection.toml', '\n'.join(collection) + '\n')
        for folder, project, exe, pack, *_ in GAMES:
            build = os.path.join(REPO, project, 'out', 'build', 'win-amd64-relwithdebinfo')
            for f in (exe, 'rexruntimerd.dll', 'TracyClientrd.dll'):
                add(os.path.join(build, f), f'{folder}/{f}')
            for f in CRT_DLLS:
                add(os.path.join(crt, f), f'{folder}/{f}')
            add_tree(os.path.join(REPO, project, 'assets'), f'{folder}/assets')
            add_tree(os.path.join(REPO, project, 'user'), f'{folder}/user', skip={'settings.toml'})
            z.writestr(f'{TOP}/{folder}/user/settings.toml', DECK_SETTINGS)
            if os.path.exists(os.path.join(REPO, pack, 'index.tsv')):
                add_tree(os.path.join(REPO, pack), f'{folder}/texture_pack')
            print(f'{folder}: {count} files, {total / 2**30:.1f} GB so far', flush=True)
    print(f'{OUT}: {count} files, {total / 2**30:.2f} GB')


if __name__ == '__main__':
    main()
