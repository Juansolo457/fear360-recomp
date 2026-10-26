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
| 1 | Weapon capacity | #23 | DB | **Done**, confirmed in play 2026-10-25 (#23 closed) (`fear_weapon_slots`, PC Settings "Weapon slots": 3 as shipped, 4, 5 or 10). Sets `Arsenal/Global/Global` `MaxWeaponCapacity`. Check that the HUD's weapon list copes with more than 3 | S |
| 2 | Infinite flashlight | #20 | DB | **Done**, confirmed 2026-10-25 (#20 closed) (`fear_infinite_flashlight`, "Flashlight"). Sets `Client/Flashlight/Player_Narrow` `MaxCharge` from 30 to 0. In the PC code the player's flashlight drains only while `MaxCharge` is above 0, and the HUD hides the battery bar at 0. The AI's flashlights already have 0 | S |
| 3 | Skip intros | #19 | DB + hook | **Done**, confirmed 2026-10-25 (#19 closed) (`fear_intro_videos`, "Intro videos": all as shipped, Monolith logo only, or skip). Blanks entries of `Client/Shared/Shared` `SplashMovies` (ESRB, Sierra, Day 1, Monolith). The 360's splash loop, like the PC's, moves on from a movie that doesn't open, but it retried the empty names every frame for 8.7 s (the black screen), so with *Skip* a hook also makes the list's count 0 (`FearSplashMovieCount`): the menu comes at once | S |
| 4 | Bodies and glass stay | #22, part | cvar + DB | **Done**, confirmed 2026-10-25 (`fear_bodies_stay`, "Bodies and debris"). `BodyCapTotalCount` 10 and `BodyCapRadiusCount` 4 are console variables seeded from `Server/Limits`: set to 1000 in both games. The check clamps a count to 0 or more, so -1 was a cap of 0; that first try made every body fade about 3 s after death. Glass: `FX/ShatterType` `FadeOutStart` and `Lifetime` set to 1e9 s (F.E.A.R. only). Model decals need nothing: their `FadeDuration` is how long a fade takes, and they only go once over the limit | S |
| 5 | Gun model at wide FOV | #1, rest | cvar | **Done**, confirmed at 95° 2026-10-25 (#1 closed). With `fear_fov` set off 70, `CameraAttachedOffsetZ` follows EchoPatch's table (70° → 0, 80° → 5.75, 95° → 13, 128° → 23), interpolated, in both games | S |
| 6 | No model LOD bias | #21 | hook | **Done**, confirmed 2026-10-25 (#21 closed) (`fear_full_detail_models`, "Model detail"). `ModelLODDistanceScale` → 0.003 whenever positive, by hooks before the camera's five sets in each game (`FearModelLodScale`) | S |
| 7 | Faster saves | #24 | cvar + code | **Half done, not run** (`fear_fast_checkpoints`, "Checkpoint saves", on by default): `CheckPointOptimizeVideoMemory` → 0, so a checkpoint no longer runs the interface's DoOptimizeVideoMemory. Left: buffering the save's many small writes in our file layer, if a measurement shows them. Check against #36 (input drops at every autosave), which may be the same cost | M |
| 8 | Bullet holes, blood stains and shell casings stay | #22, rest | code | **Done, not run** (part of `fear_bodies_stay`). `FearCreateFxKey` at CClientFXMgr::CreateFXKey: EchoPatch's keys (every decal, every model but a rocket's debris, the stone bullet-hole, debris and Flesh_splat6 sprites) get end and lifetime FLT_MAX. `FearShellCasingStays` skips the casing's count-down. The 360's CreateFX takes the type's FX_REF, not its name, so the type is told by its create function (`src/look_tuning.h`). EchoPatch also stops saving model decals and lets them live forever; on the 360 they already live until the limit, so that part isn't needed | M |
| 9 | No cutscene letterbox | #18 | cvar | **Done**, confirmed 2026-10-25 (#18 closed) (`fear_letterbox`, "Cutscene black bars"). The game's own console variable `LetterBoxDisabled` starts at 1, in both games | S |
| 10 | HD reflections and displays | #25 | check | **Done, no code** (#25 closed): every host render target and resolve image is the game's size times the draw scale, so mirrors and displays render at 3x per axis at 4K. Confirmed by eye 2026-10-25 | – |
| 11 | Shotgun rumble | #68 (from #26) | check, then code | Compare at 30 fps first. If it's the frame rate, fix how the rumble effect fades | S–M |
| 12 | Slow-mo charge and timer | #13 | code | **Done, not run** (`fear_high_fps_fixes`, "High frame rate fixes", on by default). A player's period is clamped to the charge left, the server ends slow-mo once the client's charge is spent, and a server message can't lower a recharging meter or raise one in use. Addresses in `src/high_fps.h` | M |
| 13 | Water splashes | #6 | code | **Done, not run**: each pair of bodies splashes at most every 1/60 s (the water's contact callback) | S–M |
| 14 | Water physics | #5 | code | **Done, not run**: the per-frame wave update steps at 60 Hz, each water surface with its own remainder, at most 5 steps a frame | S–M |
| 15 | Jump kick | #11 | code | **Done, not run**: `m_bJumped` kept for 1/60 s after takeoff (CMoveMgr::UpdateOnGround) | S–M |
| 16 | Sliding on slopes | #12 | code | **Done, not run**: the friction's frame time is at least 1/60 s | S–M |
| 17 | Jump and water-exit momentum | #8, #9 | code | **Mostly done, not run**: the landing's camera-height damping is skipped (#9), and so is the zeroing of upward speed on the water's surface (#8), as EchoPatch does. Its other #8 half, keeping some upward speed in the engine's SetVelocity, is in the engine and isn't ported | M |
| 18 | Walk animation dropping to idle | #10 | code | Smooth the player's velocity over a short window before the walk/idle choice. EchoPatch's version is long | L |
| 19 | Ragdolls | #4, with #62 | code | Fixed 60 Hz physics steps with interpolation. EchoPatch's biggest fix | L |
| 20 | Weapon state after loading a save | #14 to #17 | check, then code | First check whether the 360 has each bug: zoom after a cutscene save, automatic weapons stuck firing, cycling past an empty slot, the gun model's position. EchoPatch's fixes are small. **#16 done, not run** (`fear_weapon_cycle_fix`, on by default, `src/weapon_fixes.*`): the 360's NextWeapon and PreviousWeapon are the PC's, so an empty slot sent the cycle back to slot 0; now they skip empty slots and wrap to the first or last weapon. It matters more now that "Weapon slots" allows 4, 5 or 10. #14, #15 and #17 still wait for the check | S each |
| 21 | Scripted animation crash | new | wait | F.E.A.R. only: a script asks for a bone of a character whose model is gone. EchoPatch returns a neutral transform. Not seen in the port; ported only if a smoke-test crash report shows it | S |
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

**Batch 3 (2026-10-25, on the D3D12 build, `native-renderer`):** items 7 (the console-variable
half), 8, 12 to 17 and #16 of item 20, written for both games and not yet built or run. What to
try in play, at 120 fps: a jump kick (jump, then melee in the air); walking down a slope and
stopping; jumping and landing while running; jumping out of water onto a ledge; water surfaces
with things falling in (waves calm, splashes not repeated); slow-mo until the meter empties (the
effect should end with it) and its recharge; next and previous weapon with an empty slot between
two weapons (with "Weapon slots" above 3); and with "Bodies and debris: Stay", bullet holes, blood
splats, debris and shell casings that stay. Each fix logs one "high frame rate fix in use" (or
its own) line the first time it acts. Left: items 18 and 19 (walk/idle animation, Havok ragdolls,
both long), the SetVelocity half of #8 (in the engine), the save write buffering of item 7, and
the weapon checks #14, #15 and #17.
