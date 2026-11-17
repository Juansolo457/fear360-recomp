# PC F.E.A.R. progress

Milestone 1 of [PC-PLAN.md](PC-PLAN.md): the 360's controller on Windows PC F.E.A.R. (base game). Findings, with the binary hashes they apply to: [PC-FINDINGS.md](PC-FINDINGS.md). Nothing here has been pushed.

## Where it stands (2026-11-17)

Handoff for the next session: [PC-HANDOFF.md](PC-HANDOFF.md).

| Step | State |
| --- | --- |
| 1. Dev copy | Done. A copy of the game outside both store folders, without EchoPatch; dxwrapper's window mode is on in the copy. Path in `private/pc.toml`. |
| 2. Source tree | Done. `pc/tools/setup.ps1` unpacks the public tools from their installer into `third_party/fear-publictools` (checked against a known tree) and applies `pc/game/patches`. `tools/audit_repo.py` rejects the SDK's files anywhere in the index (by their copyright header). |
| 3. Unmodified rebuild | Done. VS 2022, x86. Needed two small source fixes and GameSpy compiled out (patches 0001, 0002). |
| 4. Loading | Done. `pc/tools/launch.ps1`: `-archcfg` plus a folder of our DLLs, `-userdirectory` for saves. The game folder is not written. Checked: the menu, Interval 01's opening and Interval 02 load and play with our DLLs; autosaves are written. Not checked: an interval played to the end. |
| 5. Input path | Done, in PC-FINDINGS.md. |
| 6. Pad input | Play-tested with an Xbox Series X controller (Bluetooth): good, except X never reloaded (fixed and confirmed in a second session). SDL3 (static), the 360's Normal and Lean layouts, menus by pad, X as use-or-reload, Start for the menu, rumble (second version, confirmed working in play). |
| 7. 360 look | Done (2026-11-15): follows the 360's own stick code (PC-FINDINGS.md), play-tested good on Windows and the Steam Deck. #139 closed. |
| 8. Play test | Five play tests, see below. |
| 9. Button glyphs | First pass done (2026-11-17, #145): prompts show the pad's button, seen working on the Steam Deck (the pick-up prompt shows X). See "Button glyphs". |

### Play test 5 (2026-11-15): the stick fixed

- After the rewrite below, played with a controller on a GTX 1650 laptop (Windows) and on the Steam Deck (Proton Experimental, the Steam copy). Both felt great, smoother than the 360. #139 closed.
- The laptop also ran the recomp 1.7 for comparison: 30 fps at 3x resolution, which felt poor (#149).
- No logs from either PC run: the PC game writes none, and `PadLookLog` was off.

### Play test 4 (2026-11-11): still floaty

Felt floaty and delayed, sometimes missed input, the view jerked, and the yaw ran ahead of the thumb. Reading the 360's code (PC-FINDINGS.md, "The 360's right stick, from its code") found four differences, all fixed:

- The PC lacked the 360's x2.04 sensitivity (Medium), so Off felt slow and Full speed made up for it by jumping to 360 deg/s at the edge: the yaw running ahead.
- `PadSoftLock` slowed turning by 70% whenever `GetEnemyTarget()` flickered on; the 360 doesn't slow down at all. Now off by default.
- The dead zone and curve were radial; the 360's are per axis.
- Look acceleration now defaults to Off, as in the recomp.

`pc/core/tests/core_tests.cpp` checks the new model (183.45 deg/s at full stick at Medium, the per-axis dead zone, the edge ramp).

### Controller screen

Options > Controls > Configure joystick/gamepad is the Controller screen when the pad code is built in (`pc/game/src/fear_pad_screen.cpp`, patch 0005 compiles the PC's DirectInput joystick page out): Controller on/off, Button layout (Normal, Lean), Look sensitivity (25 to 200%), Look acceleration (Off, Xbox 360, Immediate, Full speed, named as in the recomp's settings), Slow turn on enemies (`PadSoftLock`, called Aim assist before 2026-11-15), Vibration (0 to 100%). Leaving the screen applies the settings and saves them to `fearpc_pad.txt` in the user directory, read at the next start. Checked in game: values shown, saved on leaving. It is the first piece of the recomp's settings menu; the other options come later.

### Play test 3 (2026-11-06)

- Interval 04 played through well; vibration and reload good.
- **Holding X on a weapon didn't pick it up.** Weapons and items on the floor aren't crosshair targets: the game finds them with `CPlayerMgr::GetPickupObjectDetector()`, which Use acts on first. X now uses when that detector has an object too.
- **Wind-up on the stick** (the 360's, still there). Two causes: the stick went through the PC's mouse smoothing (each frame averaged with the last, by the profile's input rate), which delays starting and stopping; and the 360's edge ramp (0.3 s wait, then 0.4 s to full speed). The stick's turn is now added after the smoothing (patch 0006; invert and the slow-mo, melee and aiming dampers still apply), and Look acceleration starts at Full speed: past 95% of the stick's travel, 360 degrees a second at once. Measured again in Docks at 110% sensitivity: 16.5, 33, 49.5, 74.25 deg/s at 0.25 to 0.85, 396 at 0.95 and 1.0. Not yet re-tested by feel. Moving (left stick) is the game's own movement acceleration, not changed.
- The quit-to-menu crash happened again at the end of the session (`FEAR.exe+0x4abd6`, three times in this session by the event log).

### Play test 2 (2026-11-06)

- Played well (Interval 04), with the controller.
- **Turning felt slow.** With the edge ramp off, the stick tops out at the curve's 90 degrees a second. Look acceleration now starts at Xbox 360 (`PadLookRamp 0`): held at the edge for 0.3 s, turning ramps to 360 a second, as the 360 shipped. Off is still on the Controller screen.
- **Quitting to the menu crashed the game** (Leave game, after a longer session). Caught with a debugger attached: an access violation in `FEAR.exe` at offset `0x4abd6`, the same as the 15:44 crash in play test 1. The stack is all engine code: `0x44C900` drops a reference count and at zero frees the object (`0x44ABA0`), which reads a pointer (`[esi+10h]`, then `+8`) whose target is already gone. Level teardown freeing a resource twice, or after its owner. A short run (Docks, Leave game) didn't reproduce it. Open: whether the retail DLLs do the same, and which resource it is; suspect memory created on one side of the DLL boundary and freed on the other, since our DLLs use a different C runtime heap from the engine's.

### Play test 1 (2026-11-06)

- Good overall.
- **X never reloaded.** `CTargetMgr::CanActivateTarget()` is true by default, with nothing in front of you, so X always chose Use. It now uses only when the activation target is set (something usable in reach; surface sounds don't count), otherwise reloads.
- **No vibration** (none was built). A first try sent the pad's motors a rumble command every frame (shots, damage, the effects' camera shake); the game then stopped responding (Windows closed it, "Application Hang", 16:17) and it was reverted. Suspected: per-frame rumble writes blocking over Bluetooth. That didn't hold up: `fearpc_pad_probe --rumble` timed 300 sends at 60 a second on the same controller over Bluetooth, worst 0.019 ms (SDL queues them). The cause of that hang is unknown.
- **Rumble, second version.** The same sources (shots, damage, effect motors, camera shake), but the pad is told only when a motor's strength changes by 0.05 or more, or every 0.1 s while it runs (each send lasts 0.25 s). Also fixed: rumble went only to a pad that had had input, so an idle pad got none. Soak test: Docks with its helicopter ride, 4.5 minutes, the window answered every 5 s check, no errors logged.
- The build before the fixes also crashed once (15:44): access violation in `FEAR.exe` itself, offset `0x4abd6`. Cause unknown; when it happened in the session not recorded.

## Running it

```
pwsh -NoProfile -File pc/tools/setup.ps1 -Installer "<Steam F.E.A.R.>\extras\fear_publictools_108.exe"
pwsh -NoProfile -File pc/tools/build.ps1
pwsh -NoProfile -File pc/tools/launch.ps1
```

`build.ps1` also runs `pc/core`'s unit tests. `build/pc/game/core/fearpc_pad_probe.exe` shows which pads SDL sees and their live state, through the same code the game uses. `launch.ps1 -Retail` runs the game's own DLLs the same way, for comparison. More engine arguments: `-Extra '+runworld','"Worlds\Release\Docks"'`.

Console variables (in the game's console, or `+Name value` on the command line through `-Extra`):

| Variable | Default | |
| --- | --- | --- |
| `PadEnabled` | 1 | 0 ignores pads |
| `PadLayout` | 0 | 0 the 360's Normal layout, 1 its Lean layout |
| `PadLookRamp` | 3 | 0 the 360's edge ramp as shipped, 1 ramp without the wait, 2 full boost at the edge, 3 off (the recomp's default) |
| `PadLookSensitivity` | 1 | multiplies the 360's turn rates; 1 is the 360's Medium (x2.04) |
| `PadSoftLock` | 0 | the older slowdown with an enemy under the crosshair; not the 360's behaviour |
| `PadLookLog` | 0 | 1 writes `<user dir>\pad_look_log.txt` once a second: frame times, stick, yaw, soft-lock switching |
| `PadRumble` | 1 | vibration strength, 0 to 1 (0 off) |
| `PadMeasure` | 0 | 1 runs the turn-rate measurement into `<user dir>\pad_measure.txt`; N > 1 waits N seconds into play first |

The Controller screen saves its settings to `<user dir>\fearpc_pad.txt`, which starts with `Version 2`. A file without that line is from before the look followed the 360's code: its `PadLookRamp`, `PadLookSensitivity` and `PadSoftLock` are ignored, so the defaults above apply, and the rest (on/off, layout, vibration) is kept.

## Button glyphs

With a controller in use, a command's key name is the 360's character for the button it is on (U+E000 and up, `pc/core` `ButtonGlyph`), so the pick-up and use prompts, the health hint and the loading-screen tips all get it from `CUserProfile::GetTriggerNameFromCommandID`. `CLTGUIString` (patch 0007) keeps spaces where a button character is, sized to the font, and draws the button over them from `Interface\FearPC\PadGlyphs.dds`.

- The picture is our own art, both an Xbox and a PlayStation set, drawn by `pc/tools/make_glyphs.py` during `build.ps1` (Python with Pillow). It is a loose file in the overlay, beside the DLLs; `launch.ps1` and `deploy-deck.ps1` copy it. Nothing comes from either console's files.
- `PadGlyphs` (Controller screen, "Button prompts"): Keyboard, Automatic (by the connected pad; the Deck's controls report as Xbox), Xbox, PlayStation. The Controller screen also lists each action with its button for the chosen layout.
- The glyph members of `CLTGUIString` are not under `FEAR_PC_PAD`. The first build had them under it, and `Game_Shared_Client` (compiled without it) then read `CGameClientShell::m_sfxMgr` at the wrong offset: the game crashed when the first shot was traced. Any change to a class in a header shared between the libraries must be the same in all of them.
- Not done: the weapon list's hotkeys still show keyboard numbers; menus have no button bar; the PlayStation set has not been seen with a PlayStation pad; a button character in a string that takes its glyphs from another string's texture is left blank.
- The taken-down console content mod's source has no button glyph code (its `hudicon` is weapon icons and its `consolefont` the Instant Action results fonts); what it draws for buttons is inside its closed DLL.

## EchoPatch

Not in the first pass (decided 2026-11-06): the goal is the 360's controller, and EchoPatch has its own SDL controller support with the PC's layout and stick code, which would double every input alongside ours. Briefly tried in the dev copy with `SDLGamepadSupport = 0`, then taken out again. It may come back later as a toggle, with its controller support off whenever ours is on.

PlayStation controllers are later too (noted 2026-11-06). SDL3 already reads DualShock and DualSense pads and reports them as `PadFamily::kPlayStation`; what's missing is their prompts. The 360 game's database has a PS3 controller-layout screen (`Interface/Screens/ScreenShowControlsPS3`, `ps3_controller_*.tex`), a reference for the layout and the glyph set.

## Steam Deck

There is no native Linux build of the PC version: the engine (`FEAR.exe`, with the renderer) is a closed 32-bit Windows program, and our work is the game DLLs it loads. On the Deck the Steam F.E.A.R. runs under Proton and loads our DLLs as on Windows.

```
pwsh -NoProfile -File pc/tools/deploy-deck.ps1
```

copies the three DLLs to `~/fear-pc/overlay` on the Deck (over SSH, host `deck-ts`), writes `~/fear-pc/fearpc.archcfg` (the Deck install's archives, then the overlay, as Proton `Z:` paths) and prints the launch option to set in Steam (F.E.A.R. > Properties > Launch Options):

```
%command% -archcfg "Z:\home\deck\fear-pc\fearpc.archcfg" -userdirectory "Z:\home\deck\fear-pc\user"
```

Clearing the launch option plays the game as shipped; the game folder isn't changed. The Deck's install (2026-11-06) is byte-identical to the Windows Steam copy (`FEAR.exe` with the Steam wrapper, build 15017) and has no EchoPatch. Played there 2026-11-15 under Proton Experimental: the Steam `FEAR.exe` loads our DLLs, SDL3 reads the Deck's controls, and the stick felt great. Tailscale SSH to the Deck asks for a browser approval the first time in a while; the deploy waits on it.

## Low-end laptop

`jj-low-laptop` on Tailscale (GTX 1650 Max-Q, Windows 11), SSH as `onecu`. OpenSSH Server was reinstalled there 2026-11-15. `C:\recomp\fear-pc` holds a copy of the dev game folder, the three DLLs in `overlay\`, the 32-bit VC++ runtime in `runtime\` and `Play.bat` (writes the archive list, sets `-userdirectory user`). To update it, copy `build\pc\game\{GameClient.dll,GameServer.dll,ClientFx.fxd}` to `C:/recomp/fear-pc/overlay/` with scp. `C:\recomp\fear-1.7-20261115\run-fear.bat` is the recomp 1.7 (shared data in `C:\recomp\fear-assets`, saves in `fear-user`); running `fear.exe` directly fails with "--game_data_root was not provided".

## Play-test checklist (step 8)

Turn the controller on, check `fearpc_pad_probe.exe` sees it, then `launch.ps1`. Play Intervals 01 and 02, and the same stretch in the recomp (`tools/launch.ps1 -Nx -Extra '--nx_renderer native'`).

- Menus: D-pad and left stick move, A picks, B backs out, Start picks. The A that starts a level doesn't also jump.
- Every button in the Normal layout does what the 360 does (table in `pc/core/include/fearpc/layout.h`). X opens doors and picks up weapons when aimed at them (hold it), otherwise reloads.
- Start opens the menu in play, and the Start that opens it doesn't also pick an item.
- Looking: does it feel like the 360? Try Look acceleration Xbox 360 (as shipped) and Off on the Controller screen, and Slow turn on enemies on and off in a fight.
- Keyboard and mouse still work at the same time.
- Note anything that differs from the recomp in PC-PROGRESS.md.

## Known gaps

- The weapon list's hotkeys still show the keyboard's numbers with a pad (the pad has no button for them).
- Rumble is ours, not the 360's: the 360's comes from rumble effects in its effect files, which the PC game doesn't have. The strengths (shots, damage, shake) are first guesses to tune by feel.
- The 360's aim assist (an added yaw toward the target, from the weapon's `TargetAssistance`) is not ported, nor zoom before the curve or the 0.5 s input blackout. See PC-FINDINGS.md.
- #137 (hold X to pick up a weapon) has a fix in but hasn't been confirmed in play.
- Multiplayer through GameSpy is compiled out (it has been offline since 2014).
- The patches in `pc/game/patches` carry a few lines of the SDK as context. Whether that may be published is the licence question (plan, spike 6); until it is answered, nothing in `pc/` is pushed.
