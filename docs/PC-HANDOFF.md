# PC version: handoff (2026-11-17)

Where the PC version of F.E.A.R. (our GameClient/GameServer DLLs on the retail PC engine) stands, for a new session. The plan is in [PC-PLAN.md](PC-PLAN.md), the test history in [PC-PROGRESS.md](PC-PROGRESS.md), findings with the binary hashes in [PC-FINDINGS.md](PC-FINDINGS.md), the world formats in [PC-WORLDS.md](PC-WORLDS.md).

## Rules for this work

- Two draft PRs, stacked: `pc-controller` (#150: the 360's controller, the right stick, button glyphs) and `pc-instant-action` on top of it (Instant Action). **Review only: no merge and no release until the user says.** #143 is the release review.
- Run `python -I tools/audit_repo.py` before every commit and push. Never commit game data, SDK files (the audit rejects them by header), converted worlds, logs or saves.
- Our working copy of the game is `game_dir` in `private/pc.toml`. Don't touch the store installs (Steam, GOG).
- Commit only the PC files. The tree also has another session's uncommitted recomp work: `docs/FINDINGS.md`, `port-files/fear_manifest.toml`, `port-nx/fear_manifest.toml`, `patches/rexglue-sdk-bd833a2/`. Leave those alone.
- Ask before building or launching the game on the user's PC if they are using it; never close a game the user is playing.
- Changes to the game source live in `third_party/fear-publictools` (local branch `fear-pc`, one commit per patch) and reach the repo as `pc/game/patches/000N-*.patch` (`git format-patch -1 --zero-commit --no-signature --start-number N`; check with `git apply --check -R --directory=third_party/fear-publictools`).

## First job: code review of Instant Action (branch `pc-instant-action`)

Review the branch's diff against `pc-controller`. It was written fast, partly by agents, and has had one short play test; read it as untrusted.

What it adds (#133): the Xbox 360's Instant Action mode in the source build. The rules come from the 360 executable (PC-FINDINGS.md, "Instant Action on the 360, from its code"): four scripted maps, a 15 minute clock, no saves, and a summary screen that scores the run.

| Part | Files |
| --- | --- |
| Score formula and tests | `pc/core/include/fearpc/instant_action.h`, `pc/core/src/instant_action.cpp`, `pc/core/tests/core_tests.cpp` (`TestInstantAction`) |
| Client: run flags, clock, failure, summary message, best scores (`fearpc_ia.txt`) | `pc/game/src/fear_ia.h`, `fear_ia_client.cpp` |
| Menu screens: map list with difficulty, summary | `pc/game/src/fear_ia_screens.cpp` |
| Server: run statistics, kill split, combos, boosters, summary | `pc/game/src/fear_ia_server.cpp` |
| Hooks in the game source (16 files, under `FEAR_PC_IA`) and the effects DLL's unload fix | `pc/game/patches/0008-*.patch` |
| Map converter (Python, standard library) | `pc/tools/convert_ia.py`, `pc/tools/iaworld/` |
| Scripts that copy the converted maps beside the DLLs | `pc/tools/launch.ps1`, `pc/tools/deploy-deck.ps1` |

Check in particular:

- **Layout safety.** No class or struct in a header shared between the libraries may differ between them (the first glyph build crashed that way; PC-PROGRESS.md "Button glyphs"). `MID_INSTANT_ACTION` (166) and the two screen ids are deliberately outside `FEAR_PC_IA`. `MissionStats` must be untouched: it is in save files.
- **Normal single player unchanged** with no run active: every hook in patch 0008 should be inert. Look hardest at `CPlayerObj::HandleExitLevelMsg`, `CInterfaceMgr::MissionFailed`, both `CanSaveGame`s and `CMissionMgr::FinishStartGameFromLevel`.
- **The run flag's life:** set by `FearIAStart` through `StartingNewGame`, cleared by any other start and by the main menu. Can it stay set into a campaign level, or clear during a run (loading a save from the pause menu is still offered)?
- **The clock.** It read 14:22 about fifteen seconds after a map loaded, so it starts before play does. The server's run length and the HUD clock should agree, and the 360 starts on entering the world.
- **Statistics against the findings:** the kill split by damage type, combos (the scope around one trigger pull and one explosion), boosters, medkits and armor at the end, favorite weapon by ammo. The summary's arithmetic is `fearpc::InstantActionScore`.
- **The failed-run path:** the client reports the failure and waits up to 3 s for the server's summary before showing the normal failure screen.
- **Completed runs leave the server in the level**, paused behind the summary; Retry starts a new game on it. Leaks or stale state?
- **The effects DLL fix** (`ClientFXVertexDeclMgr`'s destructor no longer releases through the engine): right for a DLL that is unloaded and loaded again in one run?
- **The converter:** that nothing it writes can land in a git-tracked folder, that it fails loudly on input it does not understand, and that the content fixes in PC-WORLDS.md are each justified. Its geometry, render, sector and blind sections were byte-identical to the mod's output for all four maps; only the object data differs, by the listed edits.
- **Licence and credit:** the converter's format knowledge came from the F.E.A.R. Exclusive Console Content installer (credited in the tool and in PC-WORLDS.md). It must be our own code, not a transliteration; spot-check against `E:\fear-pc-dev\ecc\src\internal\iaworld`.

Seen working (Windows, 2026-11-17): the menu entry, the map screen with a saved best score, maps 1 and 2 loading and playing, the clock, Save greyed out, the loading screen naming the map. Not seen: the summary screen itself, a completed run, maps 3 and 4, anything on the Deck.

Build and test with `pwsh -NoProfile -File pc/tools/build.ps1` (also runs the `pc/core` tests). Maps: `python -I pc/tools/convert_ia.py --x360 port/assets/Game`, then `pc/tools/launch.ps1`.

## Also on `pc-controller` since the last review

Button glyphs (#145, patch 0007, `pc/game/src/fear_pad_glyphs.cpp`, `pc/tools/make_glyphs.py`): seen working on the Deck. Worth a look in the same review: `CLTGUIString`'s glyph code in patch 0007 (placeholder spaces, `FitGlyphs`, `RenderGlyphs` with centred and right-aligned text, clipping).

## Open work, roughly in order

1. **Instant Action, after review:** the summary screen checked in play; the clock's start; the loading picture and the 360's HUD layout; the machine pistol pickups (stock weapons stand in until #147); best times and leaderboards (#83 is the recomp's); the expansions' eight maps.
2. **Quit crashes** (#142, #153): the effects DLL's part is fixed. What remains is in the engine's teardown (`FEAR.exe+0x9b64d` through `0x44AC0A` and `0x44C90D`, the same release path as #142's `0x4abd6`): an object released after it was freed.
3. **PS3 content:** the PlayStation glyph set is drawn; the SFOD-D mission and the Watson shotgun remain (#127, #128, #147).
4. **#151** All of the recomp's settings in the game's menus, with a field of view slider (#146 for presets).
5. **#152** Measure what EchoPatch costs in frame rate (#138, #144).
6. **#137** Hold X to pick up a weapon: the fix is in but hasn't been confirmed in play. **#140** look sensitivity: recheck now that 1 = the 360's Medium.
7. **Stick, still to port:** the 360's target-assist pull, zoom before the curve, and the 0.5 s blackout (PC-FINDINGS.md).
8. **Questions:** #148 (a native Linux build of the PC version) and #149 (low-end PCs, Sandy Bridge at the oldest).

A local clone of the taken-down F.E.A.R. Exclusive Console Content mod (Go) is at `E:\fear-pc-dev\ecc\src`, with its output from a test install beside it. Its licence allows reusing its installer source in a free project that credits it and carries its licence; the user's decision (2026-11-17) is to use it as a reference only and write our own.

## Test machines

- **This PC:** `pc/tools/launch.ps1`. Crash dumps land in `%LOCALAPPDATA%\CrashDumps`; `llvm-symbolizer --obj=build/pc/game/<dll> --relative-address <offset>` names an address in our DLLs.
- **Steam Deck:** `pc/tools/deploy-deck.ps1` (DLLs, the glyph picture, the converted maps). Tailscale SSH may ask for a browser approval first, and the Deck drops off Tailscale when it sleeps; the launch option is in PC-PROGRESS.md. Check the game isn't running before copying.
- **GTX 1650 laptop:** `onecu@jj-low-laptop`. To update it, scp the DLLs to `C:/recomp/fear-pc/overlay/`; the player runs `C:\recomp\fear-pc\Play.bat`. Details are in PC-PROGRESS.md "Low-end laptop". It still has a build from before the glyphs.
