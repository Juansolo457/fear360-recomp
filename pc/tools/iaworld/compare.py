"""Compares a converted world with another converter's world, section by section.

Used to check this converter against the output of the mod it learned the formats from.
For the two sections that are expected to differ in known ways it also says how: the
precache list by name, the objects by property.
"""
from . import WorldError
from . import core
from . import objects as O
from .convert import sections


def first_difference(a, b):
    """The offset of the first differing byte, or None when a and b are equal."""
    if a == b:
        return None
    limit = min(len(a), len(b))
    low, high = 0, limit
    # Halve the range holding the first difference.
    while high - low > 1:
        middle = (low + high) // 2
        if a[low:middle] == b[low:middle]:
            low = middle
        else:
            high = middle
    if low < limit and a[low] != b[low]:
        return low
    return limit


def _precache_lines(ours, theirs):
    lines = []
    try:
        our_pairs, our_start, our_end = core.read_pc_precache(ours, 'our world')
        their_pairs, their_start, their_end = core.read_pc_precache(theirs, 'their world')
    except WorldError as error:
        return [f'    (precache lists not compared: {error})']
    our_start, our_end, their_start, their_end = our_start - 20, our_end - 20, their_start - 20, their_end - 20
    our_core, their_core = sections(ours)['core'], sections(theirs)['core']
    rest_same = (our_core[:our_start] == their_core[:their_start]
                 and our_core[our_end:] == their_core[their_end:])
    lines.append('    outside the precache list: ' + ('identical' if rest_same else 'DIFFERENT'))
    if our_pairs == their_pairs:
        lines.append('    precache list: same names')
        return lines
    ours_set = {(a.lower(), b.lower()) for a, b in our_pairs}
    theirs_set = {(a.lower(), b.lower()) for a, b in their_pairs}
    only_ours = [pair for pair in our_pairs if (pair[0].lower(), pair[1].lower()) not in theirs_set]
    only_theirs = [pair for pair in their_pairs if (pair[0].lower(), pair[1].lower()) not in ours_set]
    lines.append(f'    precache list: {len(our_pairs)} names here, {len(their_pairs)} there')
    for title, pairs in (('only here', only_ours), ('only there', only_theirs)):
        for folder, name in pairs:
            lines.append(f'      {title}: {folder}\\{name}')
    if not only_ours and not only_theirs:
        lines.append('      same names, in another order or spelling')
    return lines


def _object_lines(ours, theirs, limit):
    try:
        our_objects = O.parse_pc_world(ours, 'our world')
        their_objects = O.parse_pc_world(theirs, 'their world')
    except WorldError as error:
        return [f'    (objects not compared: {error})']
    lines = []
    if len(our_objects) != len(their_objects):
        lines.append(f'    {len(our_objects)} objects here, {len(their_objects)} there')
    differing_objects = 0
    details = []
    for a, b in zip(our_objects, their_objects):
        if a.cls == b.cls and a.props == b.props:
            continue
        differing_objects += 1
        if a.cls != b.cls:
            details.append(f'{a.label()}: class there is {b.cls}')
            continue
        if [p[:2] for p in a.props] != [p[:2] for p in b.props]:
            details.append(f'{a.label()}: property lists differ')
        theirs_by_name = {name: value for name, _kind, value in b.props}
        for name, _kind, value in a.props:
            if name in theirs_by_name and theirs_by_name[name] != value:
                details.append(f'{a.label()}.{name}: here {value!r}, there {theirs_by_name[name]!r}')
    lines.append(f'    {differing_objects} of {min(len(our_objects), len(their_objects))} objects differ')
    for text in details[:limit]:
        lines.append('      ' + (text if len(text) <= 400 else text[:400] + '...'))
    if len(details) > limit:
        lines.append(f'      ... and {len(details) - limit} more differences')
    return lines


def compare(ours, theirs, detail_limit=60):
    """Returns (all identical, report lines)."""
    our_sections, their_sections = sections(ours, 'our world'), sections(theirs, 'their world')
    lines = []
    all_same = True
    for name in our_sections:
        a, b = our_sections[name], their_sections[name]
        at = first_difference(a, b)
        if at is None:
            lines.append(f'  {name:8} identical ({len(a)} bytes)')
            continue
        all_same = False
        lines.append(f'  {name:8} DIFFERS at offset 0x{at:X} of the section ({len(a)} bytes here, {len(b)} there)')
        if name == 'core':
            lines += _precache_lines(ours, theirs)
        elif name == 'objects':
            lines += _object_lines(ours, theirs, detail_limit)
    return all_same, lines
