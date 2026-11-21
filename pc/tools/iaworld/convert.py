"""Object conversion, the content fixes, and the assembly of one PC world.

A PC .World00p is five 32-bit words (version 0x71 and the offsets of the render, sector,
object and blind data sections) followed by the core section and those four.

Objects
  The 360 and PC builds of a class do not have the same properties. A converted object
  gets exactly the PC class's properties, in the order the PC's own worlds have them:
    - a 360 property the PC class lacks is dropped (and counted in the report);
    - a PC property the 360 object lacks takes its value from the object of the same class
      and name in a stock PC world if there is one, else the class's usual value there,
      else the default in GameServer.dll;
    - a property that names a file gets the PC's name for that file.
  The stock world is WTF_Exfil (the choice of the mod this was checked against).

Render inputs
  Which vertex inputs each PC material reads is taken from the PC's own worlds: every
  single-player world in the archives is read and the inputs of each material's sections
  noted (see render.py for why this matters).

Content fixes are listed in FIXES below; docs/PC-WORLDS.md gives the reasons, and the ones
left out on purpose.
"""
import re
import struct

from . import WorldError
from . import classdefs, core, render
from . import objects as O
from .pcfiles import CONSOLE_ONLY, extension_of

REFERENCE_WORLD = 'Worlds\\Release\\WTF_Exfil.World00p'
GAME_DATABASE = 'Database\\FEAR.Gamdb00p'
STOCK_WORLDS = 'worlds\\release\\'

# A text property ending in one of these names a file.
FILE_ENDINGS = ('.tex', '.mdl', '.mat', '.txanm', '.wav', '.model00p', '.model00c', '.dds', '.mat00')

# The 360's machine pistol pickups. The record name says which stock weapon the pickup
# stands in for on a machine without the machine pistol.
MACHINE_PISTOL_PREFIX = 'Platform_MachPist_'
MACHINE_PISTOL_STAND_IN = {'Pistol': 'Pistol', 'SMG': 'Submachinegun', 'AssRifle': 'Assault Rifle'}

# Spoken lines that are set to loop, by map: (object name, sound in lower case). Other
# looping sounds under Voice (ringing telephones' messages) are meant to loop and are left.
LOOPING_VOICE_LINES = {
    1: (('SoundFX00', r'voice\21270.wav'),),
    2: (('Start_SFX', r'voice\14093r.wav'),),
    3: (('SoundFX00', r'voice\14012r.wav'),),
}

# Rooftop's two gates, for the optional rooftop-gates fix: (counter, the locked trigger it
# unlocks). See fix_rooftop_gates.
ROOFTOP_GATES = (('LastRoofEventCounter00', 'Exit01_Tr01'), ('EndHall_DeathCounter', 'Exit00_Tr00'))

EVENT_SLOT = re.compile(r'EventT(\d+)E(\d+)$')

FIXES = {
    'machine-pistol': 'machine pistol pickups become the stock weapon their record names',
    'event-slots': 'CommandObject events in slots the PC class lacks move to free slots',
    'voice-loop': 'three spoken lines set to loop play once',
    'file-names': 'misspelt file names are repaired when the repaired name exists',
}
# Not applied unless asked for.
OPTIONAL_FIXES = {
    'rooftop-gates': 'Rooftop: clearing an area opens its gate at once, without the trigger',
}


class Report:
    """What happened to one map: fixes applied and everything the PC lacks."""

    def __init__(self):
        self.fixes = []                 # (fix id, text)
        self.console_only = {}          # precache: extension -> count
        self.precache_missing = []      # precache names with no PC file
        self.precache_kept = 0
        self.precache_total = 0
        self.files_missing = []         # (object label, property, value)
        self.classes_missing = {}       # class -> objects dropped
        self.dropped = {}               # (class, property) -> [count, count with a value]
        self.unwritable = []            # (class, property)
        self.weapons_missing = {}       # weapon type -> pickups
        self.events_lost = []           # (object label, property)
        self.materials_missing = []     # materials the render data names that the PC lacks
        self.materials_unknown = []     # materials no stock PC world uses (inputs unknown)
        self.render = {}
        self.blind = {}
        self.objects_in = 0
        self.objects_out = 0

    def fix(self, fix_id, text):
        self.fixes.append((fix_id, text))


class PcReference:
    """Everything read from the PC game, shared by the maps."""

    def __init__(self, files):
        self.files = files
        world = files.read(REFERENCE_WORLD)
        reference_objects = O.parse_pc_world(world, REFERENCE_WORLD)
        del world
        # Vertex inputs per material, from every stock single-player world.
        self.material_inputs = {}
        for key, (archive, entry) in sorted(files.source.items()):
            if archive is None or not key.startswith(STOCK_WORLDS) or not key.endswith('.world00p'):
                continue
            for material, inputs in render.material_inputs(archive.read(entry), entry[0]).items():
                self.material_inputs.setdefault(material, set()).update(inputs)
        self.database = files.read(GAME_DATABASE).lower()
        self._build_schema(reference_objects, classdefs.load(files.read('GameServer.dll')))
        # Objects of the reference world by class and name; the last of a name is used.
        self.named = {}
        for obj in reference_objects:
            self.named[(obj.cls, obj.name)] = {name: (kind, value) for name, kind, value in obj.props}
        # The pickup model each weapon type has in the reference world.
        self.weapon_models = {}
        for obj in reference_objects:
            if obj.cls == 'WeaponItem' and obj.text('WeaponType') and obj.text('Filename'):
                self.weapon_models.setdefault(obj.text('WeaponType').lower(), obj.text('Filename'))

    def _build_schema(self, reference_objects, dll_classes):
        self.order = {}      # class -> [(property, type)] in PC order
        self.usual = {}      # class -> {(property, type): value}
        tallies = {}
        for obj in reference_objects:
            order = self.order.setdefault(obj.cls, [])
            tally = tallies.setdefault(obj.cls, {})
            for name, kind, value in obj.props:
                key = (name, kind)
                if key not in tally:
                    tally[key] = {}
                    order.append(key)
                tally[key][value] = tally[key].get(value, 0) + 1
        for cls, tally in tallies.items():
            # The commonest value; of equally common ones, the first met.
            self.usual[cls] = {key: max(values, key=values.get) for key, values in tally.items()}
        for cls, properties in dll_classes.items():
            order = self.order.setdefault(cls, [])
            usual = self.usual.setdefault(cls, {})
            for name, kind, default in properties:
                if (name, kind) not in usual:
                    order.append((name, kind))
                    usual[(name, kind)] = default    # None: a default that cannot be written

    def has_weapon(self, weapon_type):
        """Whether the PC game database names this weapon (a search for the name, not a parse)."""
        return weapon_type.lower().encode('latin-1') + b'\0' in self.database


def _is_file_name(kind, value):
    return kind in (O.STRING, O.COMMAND) and ';' not in value and value.lower().endswith(FILE_ENDINGS)


# --- content fixes, applied to the 360 objects before conversion ---

def fix_machine_pistols(objects, pc, report):
    swapped = {}
    for obj in objects:
        weapon = obj.text('WeaponType') if obj.cls == 'WeaponItem' else None
        if not weapon or not weapon.startswith(MACHINE_PISTOL_PREFIX):
            continue
        stand_in = MACHINE_PISTOL_STAND_IN.get(weapon[len(MACHINE_PISTOL_PREFIX):])
        if not stand_in or not pc.has_weapon(stand_in):
            continue        # left as it is; reported as a weapon the PC lacks
        obj.set('WeaponType', stand_in)
        model = pc.weapon_models.get(stand_in.lower())
        if model and obj.get('Filename'):
            obj.set('Filename', model)
        swapped[(weapon, stand_in)] = swapped.get((weapon, stand_in), 0) + 1
    for (weapon, stand_in), count in sorted(swapped.items()):
        report.fix('machine-pistol', f'{count} x {weapon} pickup -> {stand_in}')


def _event_time(value):
    head = value.partition('|')[0]
    try:
        return float(head)
    except ValueError:
        return None


def fix_event_slots(objects, pc, report):
    """Moves CommandObject events out of slots the PC class does not have.

    The 360 class has 8 tracks of 50 events and the PC's 6 of 10. An event is "time|commands",
    so which slot holds it does not change when it runs. It goes to the first free slot at
    the end of the PC track whose last event is nearest before it in time.
    """
    pc_slots = set()
    for name, kind in pc.order.get('CommandObject', ()):
        slot = EVENT_SLOT.match(name)
        if slot and kind == O.COMMAND:
            pc_slots.add((int(slot.group(1)), int(slot.group(2))))
    if not pc_slots:
        return
    for obj in objects:
        if obj.cls != 'CommandObject':
            continue
        homeless, used = [], {}
        for name, kind, value in obj.props:
            slot = EVENT_SLOT.match(name)
            if not slot or kind != O.COMMAND:
                continue
            where = (int(slot.group(1)), int(slot.group(2)))
            if where in pc_slots:
                if value:
                    used[where] = value
            elif value.partition('|')[2].strip():
                homeless.append((name, value))
        for name, value in homeless:
            time = _event_time(value)
            best = None
            for track in sorted({track for track, _ in pc_slots}):
                events = [event for (t, event) in used if t == track]
                free = max(events) + 1 if events else 0
                if (track, free) not in pc_slots or time is None:
                    continue
                last = _event_time(used[(track, free - 1)]) if events else None
                if events and (last is None or last > time):
                    continue
                gap = time - last if events else float('inf')
                if best is None or gap < best[0]:
                    best = (gap, track, free)
            if best is None:
                report.events_lost.append((obj.label(), name))
                continue
            target = f'EventT{best[1]}E{best[2]}'
            if obj.get(target):
                obj.set(target, value)
            else:
                obj.props.append((target, O.COMMAND, value))
            obj.set(name, '')
            used[(best[1], best[2])] = value
            report.fix('event-slots', f'{obj.label()}: {name} -> {target}')


def fix_voice_loops(number, objects, pc, report):
    """Three spoken lines are set to loop; on PC a looping line repeats without end."""
    for obj in objects:
        if obj.cls != 'SoundFX' or obj.get('Loop') != (O.BOOL, 1):
            continue
        sound = (obj.text('Sound') or '').replace('/', '\\').lstrip('\\').lower()
        if (obj.name, sound) in LOOPING_VOICE_LINES.get(number, ()):
            obj.set('Loop', 0)
            report.fix('voice-loop', f'{obj.label()}: Loop off')


def fix_rooftop_gates(number, objects, pc, report):
    """Optional. On the 360, clearing each of two areas unlocks a trigger, and the gate opens
    when the player then walks into it. This makes the counter that unlocks the trigger
    run the trigger's commands itself, so the gate opens as soon as the area is clear.
    """
    if number != 2:
        return
    by_name = {}
    for obj in objects:
        by_name.setdefault(obj.name, []).append(obj)
    for counter_name, trigger_name in ROOFTOP_GATES:
        counters, triggers = by_name.get(counter_name, []), by_name.get(trigger_name, [])
        unlock = f'msg {trigger_name} unlock;'
        if (len(counters) != 1 or len(triggers) != 1 or triggers[0].get('Locked') != (O.BOOL, 1)
                or not (counters[0].text('Event1IncToValCmd') or '').startswith(unlock)):
            report.fix('rooftop-gates', f'{counter_name}: NOT applied, the objects are not as expected')
            continue
        commands = ''.join(triggers[0].text(f'Command{n}') or '' for n in range(1, 11))
        counters[0].set('Event1IncToValCmd', counters[0].text('Event1IncToValCmd')[len(unlock):] + commands)
        report.fix('rooftop-gates', f'{counters[0].label()} runs the commands of {triggers[0].label()}')


def _repairs(path):
    """Other spellings to try for a file name that does not resolve."""
    stripped = path.lstrip('\\/')
    if stripped != path:
        yield stripped
    if '..' in stripped:
        yield stripped.replace('..', '.')


# --- conversion ---

def convert_objects(objects, pc, report, repair_names=True):
    out = []
    repaired = {}
    for obj in objects:
        order = pc.order.get(obj.cls)
        if order is None:
            report.classes_missing[obj.cls] = report.classes_missing.get(obj.cls, 0) + 1
            continue
        usual = pc.usual[obj.cls]
        kept = {}
        for name, kind, value in obj.props:
            if (name, kind) not in usual:
                entry = report.dropped.setdefault((obj.cls, name), [0, 0])
                entry[0] += 1
                if value not in ('', 0):
                    entry[1] += 1
                continue
            if _is_file_name(kind, value):
                hit = pc.files.resolve(value)
                if hit is None and repair_names:
                    for other in _repairs(value):
                        hit = pc.files.resolve(other)
                        if hit:
                            repaired[(value, hit)] = repaired.get((value, hit), 0) + 1
                            break
                if hit is None:
                    report.files_missing.append((obj.label(), name, value))
                else:
                    value = hit
            kept[name] = (kind, value)
        same_name = pc.named.get((obj.cls, obj.name)) if obj.text('Name') is not None else None
        props = []
        for name, kind in order:
            if name in kept:
                props.append((name,) + kept.pop(name))
            elif same_name and name in same_name and same_name[name][0] == kind:
                props.append((name, kind, same_name[name][1]))
            elif usual[(name, kind)] is None:
                report.unwritable.append((obj.cls, name))
            else:
                props.append((name, kind, usual[(name, kind)]))
        props += [(name,) + entry for name, entry in kept.items()]
        out.append(O.WorldObject(obj.cls, props))
        if obj.cls == 'WeaponItem':
            weapon = obj.text('WeaponType')
            if weapon and not pc.has_weapon(weapon):
                report.weapons_missing[weapon] = report.weapons_missing.get(weapon, 0) + 1
    for (old, new), count in sorted(repaired.items()):
        report.fix('file-names', f'{old} -> {new} ({count} x)')
    return out


def convert_precache(pairs, pc, report, repair_names=True):
    kept, seen = [], set()
    repaired = 0
    report.precache_total = len(pairs)
    for folder, name in pairs:
        extension = extension_of(name)
        if extension in CONSOLE_ONLY:
            report.console_only[extension] = report.console_only.get(extension, 0) + 1
            continue
        path = f'{folder}\\{name}' if folder else name
        hit = pc.files.resolve(path)
        if hit is None and repair_names:
            hit = next(filter(None, map(pc.files.resolve, _repairs(path))), None)
            repaired += hit is not None
        if hit is None:
            report.precache_missing.append(path)
            continue
        pc_folder, _, pc_name = hit.rpartition('\\')
        key = (pc_folder.lower(), pc_name.lower())
        if key not in seen:
            seen.add(key)
            kept.append((pc_folder, pc_name))
    report.precache_kept = len(kept)
    if repaired:
        report.fix('file-names', f'{repaired} misspelt names in the precache list')
    return core.pc_precache(kept)


def sections(world, what='PC world'):
    """A PC world's five sections as {name: bytes}, in file order."""
    if len(world) < 20:
        raise WorldError(f'{what}: too short to be a PC world')
    version, *offsets = struct.unpack_from('<5I', world, 0)
    if version != 0x71:
        raise WorldError(f'{what}: version 0x{version:X}, expected 0x71')
    bounds = [20] + offsets + [len(world)]
    if any(a > b for a, b in zip(bounds, bounds[1:])):
        raise WorldError(f'{what}: section offsets are out of order')
    names = ('core', 'render', 'sectors', 'objects', 'blind')
    return {name: world[bounds[i]:bounds[i + 1]] for i, name in enumerate(names)}


def convert_map(number, wld, clnt, srvr, pc, skip=(), extra_fixes=()):
    """Builds one PC world. Returns (bytes, Report).

    `skip` names fixes to leave out, `extra_fixes` optional ones to apply.
    """
    report = Report()
    label = f'InstantAction{number}'
    objects = O.parse_console(srvr, f'{label}.wldsrvr')
    report.objects_in = len(objects)
    if 'machine-pistol' not in skip:
        fix_machine_pistols(objects, pc, report)
    if 'event-slots' not in skip:
        fix_event_slots(objects, pc, report)
    if 'voice-loop' not in skip:
        fix_voice_loops(number, objects, pc, report)
    if 'rooftop-gates' in extra_fixes:
        fix_rooftop_gates(number, objects, pc, report)
    # The mod repaired names in objects only; mod-names mode keeps to that.
    repair_names = 'file-names' not in skip
    converted = convert_objects(objects, pc, report, repair_names)
    repair_names = repair_names and pc.files.fx_prefix
    report.objects_out = len(converted)
    object_section = O.serialise(converted)

    core_section, blind_at = core.convert_core(
        wld, lambda pairs: convert_precache(pairs, pc, report, repair_names), f'{label}.wld')
    blind_section, report.blind = core.convert_blind(wld, blind_at, f'{label}.wld')

    render_section, sector_section, report.render = render.convert_render(
        clnt, pc.material_inputs, f'{label}.wldclnt')
    report.materials_unknown = sorted(
        name for name in report.render['materials'] if name.lower() not in pc.material_inputs)
    report.materials_missing = sorted(
        name for name in report.render['materials'] if pc.files.find(name) is None)

    parts = (core_section, render_section, sector_section, object_section, blind_section)
    offsets, at = [], 20
    for part in parts[:-1]:
        at += len(part)
        offsets.append(at)
    world = struct.pack('<5I', 0x71, *offsets) + b''.join(parts)

    # Read the result back the way the PC's loader walks it.
    _h, _m, _g, render_end = render.read_render(world, offsets[0], False, f'{label} (converted)')
    if render_end != offsets[1]:
        raise WorldError(f'{label}: the converted render data does not read back')
    if len(O.parse_pc_world(world, f'{label} (converted)')) != len(converted):
        raise WorldError(f'{label}: the converted objects do not read back')
    core.read_pc_precache(world, f'{label} (converted)')
    return world, report
