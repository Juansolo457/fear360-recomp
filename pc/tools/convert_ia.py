"""Converts the Xbox 360 F.E.A.R.'s four Instant Action maps to worlds the PC game loads.

    python -I pc/tools/convert_ia.py --x360 <Game dir> --pc <game folder>
                                     [--out dir] [--map N] [--extra dir] [--compare dir]
                                     [--skip-fix name] [--fix name] [--mod-names]

  --x360     the 360 game's Game folder (holding Worlds/Release/InstantAction1..4.wld,
             .wldclnt and .wldsrvr), or the folder above it. Read only.
  --pc       the PC game's folder (FEAR.exe, Default.archcfg and the FEAR*.Arch00
             archives). Read only. Default: game_dir in private/pc.toml.
  --out      where Worlds/Release/InstantAction<N>.World00p is written, with a report
             beside each. Default build/pc/ia. A folder git tracks is refused.
  --map      convert one map (1 to 4) and not all four.
  --extra    a folder of loose files the game will also have (as a mod folder would be
             mounted); may be given more than once. Its files count as present on PC.
  --compare  a folder holding another converter's worlds for the same maps
             (InstantAction<N>.World00p, or the mod's IA_<name>.World00p). Reports each
             section as identical or gives the first differing offset. Nothing is written.
  --skip-fix leave out a content fix: machine-pistol, event-slots, voice-loop, file-names.
  --fix      apply an optional content fix: rooftop-gates.
  --mod-names resolve file names only the way the mod did (see docs/PC-WORLDS.md, "Checked
             against the mod"). With --extra <the mod's folder> this makes the output
             comparable byte for byte; it is not for worlds to play.

Everything comes from the player's own copies of the two games; nothing is bundled. The
maps belong to the 360 release with title ID 565507D9. The PC game needs no companion file
beside a .World00p.

The file formats were learned from the installer of the mod "F.E.A.R. Exclusive Console
Content"; this converter is our own code. docs/PC-WORLDS.md describes the formats, the
content fixes and what has been checked.
"""
import argparse
import os
import subprocess
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from iaworld import WorldError                       # noqa: E402
from iaworld import compare as comparing             # noqa: E402
from iaworld import convert                          # noqa: E402
from iaworld.pcfiles import PcFiles                  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAPS = (1, 2, 3, 4)
# What the mod named its output for maps 1 to 4, for --compare.
MOD_NAMES = {1: 'IA_Distribution', 2: 'IA_Rooftop', 3: 'IA_Construction', 4: 'IA_Vault'}
SHOWN = 8   # lines of each list in the console summary; the report file has all of them


def find_folder(root, *parts):
    """root/parts..., matching names without regard to case; None when absent."""
    current = root
    for part in parts:
        try:
            names = os.listdir(current)
        except OSError:
            return None
        match = next((name for name in names if name.lower() == part.lower()), None)
        if match is None:
            return None
        current = os.path.join(current, match)
    return current


def worlds_folder(x360):
    for parts in (('Worlds', 'Release'), ('Game', 'Worlds', 'Release')):
        folder = find_folder(x360, *parts)
        if folder:
            return folder
    sys.exit(f'{x360}: no Worlds/Release folder here; give the 360 game\'s Game folder')


def read_map(folder, number):
    files = []
    for extension in ('wld', 'wldclnt', 'wldsrvr'):
        path = find_folder(folder, f'InstantAction{number}.{extension}')
        if path is None:
            sys.exit(f'{folder}: InstantAction{number}.{extension} is missing')
        with open(path, 'rb') as file:
            files.append(file.read())
    return files


def default_pc_folder():
    config = os.path.join(REPO, 'private', 'pc.toml')
    if not os.path.isfile(config):
        return None
    with open(config, 'rb') as file:
        return tomllib.load(file).get('game_dir')


def refuse_tracked(out):
    """Stops if `out` is inside a git work tree and not ignored there."""
    probe = os.path.abspath(out)
    while not os.path.isdir(probe):
        probe = os.path.dirname(probe)
    try:
        top = subprocess.run(['git', '-C', probe, 'rev-parse', '--show-toplevel'],
                             capture_output=True, text=True)
    except OSError:
        return      # no git here, so nothing can track it
    if top.returncode != 0:
        return      # not in a repository
    ignored = subprocess.run(['git', '-C', probe, 'check-ignore', '-q', os.path.abspath(out)])
    if ignored.returncode != 0:
        sys.exit(f'{out}: this folder is inside a git repository and not ignored. Converted '
                 'worlds are game data and must not be committed; choose another --out.')


def report_lines(number, world, report):
    """The full report for one map."""
    parts = convert.sections(world)
    lines = [f'InstantAction{number}.World00p: {len(world)} bytes',
             '  sections: ' + ', '.join(f'{name} {len(data)}' for name, data in parts.items()),
             f'  objects: {report.objects_out} of {report.objects_in}',
             f'  render: {report.render["meshes"]} meshes, {report.render["vertex_bytes"]} bytes of vertices; '
             f'inputs made up for {report.render["made_up_vertices"]} vertices',
             f'  blind data entries: {sum(report.blind.values())}',
             f'  precache list: {report.precache_kept} names kept of {report.precache_total}']
    lines.append(f'Content fixes applied: {len(report.fixes)}')
    lines += [f'  [{fix_id}] {text}' for fix_id, text in report.fixes]
    lines.append('What the PC lacks:')
    console_only = sum(report.console_only.values())
    by_kind = ', '.join(f'{count} .{ext}' for ext, count in sorted(report.console_only.items()))
    lines.append(f'  precache names of console-only kinds, left out: {console_only}' + (f' ({by_kind})' if by_kind else ''))
    lines.append(f'  precache names with no PC file, left out: {len(report.precache_missing)}')
    lines += [f'    {path}' for path in report.precache_missing]
    lines.append(f'  materials the render data names that the PC lacks: {len(report.materials_missing)}')
    lines += [f'    {name}' for name in report.materials_missing]
    lines.append(f'  materials no stock PC world uses (vertex inputs unknown, none added): {len(report.materials_unknown)}')
    lines += [f'    {name}' for name in report.materials_unknown]
    lines.append(f'  files objects name that the PC lacks (name kept as it was): {len(report.files_missing)}')
    lines += [f'    {label}.{prop}: {value}' for label, prop, value in report.files_missing]
    lines.append(f'  weapon pickups of types the PC lacks: {sum(report.weapons_missing.values())}')
    lines += [f'    {count} x {weapon}' for weapon, count in sorted(report.weapons_missing.items())]
    lines.append(f'  classes the PC lacks (objects left out): {sum(report.classes_missing.values())}')
    lines += [f'    {count} x {cls}' for cls, count in sorted(report.classes_missing.items())]
    with_value = {key: counts for key, counts in report.dropped.items() if counts[1]}
    lines.append(f'  properties the PC class lacks, dropped: {sum(c[0] for c in report.dropped.values())} '
                 f'({len(report.dropped)} kinds; {len(with_value)} kinds held a value somewhere)')
    lines += [f'    {cls}.{prop}: {counts[1]} with a value of {counts[0]}'
              for (cls, prop), counts in sorted(with_value.items())]
    lines.append(f'  CommandObject events with no free PC slot, lost: {len(report.events_lost)}')
    lines += [f'    {label}.{prop}' for label, prop in report.events_lost]
    if report.unwritable:
        lines.append(f'  PC properties with a default that cannot be written, left out: {len(report.unwritable)}')
        lines += [f'    {cls}.{prop}' for cls, prop in sorted(set(report.unwritable))]
    return lines


def shortened(lines):
    """The report with long lists cut to SHOWN lines for the console."""
    out, run = [], 0
    for line in lines:
        if line.startswith('    '):
            run += 1
            if run == SHOWN + 1:
                out.append('    ... (the rest is in the report file)')
            if run > SHOWN:
                continue
        else:
            run = 0
        out.append(line)
    return out


def other_world(folder, number):
    for base in (folder, find_folder(folder, 'Worlds', 'Release')):
        for name in (f'InstantAction{number}.World00p', MOD_NAMES[number] + '.World00p'):
            path = find_folder(base, name) if base else None
            if path:
                return path
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--x360', required=True, help='the 360 game\'s Game folder')
    parser.add_argument('--pc', default=default_pc_folder(), help='the PC game\'s folder')
    parser.add_argument('--out', default=os.path.join(REPO, 'build', 'pc', 'ia'))
    parser.add_argument('--map', type=int, choices=MAPS, help='convert only this map')
    parser.add_argument('--extra', action='append', default=[], help='a folder of loose files the game also has')
    parser.add_argument('--compare', help='a folder with another converter\'s worlds; compare, write nothing')
    parser.add_argument('--skip-fix', action='append', default=[], choices=sorted(convert.FIXES),
                        help='leave out a content fix')
    parser.add_argument('--mod-names', action='store_true', help='resolve names only as the mod did (for --compare)')
    parser.add_argument('--fix', action='append', default=[], choices=sorted(convert.OPTIONAL_FIXES),
                        help='apply an optional content fix')
    args = parser.parse_args()
    if not args.pc:
        sys.exit('Give the PC game\'s folder with --pc (or set game_dir in private/pc.toml).')
    if not os.path.isdir(args.pc):
        sys.exit(f'{args.pc}: not a folder')
    if not os.path.isdir(args.x360):
        sys.exit(f'{args.x360}: not a folder')
    if args.compare and not os.path.isdir(args.compare):
        sys.exit(f'{args.compare}: not a folder')
    source = worlds_folder(args.x360)
    if not args.compare:
        refuse_tracked(args.out)
    try:
        pc = convert.PcReference(PcFiles(args.pc, args.extra, not args.mod_names))
    except (WorldError, OSError) as error:
        sys.exit(f'PC game: {error}')
    failed = False
    for number in ([args.map] if args.map else MAPS):
        try:
            world, report = convert.convert_map(number, *read_map(source, number), pc, args.skip_fix, args.fix)
        except WorldError as error:
            print(f'InstantAction{number}: FAILED: {error}')
            failed = True
            continue
        lines = report_lines(number, world, report)
        if args.compare:
            path = other_world(args.compare, number)
            if path is None:
                print(f'InstantAction{number}: nothing to compare with in {args.compare}')
                failed = True
                continue
            with open(path, 'rb') as file:
                theirs = file.read()
            try:
                same, result = comparing.compare(world, theirs)
            except WorldError as error:
                print(f'InstantAction{number}: cannot compare with {path}: {error}')
                failed = True
                continue
            print(f'InstantAction{number} against {os.path.basename(path)}: '
                  + ('all sections identical' if same else 'differences'))
            print('\n'.join(result))
            continue
        folder = os.path.join(args.out, 'Worlds', 'Release')
        os.makedirs(folder, exist_ok=True)
        target = os.path.join(folder, f'InstantAction{number}.World00p')
        with open(target, 'wb') as file:
            file.write(world)
        with open(os.path.join(folder, f'InstantAction{number}.report.txt'), 'w', encoding='utf-8') as file:
            file.write('\n'.join(lines) + '\n')
        print('\n'.join(shortened(lines)))
        print(f'  written: {target}')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
