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

This checks our path (zoom, input smoothing, the engine's rotation) against the 360's data. It does not measure the 360 game itself, which would need the same scripted stick in the recomp.
