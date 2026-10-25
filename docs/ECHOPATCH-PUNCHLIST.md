# EchoPatch punch list

Written 2026-10-25. What's left of EchoPatch parity (#2), as a list to work through before the full
smoke test (#55 to #57). Gyro aiming is out of scope; rumble stays.

Each item says how the 360 version can get it. This comes from reading the 360 game database
(`Game/database/Loki.gamedb`, with `tools/gamedb.py`) and EchoPatch's source, which is GPL-3.0 and
was read as a reference only.

## How a change goes in

- **Database (DB):** the value is in the game database. F.E.A.R. has the database as a loose file,
  so a patched copy goes to `<user data>/overrides/`, as `look_tuning.cpp` already does for the
  stick tuning. F.E.A.R. Files keeps its database inside `uberCatalog.cat`, so the same changes
  there need one new piece: a hook on the game's database reads. That hook would cover every DB
  item in this list at once.
- **Console variable (cvar):** the value starts as a console variable. `FearConsoleVarDefault`
  sets the starting value in both games. A variable the game sets again later needs a hook where
  it's set.
- **Code:** a hook in the game code, like the AI fix.

## The list

Quick items first. Size means how much work: S is a few hours, M a day or two, L several days.

| # | Item | Issue | Kind | How on the 360 | Size |
|---|---|---|---|---|---|
| 1 | Weapon capacity | #23 | DB | `Arsenal/Global/Global` `MaxWeaponCapacity` 3 → an option. Check that the HUD's weapon list copes with more than 3 | S |
| 2 | Infinite flashlight | #20 | DB | `Client/Flashlight/Player_Narrow` `MaxCharge` 30 (`RechargeRate` 5). The AI's flashlights use `MaxCharge` 0, likely "no battery"; try 0, else a huge charge | S |
| 3 | Skip intros | #19 | DB | `Client/Shared/Shared` `SplashMovies`: ESRB, Sierra, Day 1 and Monolith. Empty the list, or keep only the ESRB card | S |
| 4 | Bodies, glass and body marks stay | #22, part | DB | `Server/Limits` `BodyCapTotalCount` 10, `BodyCapRadiusCount` 4 (within `BodyCapRadius` 500) → unlimited; `FX/ShatterType` `Lifetime` 20/10 and `FadeOutStart` 15 → never; `FX/ModelDecal` `FadeDuration` 10 → never | S |
| 5 | Gun model at wide FOV | #1, rest | cvar | `CameraAttachedOffsetZ` from the FOV, by EchoPatch's table (70° → 0, 80° → 5.75, 95° → 13, 128° → 23) | S |
| 6 | No model LOD bias | #21 | cvar + hook | `ModelLODDistanceScale` → 0.003 whenever positive. The camera sets it at five places (`docs/VISUALS.md`, table 2), so it needs a hook there | S |
| 7 | Faster saves | #24 | cvar + code | `CheckPointOptimizeVideoMemory` → 0, and buffer the save's many small writes in our file layer. Check against #36 (input drops at every autosave), which may be the same cost | M |
| 8 | Bullet holes, blood stains and shell casings stay | #22, rest | code | They fade by their effect keys, not the database. EchoPatch sets the lifetime of Decal, LTBModel and Sprite effects at creation | M |
| 9 | No cutscene letterbox | #18 | code | Find the client's letterbox setup (EchoPatch patches one function) | S–M |
| 10 | HD reflections and displays | #25 | check, then code | First check whether mirror and screen targets already scale with the draw resolution. If not, multiply the `RenderTargetGroup` sizes: a world-object change like PitFight's, or a hook at its init | S–M |
| 11 | Shotgun rumble | #68 (from #26) | check, then code | Compare at 30 fps first. If it's the frame rate, fix how the rumble effect fades | S–M |
| 12 | Slow-mo charge and timer | #13 | code | Keep the server's charge in step with the client's, and limit a slow-mo period to the charge left | M |
| 13 | Water splashes | #6 | code | One splash per pair of bodies per 30 fps frame time | S–M |
| 14 | Water physics | #5 | code | Step the wave simulation at a fixed rate | S–M |
| 15 | Jump kick | #11 | code | Hold the jump state for a few frames after takeoff | S–M |
| 16 | Sliding on slopes | #12 | code | Limit the frame time used while friction slides the player | S–M |
| 17 | Jump and water-exit momentum | #8, #9 | code | Keep upward velocity through the per-frame damping (EchoPatch's `SetVelocity` hook) | M |
| 18 | Walk animation dropping to idle | #10 | code | Smooth the player's velocity over a short window before the walk/idle choice. EchoPatch's version is long | L |
| 19 | Ragdolls | #4, with #62 | code | Fixed 60 Hz physics steps with interpolation. EchoPatch's biggest fix | L |
| 20 | Weapon state after loading a save | #14 to #17 | check, then code | First check whether the 360 has each bug: zoom after a cutscene save, automatic weapons stuck firing, cycling past an empty slot, the gun model's position. EchoPatch's fixes are small | S each |
| 21 | Scripted animation crash | new | check, then code | F.E.A.R. only: a script asks for a bone of a character whose model is gone. EchoPatch returns a neutral transform. Matters for the smoke test | S |
| 22 | Particle sizes at 120 fps | #7 | check | Ported (3a2470e); look at dust and impact puffs | check |

## Left out

- **Gyro aiming** (#26): skipped.
- **Hip-fire accuracy penalty off:** an EchoPatch extra, not parity.
- **Per-texture-class mipmap bias:** a refinement of #50, optional.
- **Everything PC-only:** listed in #2.

## Suggested order

1. **Items 1 to 5:** one patched-database change plus one console variable, one build. Rows 1 to 4
   in F.E.A.R. right away, and in F.E.A.R. Files once the database-read hook exists.
2. **Items 6, 9, 10, 11 and 21:** small hooks and checks.
3. **High frame rate:** 12 to 17, then 18 and 19.
4. **Item 20:** the save/load weapon checks, during the smoke test.

Then quick testing, and the full smoke test of both games.
