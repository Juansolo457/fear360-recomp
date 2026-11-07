# PC F.E.A.R. progress

Milestone 1 of [PC-PLAN.md](PC-PLAN.md): the 360's controller on Windows PC F.E.A.R. (base game). Findings, with the binary hashes they apply to: [PC-FINDINGS.md](PC-FINDINGS.md). Nothing here has been pushed.

## Where it stands (2026-11-06)

| Step | State |
| --- | --- |
| 1. Dev copy | Done. A copy of the game outside both store folders, without EchoPatch; dxwrapper's window mode is on in the copy. Path in `private/pc.toml`. |
| 2. Source tree | Done. `pc/tools/setup.ps1` unpacks the public tools from their installer into `third_party/fear-publictools` (checked against a known tree) and applies `pc/game/patches`. `tools/audit_repo.py` rejects the SDK's files anywhere in the index (by their copyright header). |
| 3. Unmodified rebuild | Done. VS 2022, x86. Needed two small source fixes and GameSpy compiled out (patches 0001, 0002). |
| 4. Loading | Done. `pc/tools/launch.ps1`: `-archcfg` plus a folder of our DLLs, `-userdirectory` for saves. The game folder is not written. Checked: the menu, Interval 01's opening and Interval 02 load and play with our DLLs; autosaves are written. Not checked: an interval played to the end. |
| 5. Input path | Done, in PC-FINDINGS.md. |
| 6. Pad input | Play-tested with an Xbox Series X controller (Bluetooth): good, except X never reloaded (fixed and confirmed in a second session). SDL3 (static), the 360's Normal and Lean layouts, menus by pad, X as use-or-reload, Start for the menu, rumble (second version, not yet felt in play). |
| 7. 360 look | Done and measured: turn rates in the running game match the 360's curve at every deflection tested. The edge ramp, soft lock and sensitivity are console variables. Not measured: the recomp with the same scripted stick. |
| 8. Play test | First pass done (2026-11-06), see below. |

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
| `PadLookSensitivity` | 1 | multiplies the 360's turn rates |
| `PadSoftLock` | 1 | slow turning with an enemy under the crosshair |
| `PadRumble` | 1 | vibration strength, 0 to 1 (0 off) |
| `PadMeasure` | 0 | 1 runs the turn-rate measurement into `<user dir>\pad_measure.txt`; N > 1 waits N seconds into play first |

## Play-test checklist (step 8)

Turn the controller on, check `fearpc_pad_probe.exe` sees it, then `launch.ps1`. Play Intervals 01 and 02, and the same stretch in the recomp (`tools/launch.ps1 -Nx -Extra '--nx_renderer native'`).

- Menus: D-pad and left stick move, A picks, B backs out, Start picks. The A that starts a level doesn't also jump.
- Every button in the Normal layout does what the 360 does (table in `pc/core/include/fearpc/layout.h`). X opens doors and picks up weapons when aimed at them (hold it), otherwise reloads.
- Start opens the menu in play, and the Start that opens it doesn't also pick an item.
- Looking: does it feel like the 360? Try `PadLookRamp 0` (as shipped) and `3` (off), and `PadSoftLock 0` against `1` in a fight.
- Keyboard and mouse still work at the same time.
- Note anything that differs from the recomp in PC-PROGRESS.md.

## Known gaps

- Button prompts still show the PC's key names (milestone: glyphs).
- Rumble is ours, not the 360's: the 360's comes from rumble effects in its effect files, which the PC game doesn't have. The strengths (shots, damage, shake) are first guesses to tune by feel.
- The 360's `AimAssist` (40 degrees, 25 to 50%) and the strafing soft-lock factors are not reproduced; soft lock uses `CTargetMgr::GetEnemyTarget()`, not the 360's enlarged target box (`TurningSoftLockDimsScale` 1.25).
- `DefaultSensitivity` (60/80/100) is not mapped yet; `PadLookSensitivity 1` is the curve as stored.
- Multiplayer through GameSpy is compiled out (it has been offline since 2014).
- The patches in `pc/game/patches` carry a few lines of the SDK as context. Whether that may be published is the licence question (plan, spike 6); until it is answered, nothing in `pc/` is pushed.
