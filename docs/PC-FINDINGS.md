# PC F.E.A.R. findings

What milestone 1 of [PC-PLAN.md](PC-PLAN.md) found about the PC game and its v1.08 public tools, and what it read from the 360 game for the controller. Status and how to run: [PC-PROGRESS.md](PC-PROGRESS.md).

## Binaries these apply to

| File | SHA-256 |
| --- | --- |
| `FEAR.exe` 1.08.282.0, GOG release (no Steam wrapper) | `c1678aa4df37e87c097f45d8eb68a7c379d99ad12d8da2771cf6235d9493d0b2` |
| `FEAR.exe` 1.08.282.0, Steam release (`.bind` section: Steam wrapper) | `d5ebc38a4f12b772c9112a2811c290adb6c5052d3bc2f817302d38cf55bb2cbe` |
| `GameClient.dll` 1.08, from `FEARE_8.Arch00` | `38604fbfbc91d27427368ff93739816b85365eb6d3666990d16ab9a876237db8` |
| `GameServer.dll` 1.08, from `FEARE_8.Arch00` | `a5856334d5013bb6d8d49eb40717a799d327e30e1f242346d178c86eb9649c7d` |
| `ClientFx.fxd` 1.08, from `FEARE_8.Arch00` | `d52c27dfa8d7ec766675c457bfd8b6134eb1184aa1e64c8ab42e839af9df4f26` |
| `fear_publictools_108.exe` (Steam copy, `extras\`) | `11aaa4128528403f7bc9ea5119c68051c62b92a99e6411dfd749af55e9b19df8` |
| 360 `default.xex` (title `565507D9`), for the 360 data below | `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792` |

Both stores install Extraction Point and Perseus Mandate as `FEARXP\` and `FEARXP2\` inside the base game's folder (Steam lists them as apps 21110 and 21120 with no files of their own).

## The public tools source

- The installer is an InstallScript 11.5 setup: no administrative (extract-only) install, and a silent run needs a recorded answer file. Its self-extracting stub takes `/extract_all:<dir>`, which leaves `Disk1\data1.hdr` and four cabinets (`data2.cab` holds almost everything). `pc/tools/isextract.py` unpacks them and checks each file's MD5: 13,659 files, all matching, apart from the installer's own three engine files, which are stored twice.
- The game code is `Game\` (solution `Game.sln`, VS .NET 2003 projects, `.vcproj` 7.10), with `engine\sdk\inc`, `engine\sdk\lib\win\<config>` (prebuilt `Shared_CRC.lib`, `Shared_Assert.lib`, `EngineServer.lib`) and `libs\`. `pc/tools/setup.ps1` keeps those three folders; their git tree is `73e65f11e15ea4acf9adb282c785bd468c7d7fd1`.
- The projects build with VS 2022's x86 compiler (19.44) given the 2003 compiler's language defaults: `/Zc:wchar_t-`, `/Zc:forScope-`, `/permissive`, no exceptions (`_HAS_EXCEPTIONS=0`), RTTI on, shared runtime. Source fixes needed: string literals written against a macro (`"*."RESEXT_TEXTUREANIM`, which C++11 reads as a literal suffix) and one `L''`. `<hash_map>` needs `_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS`, and `ctype.h`/`<string>` are forced in because the 2003 headers included them. `Shared_Assert.lib` imports `_snprintf` from `msvcr71.dll`, which the Universal CRT has only inline; `pc/game/src/crt_shims.c` supplies the import slot.
- Retail `GameClient.dll` and `GameServer.dll` also export a global named after a GUID (`?_0x33560e70...@@3HA`). Nothing in the source defines it, and neither `FEAR.exe`, `EngineServer.dll` nor `GameDatabase.dll` contain its name, so the engine doesn't look it up. Builds without it run.

## Running our DLLs under retail FEAR.exe

- **The C++ boundary.** The engine was built with VS .NET 2003 and its interfaces are plain virtual classes, so a DLL from VS 2022 works with one exception: interfaces that pass `std::string` by value or in a struct, because the two libraries lay it out differently. In the SDK that is GameSpy (`igamespy.h`: `StartupInfo`, `PatchInfoResults`) and the content-transfer callbacks (`iltcontenttransfernotification.h`, multiplayer downloads). The first one hit is the patch-version check on the main menu: `CInterfaceMgr::UpdatePatchInfo` fills `IGameSpyPatchInfo::StartupInfo` and the engine copies its string with `msvcp71`, which faults in `memcpy`. GameSpy has been offline since 2014; `FEAR_PC_NO_GAMESPY` (patch 0002) compiles those calls out.
- **Loading.** `FEAR.exe -archcfg <file>` replaces `Default.archcfg`. Entries can be absolute paths, later entries override earlier ones, and a folder entry serves loose files. Game DLLs found in archives are copied to `%TEMP%` (`Gam*.tmp`, `Cli*.tmp`) and loaded from there; found in a folder, they load in place. `pc/tools/launch.ps1` writes `build/pc/fearpc.archcfg` (the game's own archives, then `build/pc/overlay` with our three DLLs), so nothing in the game folder changes.
- **Saves.** `-userdirectory <path>` moves saves, profiles and settings out of the user profile.
- **Starting a level.** `+runworld "Worlds\Release\<name>"` loads a world at startup (`CGameClientShell`, console variable `runworld`). `Intro` is Interval 01; `Docks` is Interval 02 and opens with a briefing that waits for a key, then a helicopter ride with the view held.

## How the PC game reads input

- The engine lists devices and their objects through `ILTInput` (`iltinput.h`). `CBindMgr` (`ClientShellDLL/BindMgr.cpp`) maps (device, object) bindings to commands (`Shared/CommandIDs.h`): `IsCommandOn` for buttons, `GetExtremalCommandValue` for axes, and `Update` sends `OnCommandOn`/`OnCommandOff` on changes. `CUserProfile` saves bindings by device name.
- Movement reads `COMMAND_ID_FORWARD_AXIS` and `COMMAND_ID_STRAFE_AXIS` (`CMoveMgr`, positive is forward and right). Looking is `CPlayerMgr::CalculateAxisOffsets`: the mouse through `COMMAND_ID_YAW`/`PITCH`, plus a block marked `GAMEPAD CONTROLLER CODE` that reads `COMMAND_ID_YAW_ACCEL`/`PITCH_ACCEL` with `GPadAim*` console variables (a power curve, edge acceleration, a turn-boost button). Turns are radians, divided by the zoom, then smoothed by the profile's input rate. Nothing binds the `_ACCEL` commands by default.
- The `PLATFORM_XENON` block in `CUserProfile::LoadControls` is an early hard-coded pad layout using Condemned's commands (`FOCUS`, `BLOCK`, `STUNGUN`, `TOOLS`). It is not F.E.A.R.'s.
- There is no soft lock or aim assist in the PC code. `CTargetMgr::GetEnemyTarget()` tells whether an enemy is under the crosshair; `CanActivateTarget()` tells whether there is something to use.

## The 360 game's controller data (title `565507D9`)

All of it is in the 360's `Game/database/Loki.gamedb` (read with `tools/gamedb.py`).

- **Layouts.** `Interface/XenonControlsConfig` has five presets: `Default` ("Normal"), `Leaner`, `Balance`, `Boxer` and `Offensive`. There is no Southpaw record, although `IDS_CONTROLLER_SOUTHPAW` exists. Each action's `Button` is a record link whose low 16 bits index `Interface/XenonButtons` in record order: A, B, Back, D-Pad Down, D-Pad Left, D-Pad Right, D-Pad Up, Left Shoulder, Left Stick Button, Left Stick X, Left Stick Y, Left Trigger, Right Shoulder, Right Stick Button, Right Stick X, Right Stick Y, Right Trigger, Start, X, Y (0 to 19).
  - Default: RT Fire, LT ThrowGrenade, RB NextWeapon, LB SlowMo, A Jump, B Alt-Fire (melee), X Reload, Y MedKit, D-pad up NextGrenade, down FlashLight, left/right LeanLeft/LeanRight, L3 Duck, R3 ZoomIn, Back Mission, Start InGameMenu, left stick ForwardAxis/StrafeAxis, right stick Yaw/Pitch (Pitch scale -1). `Activate` is unbound: the 360's X uses and reloads.
  - Leaner differs: LB/RB lean, D-pad up MedKit, left NextGrenade, right NextWeapon, Y SlowMo.
- **Prompts.** `XenonButtons` records carry `IconGlyph`, the font character for each button: A U+E000, B U+E001, X U+E002, Y U+E003, Start U+E004, Back U+E005, L3 U+E006, R3 U+E007, LB U+E008, RB U+E009, D-pad up/down/left/right U+E00A to U+E00D, LT U+E010, RT U+E011.
- **Look.** `Interface/XenonControls` `AimInputCurve`: (0, 0), (0.75, 45), (0.95, 90), (1, 90), deflection to degrees per second. `DefaultSensitivity` is 60, 80, 100 (not yet mapped to a multiplier). `Client/Shared`: `GPadAimAspectRatio` 0.5, `GPadAimEdgeThreshold` 0.95, `GPadAimEdgeDelayTime` 0.3, `GPadAimEdgeAccelTime` 0.4, `GPadAimEdgeMaxTurnRate` 360 (a variable the PC code doesn't have). `Client/SoftLock`: `TurningSoftLockYawFactor` 0.3, `TurningSoftLockPitchFactor` 0.5, `TurningSoftLockDimsScale` 1.25, plus strafing and circle-strafe factors. `AimAssist`: angle 40, 25 to 50%.
- The 360's `ProfileDatabase/Defaults.gamedb` holds the PC's keyboard and mouse bindings, unused on the console.

## Measured: the right stick in the PC build

`PadMeasure` holds the right stick at fixed deflections inside the running game and measures the camera's actual yaw over 2 s after 0.5 s to settle. Docks, after the helicopter ride, `PadLookRamp 3` (edge ramp off), sensitivity 1:

| Deflection | Measured (deg/s) | 360 curve (deg/s) |
| --- | --- | --- |
| 0.25 | 15.0 | 15.0 |
| 0.50 | 30.0 | 30.0 |
| 0.75 | 45.0 | 45.0 |
| 0.85 | 67.5 | 67.5 |
| 0.95 | 90.0 | 90.0 |
| 1.00 | 90.0 | 90.0 |

This checks our path (zoom, input smoothing, the engine's rotation) against the 360's data. It does not measure the 360 game itself, which would need the same scripted stick in the recomp. These numbers are from before the stick followed the 360's code (next section), which multiplies every rate by 2.04 at Medium and moves the dead zone to each axis.

## The 360's right stick, from its code

From `default.xex` SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792`: `sub_8237DC88` is `CPlayerMgr::CalculateAxisOffsets`. `CPlayerMgr*` is a big-endian u32 at `0x833708DC`; the frame's yaw delta (radians, big-endian float) is at `+504`, pitch at `+500`, final when the function returns.

- **Dead zone, per axis.** The pad binding maps n = short / 32768 to 1.35 n and takes 0.35 off, so v = sign(n) (1.35 |n| - 0.35), zero below |n| = 0.2593. Not radial.
- **Curve, per axis.** `AimInputCurve` ((0, 0), (0.75, 45), (0.95, 90), (1, 90) deg/s) is applied to each axis on its own, keeping its sign.
- **Sensitivity.** `GPadAimSensitivity` = the profile's sensitivity x 0.0255: Low 60 is 1.53, Medium 80 (the default) 2.04, High 100 2.55. So full stick turns 90 x 2.04 = 183.6 deg/s of yaw at Medium, not 90 (183.46 with the 360's degree, next).
- **Degrees to radians** with 0.01744, not pi / 180 (0.08% slower); kept in our code.
- **Pitch** times `GPadAimAspectRatio` 0.5: 91.7 deg/s at full stick.
- **Edge boost, yaw only.** When the stick's magnitude is over 0.95 and |y| under 0.5: after 0.3 s, blended over 0.4 s toward 360 x dt (not times the sensitivity).
- **Zoom** divides the input before the curve.
- **No smoothing and no frame-time clamp**; dt is the real-time timer's elapsed time.
- **Dampers:** melee 0.6, death 0. `MouseAimDampen` 0.8 applies to the mouse only.
- **A 0.5 s input blackout** after an event not yet identified (`0x833721C8`).
- **Soft lock is not a slowdown.** `TurningSoftLock*Factor` only switch on a turning target manager (`CPlayerMgr+0x5B4`). Its effect is an added yaw toward the target, from the weapon's `TargetAssistance` inner and outer angles, strength and rate.

The PC build's look (`pc/core/src/look.cpp`) follows this since 2026-11-15, except for the target-assist pull, zoom before the curve and the blackout. Since the review of 2026-11-15 the stick's turn also takes only the melee and death dampers (patch 0006 had applied `MouseSloMoDampen` and `MouseAimDampen` to it too), and the right stick reaches the look as the pad reports it, without the pull back to the unit circle that slowed full diagonals. `fear_stick_measure=N` in the recomp (`port-nx/src/fear_mkb.cpp`) drives the same scripted stick through the 360 code and logs the yaw per deflection; it has not been run in a level yet.

## Instant Action on the 360, from its code (title `565507D9`)

Read 2026-11-17 from the recompiled executable (XEX `e7fccdf6…2792`), `Loki.gamedb` and the string database in `uberCatalog.cat`, in one pass that has not been cross-checked in play. "Inferred" marks what was not read directly.

**`PitFight*` is not Instant Action.** `PitFight` (server object, vtable `0x82042C24`) and the `CPitFight*` scenes are a developer tool for spawning AI, and no world on the disc holds a PitFight object. The earlier guess in PC-PLAN.md and ARCHITECTURE.md was wrong.

Instant Action is four ordinary scripted levels, a clock, and a results screen computed on the client from a larger `MissionStats`.

- **Screens.** `CScreenInstantAction` (vtable `0x8202973C`, Build `sub_824C4FB0`, launch `sub_824C4E48`): four maps, Leaderboards and a difficulty list. `CScreenInstantActionSummary` (vtable `0x82029924`, Build `sub_824C62C0`, score `sub_824C7928`, retry `sub_824C6048`). No score item on the HUD; the clock is `CHUDTimerMain`.
- **Start.** The launch sets a mode flag in the client mission manager, tells the server (message 255, one bit) and loads `Worlds\Release\InstantActionN`. On entering the world the HUD timer is set to 900 s (`sub_823685B0`); the server clears the stats and stamps the start time (`sub_82587598`).
- **End.** Success is the world's own `msg player exitlevel`; death (mission failed) ends the run too. Both go straight to the summary screen, with no reload. When the clock runs out it only turns red; no forced end was found (not checked exhaustively).
- **Saves** are refused while the server's mode flag is set (`sub_825A2F08`, inferred to be `CanSaveGame`).
- **Score** (`sub_824C7928`, floats, shown with one decimal):
  `10 x medkits + 100 x hit rate + armor + 10 x boosters + sum of 5 x c^2 + 10 x enemy kills + 0.5 x time remaining`
  - time remaining = 900 less the run's length, not below 0, and 0 on a failed run;
  - hit rate = hits / shots, 0 to 1;
  - c = each combo of 2 or more: enemies killed by one shot or one explosion ("killed" inferred);
  - medkits held, current armor, and the Health and Reflex boosters picked up (one of each per map).
  - No difficulty multiplier, headshot, health or slow-mo term, and no ranks.
- **Summary rows:** result (completed or failed), time remaining, enemies killed with the split by melee, firearm and explosive (damage types 8/10, 1/14, 5/17), total combo kills, hit rate, favorite weapon (most kills), armor remaining, medkits remaining, "You found the Booster!", score. Retry restarts the map.
- **Stats the 360 adds to `MissionStats`** (server `CPlayerObj+9588`): kills by melee, firearm and explosive, per-weapon shots, hits and kills, start and end time, six collectable bytes, and a list of combo sizes. Combos come from the weapon and explosion code as messages 281 and 278 with a count; the client keeps counts above 1.
- **Database.** `Missions/Missions/InstantAction1..4` (Layout `DefaultMP`, `ResetPlayer` 0, no default weapons) and `Missions/Levels/InstantAction1..4` (Name `IDS_INSTANT_ACTION_MAPn`, Help `IDS_BUMPER_LOADING`). `Interface/Screens/ScreenInstantAction` (FontSize 27, two lists 200 wide) and `ScreenInstantActionSummary` (row positions at x 65, y 113, 142, 239, 290, then the score at 364 or 394 with a booster). `Interface/HUD/HUDTimerMain` at (320, 22), size 27, centred.
- **Strings:** "Instant action", "Map 1 - Distribution", "Map 2 - Rooftop", "Map 3 - Construction", "Map 4 - Vault", and the `INSTANT_ACTION_RESULT_*` row labels ("Enemies killed:", "Via melee:", "Via fire-arm:", "Via explosive:", "Total Combo Kills:", "Hit rate:", "Favorite weapon:", "Armor remaining:", "Medkits remaining:", "Time remaining:", "Score:", "Mission Completed!", "Mission Failed!").
- **Loadouts are scripted in each world:** map 1 shotgun, full slow-mo and frag grenades; map 2 full health, full slow-mo, submachine gun; map 3 as map 1; map 4 full slow-mo only.
- **Leaderboards** take the map index and the score as an integer; a failed run is submitted too.

### What the PC source already has

- A world launches from its path alone (`CMissionMgr::StartGameFromLevel`); a Missions record is optional and only adds a loadout and a name.
- `MissionStats` (`Shared/SharedMission.h`) counts shots, hits, enemy kills and deaths, and reaches the client as `MID_PLAYER_SUMMARY`. It lacks the kill split, per-weapon records, combos, collectables and times.
- `SCREEN_ID_END_MISSION` is in the screen list and single player switches to it at the end of a custom level, but no screen class exists for it: the place for the summary.
- `CHUDTimer` / `g_pMainTimer->SetTime` for the clock; `CServerSaveLoadMgr::CanSaveGame` for the saves.

### The maps

The four maps exist only as 360 worlds (`InstantActionN.wld`, `.wldclnt`, `.wldsrvr`, big-endian). The taken-down console content mod converted them on the player's machine; its converter (Go, about 3,000 lines, of which about 1,900 are general 360-to-PC world conversion) is in the local clone, and its output from the test of 2026-11-07 is on this PC. Its maps are edited for its own closed hook (each `exitlevel` became a mission-failed message with a code), so they are a stand-in for development, not what we would ship a converter for. Its licence allows reusing its installer source in a free project that credits it, links its original page and carries its licence for the reused parts.
