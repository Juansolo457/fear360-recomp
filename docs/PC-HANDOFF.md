# PC version: handoff (2026-11-15)

Where the PC version of F.E.A.R. (our GameClient/GameServer DLLs on the retail PC engine) stands, for a new session. The plan is in [PC-PLAN.md](PC-PLAN.md), the test history in [PC-PROGRESS.md](PC-PROGRESS.md), findings with the binary hashes in [PC-FINDINGS.md](PC-FINDINGS.md).

## Rules for this work

- Branch `pc-controller`. **Stays local: don't push it** until the definitive PC release (#143 is the release review). GitHub issues and comments are fine.
- Run `python -I tools/audit_repo.py` before every commit. Never commit game data, SDK files (the audit rejects them by header), logs or saves.
- Our working copy of the game is `game_dir` in `private/pc.toml`. Don't touch the store installs (Steam, GOG).
- Commit only the PC files. The tree also has another session's uncommitted recomp work: `docs/FINDINGS.md`, `port-files/fear_manifest.toml`, `port-nx/fear_manifest.toml`, `patches/rexglue-sdk-bd833a2/`. Leave those alone.

## Just done: the right stick (#139, closed)

The PC look now follows the 360's own stick code (`CPlayerMgr::CalculateAxisOffsets`, PC-FINDINGS.md "The 360's right stick, from its code"):
- the x2.04 Medium sensitivity;
- dead zone and curve applied per axis;
- look acceleration Off by default;
- no soft-lock slowdown.

Play-tested on a GTX 1650 laptop and the Steam Deck (Proton Experimental): it felt great, smoother than the 360.

Files in that change:

| File | What |
| --- | --- |
| `pc/core/include/fearpc/look.h`, `pc/core/src/look.cpp` | The stick model (`LookModel`, `AxisDeadZone`, `CurveRate`), documented in the header |
| `pc/core/tests/core_tests.cpp` | `TestLook`: 183.45 deg/s at full stick, dead zone, diagonal, sensitivity, edge ramp |
| `pc/game/src/fear_pad.cpp` | New defaults (`PadLookRamp 3`, `PadSoftLock 0`); the right stick's dead zone moved into the look model; `PadLookLog` (once-a-second log to `pad_look_log.txt`); `PadMeasure` expects the new rates |
| `port-nx/src/fear_mkb.cpp` | Recomp-side `fear_stick_measure=N`: scripted right stick through the 360 code, logs yaw per deflection. Never run in a level |
| `docs/PC-*.md` | Findings and test notes |

## First job: code review (Fable 5.2, in place of CodeRabbit)

Review the change above (`git diff` for the PC files, or the commit if it's been made) for correctness against PC-FINDINGS.md:

- **Math:** the per-axis dead zone and curve, the sign handling, the edge boost blend, and the units (`kConsoleDegree`, radians vs degrees at the call site in `fear_pad.cpp`).
- **Defaults:** saved settings in `fearpc_pad.txt` override the defaults. Does the Controller screen show and save the new ones properly?
- **`PadLookLog`:** file handling, and its cost when off.
- **`fear_mkb.cpp` measure hook:** guest-memory reads, the `packet_` bump, and whether it stays inert at 0.

Build and test with `pwsh -NoProfile -File pc/tools/build.ps1`, which also runs the `pc/core` tests.

## Open work, roughly in order

1. **#137** Hold X to pick up a weapon: the fix is in but hasn't been confirmed in play.
2. **#142** Quitting to the menu crashes (`FEAR.exe+0x4abd6`): suspected cross-heap free.
3. **#140** Look sensitivity doesn't adjust correctly: recheck now that 1 = the 360's Medium.
4. **#144** Choose the controls, the 360's or EchoPatch's (#138 for EchoPatch's other features).
5. **#146** A settings screen like the recomp's, with PC / 360 / PS3 presets (#141 for its look).
6. **#145** Xbox and PlayStation glyphs.
7. **#147** Console-only weapons and the two bonus levels (#133 Instant Action).
8. **Stick, still to port:** the 360's target-assist pull, zoom before the curve, and the 0.5 s blackout (PC-FINDINGS.md).
9. **Questions:** #148 (a native Linux build of the PC version) and #149 (low-end PCs, Sandy Bridge at the oldest).

For #145 and #147: a local clone of the taken-down F.E.A.R. Exclusive Console Content mod (Go) is at `E:\fear-pc-dev\ecc\src`. Its `internal/hudicon` and `internal/consolefont` cover the glyphs; `sfodworld`, `holworld`, `iaworld` and `dbmerge` cover the content. Its licence allows free use but not re-uploading it as is, so use it as a reference and ask its authors before copying code.

## Test machines

- **This PC:** `pc/tools/launch.ps1`.
- **Steam Deck:** `pc/tools/deploy-deck.ps1`. Tailscale SSH may ask for a browser approval first; the launch option is in PC-PROGRESS.md.
- **GTX 1650 laptop:** `onecu@jj-low-laptop`. To update it, scp the three DLLs to `C:/recomp/fear-pc/overlay/`; the player runs `C:\recomp\fear-pc\Play.bat`. Details are in PC-PROGRESS.md "Low-end laptop".
