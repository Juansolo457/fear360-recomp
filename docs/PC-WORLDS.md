# Instant Action worlds for the PC game

`pc/tools/convert_ia.py` turns the Xbox 360 game's four Instant Action maps into worlds the PC game (v1.08) loads. It runs on the player's machine from the player's own copies of both games. Nothing from either game is in the repository, and the converted worlds must never be committed (the tool refuses an output folder that git tracks).

The format knowledge here was learned by reading the installer of the mod "F.E.A.R. Exclusive Console Content", which did this conversion in Go. The converter is our own Python (`pc/tools/iaworld/`); the mod's output was used only to check ours against.

## Files this applies to

- 360: title `565507D9`, `default.xex` SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792`. The maps are `Game/Worlds/Release/InstantAction1` to `InstantAction4`, each a `.wld`, a `.wldclnt` and a `.wldsrvr`. The mod called them Distribution, Rooftop, Construction and Vault, in that order.
- PC: v1.08. The tool reads `GameServer.dll` (SHA-256 `a5856334d5013bb6d8d49eb40717a799d327e30e1f242346d178c86eb9649c7d`, from `FEARE_8.Arch00`), the stock worlds under `Worlds\Release` and the game database, all from the `.Arch00` archives in the order `Default.archcfg` mounts them.

## Running it

```
python -I pc/tools/convert_ia.py --x360 <360 Game folder> --pc <PC game folder> [--out dir] [--map N]
```

`--pc` defaults to `game_dir` in `private/pc.toml`, `--out` to `build/pc/ia`. The output is `Worlds/Release/InstantAction<N>.World00p` with an `InstantAction<N>.report.txt` beside it. The console shows a short summary per map; the report file has every list in full. The four maps take a few seconds.

The PC needs no companion file. Stock single-player worlds mostly have an `.Asset00` text file beside them (a longer list of assets to load), but three stock worlds have none, and the mod shipped none. We do not write one.

## The formats

All three 360 files are big-endian and version `0x71`. The PC world is little-endian, also version `0x71`, and holds the same data in one file.

A PC `.World00p` is five 32-bit words, then five sections:

| Section | Comes from | Contents |
| --- | --- | --- |
| header | | version, then the offsets of the render, sector, object and blind sections |
| core | `.wld` (magic `PDLW`) | world box, world tree, surface tables, world models, blockers, physics shapes, precache list, particle blockers |
| render | `.wldclnt` (magic `CDLW`), first part | meshes (vertices, indices, vertex declarations, sections, material names) and visibility blocks |
| sectors | `.wldclnt`, the rest | portals, named sectors, lists |
| objects | `.wldsrvr` (magic `SDLW`) | every placed object: class and properties |
| blind | end of the `.wld` | per-object binary data: keyframer paths, breakable geometry, the AI navigation mesh (version 6) |

Each module's header comment gives its part field by field. What follows is what differs between the two machines, which is what the converter has to change.

**Core.** Almost all of it is 32-bit words (ints and floats), copied with the bytes reversed. Floats are never decoded, so nothing is rounded. The exceptions:
- The world tree's quadtree is a bit stream and is copied as it is.
- Eight counts after the tree are stored plain on the 360. The PC stores each XORed with a constant (the version plus the byte values of "FEAR").
- A world model has byte-sized fields per polygon, and 16-bit surface flags.
- A physics mesh shape has 16-bit indices when it has fewer than 65536 vertices. It ends in Havok MOPP code, which is little-endian on the 360 too and is copied as it is.
- The precache list is rebuilt. See below.

**Precache list.** The world lists every resource it wants loaded, as folder and file name pairs. The 360 header has twelve words and the PC's eleven. The names differ: `.tex` is `.dds` on PC, `.mat` is `.Mat00`, `.mdl` is `.Model00p`, `.txanm` is `.TxAnm00`, `.vid` is `.bik`. Compiled shaders, wave banks, lip-sync and baked meshes exist only on the console and are left out. The 360 keeps some content under `Prototype\` and `Prototype\FX\` that the PC has at the root. Each name is looked up in the PC's archives, written with the PC's spelling, and left out (and reported) if the PC has no such file.

**Render.** Words and 16-bit indices are reversed. The vertices are rebuilt:
- The 360 packs a normal, tangent or binormal into one word (declaration type 14: three signed 10-bit fields, 511 is 1.0). The PC has three floats. So a PC vertex is larger, and every offset in the vertex declarations changes.
- Sections that share a base vertex and a declaration share vertices, so they are rebuilt as one block and their first-vertex and base-vertex fields are renumbered.
- A PC material can read a vertex input that the 360 build of the material did not have. The PC does not draw a section whose vertices lack an input its material reads. The converter learns what each PC material reads from the PC's own worlds: it reads every stock world under `Worlds\Release` and notes the inputs of each material's sections. Where a 360 block lacks one, it is made up: white for a vertex colour; a normal from the triangles' face normals; a tangent and binormal from the texture coordinates across each triangle, squared up against the normal.
- Material names change from `.mat` to `.Mat00`.

On these four maps the only input that had to be made up is a vertex colour, for four building materials, on maps 2 and 3 (3705 and 1788 vertices). Every material the maps use is used by some stock PC world, and no material has differing inputs between stock worlds.

**Sectors.** Words and 16-bit values reversed; the sector names are text.

**Objects.** The layout is the same on both machines apart from byte order (see `objects.py`). The classes are not the same, so each object is rebuilt:
- A 360 property the PC class does not have is dropped.
- A PC property the 360 object does not have is added. Its value comes from the object of the same class and name in the stock world `WTF_Exfil` if there is one, else the value most objects of that class have there, else the class's default in `GameServer.dll`.
- The properties are written in the order the PC's own world has them.
- A property that names a file gets the PC's name for the file.

The class tables in `GameServer.dll` have no directory. `classdefs.py` finds them by their shape in the `.data` section; its header comment gives the record layout. It finds 221 classes in the v1.08 DLL, and every class the four maps use is among them.

The largest class difference is `CommandObject`: the 360 class has 8 tracks of 50 timed events, the PC's has 6 tracks of 10.

**Blind data.** A table of entries (size, type, offset) over a blob. Keyframer paths and breakable geometry are words; a keyframer entry ends in text. The navigation mesh is twelve counted arrays of words, then a block of node names, then more words.

## Content fixes

The converter keeps the 360's own scripting. In particular each map still ends with the 360's `exitlevel` message, which our game code expects. The mod's rule edits are not applied: it replaced `exitlevel` with mission-failed messages carrying its own codes, added a countdown timer object and a start-up object to each map, and changed Vault's trigger. Our game code handles the mode instead.

The mod's content fixes were considered one by one.

| Fix | Applied | Reason |
| --- | --- | --- |
| Machine pistol pickups | Yes, differently | The PC has no machine pistol. The pickups would name a weapon the PC database lacks. See the open decision below. |
| File name typo (a model path with a doubled dot, map 1) and a sound path with a leading backslash (map 3) | Yes | The names do not resolve otherwise. The rule is general: a name that does not resolve is tried without leading backslashes and with doubled dots collapsed, and used if that file exists. It is also applied to the precache list, which the mod did not do. |
| Events in `CommandObject` slots the PC lacks (map 2, two objects) | Yes, as a general rule | The PC class has no seventh track, so the event would be dropped. In both objects it gives freshly spawned soldiers their orders. An event carries its own time, so it can sit in any slot: it moves to the first free slot at the end of the PC track whose last event is nearest before it. That picks the same slot the mod chose by hand. No other non-empty event on the four maps is in a slot the PC lacks. |
| Looping voice lines (one each on maps 1, 2 and 3) | Loop turned off | The line is set to loop, and per the mod's notes it repeats without end on PC. A voice line that never stops is a play defect our mode code cannot handle. Only these three objects are touched; other looping sounds under `Voice` (telephone messages) are meant to loop. |
| Construction's opening line: also start it off, not on | No | With the loop off this should at worst play the line twice. To be judged in a play test. |
| Rooftop gate commands | No (optional: `--fix rooftop-gates`) | On the 360, clearing an area unlocks a trigger and the gate opens when the player walks into it. The mod made the gate open as soon as the area is clear. The PC `Trigger` class has the `Locked` property and nothing in the files shows the 360's way cannot work, so the 360's scripting is kept. If a play test shows the gates stay shut, the option applies the mod's change. |
| Start-up weapon commands (maps 1 and 3) | No | The mod removed a shotgun grant on map 3 because its own mission record granted one, and re-granted three default weapons on map 1 after the script clears the inventory, to match the 360's database. Both belong with the mode's game code and database, not the world. Our game code must give the 360's default weapons after map 1's inventory reset. |
| Vault: two Watson shotgun pickups from the PlayStation 3 world | No | The PC has no Watson shotgun, and we use no PS3 files. The 360's own pickups stay. |

**Open decision: what the machine pistol pickups become.** The 360 names these pickups with three records, `Platform_MachPist_Pistol`, `Platform_MachPist_SMG` and `Platform_MachPist_AssRifle`. All three are the machine pistol on the 360; the suffix reads as the weapon the pickup is on a machine without one. The converter follows the suffix: Pistol, Submachinegun, Assault Rifle. The pickup's model is set to the model that weapon's pickups have in the stock world. Counts: map 1 has 4 Pistol and 2 Assault Rifle, map 2 has 6 Submachinegun, map 3 has 5 Pistol and 3 Submachinegun, map 4 has 5 Pistol and 6 Submachinegun. If we later add the machine pistol to the PC build, `--skip-fix machine-pistol` leaves the pickups as the 360 has them.

## What the PC lacks

The tool reports this per map. Nothing is dropped silently. For the four maps, with a stock PC game:

- **Classes:** none missing. No object is left out (1288, 2108, 2034 and 1650 objects).
- **Materials:** none missing.
- **Files named by objects:** none missing, after the fixes above.
- **Weapons:** none missing, after the machine pistol fix. The check is a search for the weapon's name in the PC game database, not a parse of it.
- **Precache list:** 27 to 31 names per map are console-only kinds. 19 to 24 names per map have no PC file: the machine pistol's and Watson shotgun's models, skins and HUD icons; a console font, the map's own sound bank, an engine texture and a placeholder sound; one sky texture (map 1) and one black texture; and two to six bare names per map with no folder or extension, which are not files.
- **Properties:** thousands are dropped per map, nearly all of them the empty `CommandObject` slots the PC class lacks. The ones that held a value are the `CommandObject` editor layout string, two flags on a few `Prop` objects (`On` and `MPClientOnlyRigidBody`), and `Cinematic` on map 3's camera.

## Checked against the mod

`--compare <folder>` converts and compares with another converter's worlds, section by section, and writes nothing. For the object and core sections it also says how they differ.

Two things make our normal output differ from the mod's on purpose. The mod's folder supplied the console weapons' files, so its precache lists name them. And our converter finds 360 `Prototype\FX\` names at the PC's root and repairs misspelt precache names, which the mod did not. To compare like with like, `--extra <the mod's folder>` counts the mod's loose files as present and `--mod-names` turns our two additions off.

Run that way against the mod's output on this PC (built with a PS3 copy present), for all four maps:

- **core, render, sectors, blind:** byte for byte identical.
- **objects:** differ only in the edits we left out or made differently. Map 1: 7 of 1288 objects (6 machine pistol pickups, the start-up weapon command). Map 2: 8 of 2108 (6 pickups, the two gate counters). Map 3: 10 of 2034 (8 pickups, the start-up weapon command, the opening line's start flag). Map 4: 12 of 1650 (11 pickups, the PS3 Watson pickup).
- With `--fix rooftop-gates`, map 2's difference is the 6 pickups only.

Run normally (no `--extra`, no `--mod-names`), the core differs only inside the precache list: the mod has 14 names of files only it supplied, and we have 6 to 13 more per map that the PC has under another folder or spelling.

So the conversion of geometry, physics, render data, navigation and object properties is the same as a converter whose maps were played on PC. The made-up vertex colours match too, although we work out which materials need them from the stock worlds and the mod had a fixed list.

## Not verified

- **No converted world has been loaded in the PC game by us.** The tool reads its own output back the way the loader walks it (render section, object list, precache list), and that is all.
- The content fixes are judged from the files, not from play. The voice loops, the doubled opening line on map 3 and the Rooftop gates need a play test.
- The moved `CommandObject` events assume the PC runs an event at its own time whichever slot holds it.
- The extra precache names (the `Prototype\FX\` rule) rest on the names matching; the files were not compared.
- Whether the PC needs an `.Asset00` file for anything beyond load-time hitches is not known.
- File names inside command strings and in properties with other endings (effects, for one) are not checked against the PC's files.
- The 360's per-map texture and material catalogues (`.texCat`, `.matCat`) are not converted. The PC's own materials and textures are used. Where the 360's art differs, the PC's is what shows.
- Only the four Instant Action maps were run. Other 360 worlds may have blind data types, shape kinds or streaming tables the tool stops on.
- Only this PC copy and this 360 release were tried.
