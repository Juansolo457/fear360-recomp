# Protecting the hooks

Written 2026-11-04 as a plan and built 2026-11-05. Applies to F.E.A.R. `default.xex` SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792` (title `565507D9`) and to F.E.A.R. Files (`a51deed1dc0842af32a3a2b811674552f4ae5b6339ce8b9abe0eabe14ba6c7eb`), whose addresses differ.

## Why

Every game fix reaches the game through a hook on an address in the executable. A hook on an address that is a real function, but the wrong one, compiles, links and runs. Three things can go wrong, and each has its own check:

| Failure | Check | Where |
|---|---|---|
| A different executable is loaded, so every address is off | A hash of the game's code at every hook, checked once at startup | `port-nx/src/image_check.*`, `hook_sites_*.inc`, `tools/hook_sites.py` |
| Right executable, the hook is on the wrong function | Tripwires inside the hook; a written reason for each match | `port-nx/src/hook_guard.h`; the comments in `weapon_fixes.cpp`, `high_fps.cpp`, `fear_mkb.cpp` |
| Right function, the fix's own arithmetic is wrong | Unit tests on the arithmetic | `port-nx/tests/`, `tools/test.ps1` |

None of them replaces playing the fix: see "Still owed".

## What is protected

| Kind | F.E.A.R. | F.E.A.R. Files | Fingerprint | Tripwire | Written reason |
|---|---:|---:|---|---|---|
| Mid-function hooks (`[[entrypoint.midasm_hook]]`) | 70 | 30 | yes | no | the comment on each manifest entry |
| Whole-function fixes (`weapon_fixes`, `high_fps`, `fear_mkb`) | 10 | 8 | yes | on 4 of them | yes |
| Direct3D hooks (`d3d12/fear_d3d_hooks.cpp`) | 16 | 16 | yes | no | `d3d12/fear_d3d.h` |

## Hook-site fingerprints

- `tools/hook_sites.py` collects every hook's address from both manifests and from the `REX_HOOK_RAW` and `FEAR_..._HOOK` lines in `port-nx/src` (following `FEAR_TITLE_FILES`), and writes one table per game: address, a 64-bit FNV-1a hash of the 32 bytes of code there, and the hook's name. It needs the dumped images (`--fear_dump_image`). The tables hold hashes only.
- `python -I tools/hook_sites.py --check` needs no game files: it fails when a hook has no row or a row has no hook. CI runs it (`.github/workflows/audit.yml`). **After adding, moving or removing a hook, run the script again.**
- `fear::image_check::Run()` (from `FearApp::OnPostLoadXexImage`) hashes the same bytes of the loaded image. A mismatch is logged per hook, then one line says this is not the supported executable, and the version badge stays on screen with a notice. The game still starts. Refusing to start is the stricter choice, left until the check has been through a full smoke test on both games and on Linux.

## Tripwires

`FEAR_HOOK_HOLDS("name", condition)` in a whole-function hook: when the condition is false it logs once ("the name hook found what its function shouldn't have") and the hook runs the game's function unchanged.

| Hook | Check |
|---|---|
| GetWeaponSlot | the capacity byte and the slot vector are a small count of 4-byte records |
| UpdateOnGround | `m_bJumped` is 0 or 1 |
| CollisionHandlerCB | the `bIgnoreCollision` reference isn't null |
| HandleMsgSlowMo | the state is 0 to 3 and the charge is a finite number |
| UpdateWaveProp, UpdateSlowMo, the two mouse hooks | none: nothing they read has a fixed range (noted in the code) |
| NextWeapon, PreviousWeapon | none: they only mark the cycle's direction for GetWeaponSlot |

## Unit tests

`port-nx/src/weapon_cycle.h` is the weapon cycle fix's slot choice, with no SDK includes. `port-nx/tests/weapon_cycle_test.cpp` applies the game's own step to each answer and checks the slot it lands on: named cases, then every way of filling 1 to 10 slots from every current weapon in both directions.

    pwsh -NoProfile -File tools/test.ps1

CI builds and runs them on Linux (`.github/workflows/tests.yml`). A new testable piece of arithmetic goes the same way: a header without SDK includes, a `_test.cpp` beside the others, one line in `port-nx/tests/CMakeLists.txt`. Candidates: `high_fps.cpp`'s 60 Hz wave stepping and slow-mo rules.

## Still owed

- The play test of weapon cycling: the steps are in `docs/ECHOPATCH-PUNCHLIST.md`, under #132.
- The startup check on Linux and the Steam Deck (the hashed bytes are the guest's, so the result should be the same).
- A tripwire seen firing in play is either a wrong match or a check that is too strict: the log line names the hook and the condition.

## Not planned

- Automated play tests. `tools/autoplay.ps1` taps keys on a timer; reaching a known weapon state and reading the result back is a lot of fragile work for one fix.
- Tests that call the translated game functions. They need the whole game's memory set up.
