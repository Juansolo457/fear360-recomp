# Visual effects: what should be on screen

Research only (2026-10-23): static reading of the 360 images, the 360 catalogues and databases, the PC
archives and binaries, the PC SDK (game code, read-only) and EchoPatch's source. **Nothing was built or
run**, so every status below is "works" only where another doc already records it being played, otherwise
"unverified" (nobody has looked in the port) or "missing" (known not to exist yet).

Applies to `default.xex` SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792`
(F.E.A.R.). *F.E.A.R. Files* has the same strings and systems (checked by name, not address) plus the extras
in table 3. Addresses are F.E.A.R.'s unless marked.

## Result in short
The 360 and PC share almost all content: of the 2,647 materials with the same name, 2,534 use the same
shader; the ClientFX effect names match except 2 360-only names (one unused) and 1 PC-only name
(`prox_bluelight_1`). So the 360's extras are few,
and the real risk is **effects that depend on renderer features** the port may not fully provide.

1. **Scene-depth readers vs the skipped 2× MSAA depth copy-outs** (rows 4–7). Soft-shadow blur, fog volumes,
   murky water, ghost/dissolve all sample scene depth; [PROGRESS.md](PROGRESS.md) says the renderer skips
   those copy-outs ("if an effect later in the game looks wrong, this is the place"). Cheapest test: A/B with
   `--nx_native_msaa false`.
2. **Model decals on characters (blood/armour marks) probably start off** (L6). The PC applies "Maximum"
   (3) from its detail menu; the 360 code does not apply anything, and the cvar's own default is 1 = zero decals.
3. **LOD** (section "LOD"): engine knobs match the PC; world-object LOD filtering is gone from the 360 code
   (everything spawns); light/shadow/material LOD levels are fixed inside the 360 engine at an unknown value.
   EchoPatch's "No Model LOD Bias" ports at five call sites.
4. **Bloom**: with the strength at 0 the whole chain (downsample, two blurs, blend) still runs; and
   *Files* also has a fullscreen fog pass (`Bloom_Fog.mat`) inside the same manager (row 1).
5. **Water**: 18 materials use the planar-reflection water on the 360 where the PC uses the medium one (row 2).

**Issues (filed 2026-10-23):**
- scene-depth effects [#45](https://github.com/Juansolo457/fear360-recomp/issues/45)
- water [#46](https://github.com/Juansolo457/fear360-recomp/issues/46)
- model decals [#47](https://github.com/Juansolo457/fear360-recomp/issues/47)
- LOD [#48](https://github.com/Juansolo457/fear360-recomp/issues/48)
- bloom passes and the *Files* fog [#49](https://github.com/Juansolo457/fear360-recomp/issues/49)
- mipmap bias [#50](https://github.com/Juansolo457/fear360-recomp/issues/50)
- *Files*-only effects [#51](https://github.com/Juansolo457/fear360-recomp/issues/51)
- the remaining table 1 rows as a checklist [#52](https://github.com/Juansolo457/fear360-recomp/issues/52)

EchoPatch rows that already had issues stay there: [#25](https://github.com/Juansolo457/fear360-recomp/issues/25), [#21](https://github.com/Juansolo457/fear360-recomp/issues/21), [#22](https://github.com/Juansolo457/fear360-recomp/issues/22), [#18](https://github.com/Juansolo457/fear360-recomp/issues/18), [#1](https://github.com/Juansolo457/fear360-recomp/issues/1), [#4](https://github.com/Juansolo457/fear360-recomp/issues/4)–[#7](https://github.com/Juansolo457/fear360-recomp/issues/7), [#17](https://github.com/Juansolo457/fear360-recomp/issues/17).

## How to check (tools that already exist)
- Side by side with the real 360 and with the PC (EchoPatch), same spot, same action.
- `--nx_native_msaa false` (the renderer joins depth tiles only without MSAA).
- `--nx_native_trace_frame_every N`: per-draw trace with shader hashes (`vsh=`/`psh=`); `fear_debug_skip_shaders`
  hides a shader's draws by hash; `--dump_shaders` writes the translated microcode.
- `fear_console_vars_log` logs each game console variable as the game creates it, with its starting value;
  `fear_console_vars="Name=value,..."` starts one at another value (a variable the game sets again later keeps
  the later value). Used below as "log X".
- PC Settings: Bloom, FOV, draw-resolution scale.

## Table 1: what the 360 shows, and what could be missing or wrong
"On" is where the effect exists. Statuses as above.

| # | Effect | What it is | On | Status | Check |
|---|---|---|---|---|---|
| 1 | Screen bloom (`CBloomMgr`, `FX/Bloom`, `ScreenBloom`) | Quarter-size bright-pass (channel over 0.8), two 15-tap blurs, add-back at 0.2. *Files* adds a fullscreen fog pass in the same manager (`FX\Bloom\Bloom_Fog.mat`, shader `fog_fullscreen`, `vFogColor/fFogNear/fFogFar`; Files `0x82391184`) | 360 only | works (off by default, PC Settings → Bloom; [bloom.h](../port-nx/src/bloom.h)); the Files fog pass unverified | On: a lit wall glows over the dark sky beside it. Off still runs all four passes at strength 0: skipping them would save GPU. Files: check a foggy Files level with Bloom off |
| 2 | Murky water with planar reflection and depth fade | `murky_water_mirror`: reflection map, Fresnel table, scene depth. 18 shared water materials use it on the 360 (`FX/Water/WTF_*`, `Bishop_Evac_Tunnels_*`, `Streets_Sewer`, `Ecosphere_stream`, `E3_Demo_06Moody_1`) where the PC uses `murky_water_medium` (Files: 25 mirror, 6 medium) | 360 variant | unverified | Water levels (PolyGrid objects: WTF_Ambush/Entry/Exfil, Bishop_Evac, Moody, Vault, FactoryB): light reflections on the surface and a soft edge where water meets walls; compare with the 360, then MSAA off |
| 3 | Wet floors and mirrors | `specular_mirror` on 16 floor materials (Office/Research tile mirrors, `Docks_Asphat01_Ref`, puddles), `mirror.fx` (Asylum `Prefabs/Asylum/Mirror`, `Car_Mirror`); render-target groups (up to 9 per level by string count) | both | unverified | Tile floors reflect; the Asylum mirror shows you; compare sharpness at 1× and 3× (#25, table 2) |
| 4 | Soft shadows | Stencil shadow volumes plus a blur pass (`ShadowBlur_CopyStencil`, `ShadowBlur_BlurBuffer`, the latter samples **scene depth**) | both | unverified, high risk | Character shadows in flashlight or lamp light have soft edges, no banding or holes; compare MSAA off. Trace the `ShadowBlur_BlurBuffer` hash |
| 5 | Fog volumes | `fog_volume`, `fog_overlay` (scene depth) and the `sFogVolume_*` samplers inside refraction, feedback and mirror shaders | both | unverified, high risk | Steam/mist brushes: the fog should thicken with depth, not be a flat wash; compare MSAA off |
| 6 | Ghost and dissolve materials | `dissolve` (rigid + skeletal, **scene depth**): 24 character materials: `alma_dissolve`, `Harlan_*_Dissolve`, `Paxton_*`, `moody_*`, `deadHostage_*`, `hives_*`, `DeltaForce_Ghost`, `jankowski_Meatpuppet`, `alice_*`, `Headset_Dissolve` | both | unverified, high risk | The dissolve edge on Alma/Harlan/hive scenes; compare MSAA off |
| 7 | River, hair | `river` (2 materials: `ATC_Roof_River`, `River_Docks`) and `hair` (`PartnerHayer`) sample scene depth | both | unverified | River on the Rooftop and Docks has soft banks |
| 8 | Cloaks and ghosts (previous frame) | `feedback` / `feedbackblur` read last frame: `assassin_cloak`, `ghost_feedback`, `SpectralGirl_Feedback` | both | unverified | Cloaked assassins shimmer as outlines, not pop or flicker |
| 9 | Refraction | `refract`, `refract_additive` (14), `refract_blur` (7), `refract_dx9` (5), `refract` (9): heat shimmer, `Heli_Refract`, water rings (`RingA/B_*_Ref`), plasma impacts, `AlmaTank_Shield2`, `Use_Medkit`, `Beam_Refract`, glass, `Window_Rattle2` | both | unverified | Heat over fires, rotor wash, ripples where bullets hit water, medkit use; each distorts the scene behind it |
| 10 | Screen overlays | `screeneffect` (radial blur + sharpen, noise), `screeneffect_bleach`, `blur`, `blur_Alpha`: slow-mo (`Hud_SloMo_In/Loop/Out/Spotty`, `ScreenEffect_SloMo`, `OV_SloMoBlur`), death (`OV_DeathFX*`), melee hit (`CameraMeleeDamageOverlay` sets `fBlurScale` on the damage overlay; camera roll), sniper scope (`OV_Scope_*`), turret HUD (`OV_Turr_*`), flashes (`OV_Flash*`) | both (`blur_Alpha` 360 only) | unverified | Slow-mo on/off, die, take a melee hit, zoom a scope, ride a turret |
| 11 | Volumetric light | Nine slice/shell shader variants (plain, noise, shadow, directional): flashlight beam, window shafts. Cvar defaults equal the PC's code defaults: `EnableVolumetricLight` 1, `VolumetricLightSlices` 256, shadow on at 128, noise on | both | unverified | Flashlight beam through dust; log `VolumetricLightSlices` (256) |
| 12 | In-game video | `video`, `translucent_video` and `DrawPrimModulateVideo` draw Bink as separate Y/Cb/Cr/A planes: 17 materials (`Materials/Videos/*`, `ComputerMonitor`, `Security_Camera`, `flag_monitor_*`, `NukeBall`, `Nuke_Wall`, `Aftermath_cloud`). PC: plain textured materials, no such shaders | 360 shader | unverified | Armacham and Origin screens play with correct colours; the briefing monitor; the nuke flash |
| 13 | Brightness, contrast, gamma | `Gamma` 1, `Brightness` 0, `Contrast` 1 in the 360 engine; calibration screens in the options. The renderer applies the guest gamma ramp at swap (`SetGammaRamp256`, an apply-gamma compute pass) | 360 UI | unverified | Options → Video sliders visibly change the picture |
| 14 | HUD | XUI scenes (`XUIEnable`, `Interface/XUIConfig`, `CXUIHud*`): health, instinct, sprint, taser, berserk, crosshair, hints, forensic menu | 360 look | works (scales with the frame; sharper text is #42) | Compare each element with the 360 |
| 15 | Bump on debris and trims | 30 debris materials (`FX/Debris/Debris_Wood01`…) and 12 city trims use `*_bump` shaders on the 360 only | 360 only | unverified, low value | Crates splinters lit by the flashlight |
| 16 | Pickup glow, menu highlight | `ModelGlow*` (pickup pulse), `HighlightGlow*`, `TextGlow`: game code, same as the PC | both | unverified | Pickups pulse; menu highlight glows |
| 17 | `ScreenGlowEnable/Intensity` | Set by the melee-hit code but read by nothing: the string occurs in no engine code on the 360 (references only in the client) and not in the PC's `FEAR.exe` either | dead on both | n/a | Nothing to port |
| 18 | `MFlash_Missile` | First-person muzzle flash of the Missile Launcher (`PVMuzzleFX` in the three `Arsenal/Weapons` Missile Launcher records); the PC's ClientFX files have no such effect | 360 only | unverified | Fire the Missile Launcher in first person: a flash at the muzzle |
| 19 | `Beam_Lasersight` | In the 360 ClientFX files only; no database value and no string in the code names it | 360 only, unused | n/a | Nothing to port |

## LOD and detail levels (asked 2026-10-23: "LOD is really low on the 360")
The PC drives these from its detail menu (`Performance/Options`, applied by `CPerformanceMgr`: GPU group
"Medium", CPU group "High" when nothing is saved). The 360 keeps the table in its database but its code
applies none of it: `CPerformanceMgr::Load` (`sub_8249E910`) returns at once when the profile has no
Performance record (`0x8249E95C`), and `ProfileDatabase/Defaults.gamedb` has none. So every setting is
whatever the code, the engine or the data fixes.

| # | Setting | What the 360 does | PC | Status | Check |
|---|---|---|---|---|---|
| L1 | Model LOD distance (`ModelLODDistanceScale`, `ModelLODOffset`, `ModelLODDistanceBias`) | Engine table at `0x830A51A8`: scale 1.0, offset 0, bias 0, same as the PC's code; the camera sets the scale from the zoom (same code; five places build the name, at `0x823B40D0`, `0x823B626C`, `0x823B6728`, `0x823B8428`, `0x823B9100`) | same code | unverified | The models' own LOD tables may differ: of 400 models in both, 93 have byte-identical size (`Assasin_Base` has identical header words), 307 differ (`enemy_combat` 1.34 MB vs 1.47 MB; could be animation, could be LODs). Compare a soldier at mid range with the 360 and the PC; a model parser would settle it |
| L2 | First-person arms and weapon | `CameraFirstPersonLODBias` −1,000,000 (always the best LOD), `ChaseCamLODBias` 0 | same | works (by code) | |
| L3 | World-object LOD | The PC tags objects `ObjectLOD` Low/Medium/High and drops those above the "World Detail" level. The 360 levels keep the tag (`ATC_Roof`: 138 Low, 10 Medium, 2 High) but the 360 code has no `ObjectLOD` or `WorldDetail` string, so nothing filters: all objects spawn (the PC's Maximum) | filtered | unverified (read from strings, not traced) | Count props in a cluttered room against the PC at Maximum |
| L4 | Light, shadow and material LOD (`LODLights`, `LODShadows`, `LODMaterials`, `Light_ShadowVolume`, `Light_ShadowBlur`) | **None of these strings exist in the 360 image**, and `AddAmbientLight*` (the PC's ambient compensation for lower light LOD) is gone too: the engine fixes the levels internally. Value unknown. Objects still carry per-light LOD (`LightShadowLOD`, `VolumetricLOD`, e.g. the flashlight's `CharacterFX` shadow `Never`) | cvars | unverified, **most likely place for "LOD is low"** | In a fight, compare the number of dynamic lights and shadows (frame trace: shadow-volume draws) with the PC at Maximum and with the real 360 |
| L5 | Effect detail (`ClientFXDetailLevel`) | Starts at 2 = Maximum, nothing lowers it | Maximum | works ([PROGRESS.md](PROGRESS.md)) | |
| L6 | **Model decals** (blood and armour marks on characters; `ModelDecalPerformanceLevel`) | Starts at 1 = "0 - Low" in `FX/ModelDecal/Settings`: PerModel, PerSecond and Total all 0, so **no decals**. The PC detail menu sets 3 (10 per model, 200 total) | 3 at Maximum | unverified, likely missing | Shoot a soldier: marks on body and armour (`FX/Impacts/ModelDecals/ModelDecal_Blood*`)? Does the real 360 show them? Log `ModelDecalPerformanceLevel` (starts at 1); try `fear_console_vars="ModelDecalPerformanceLevel=3"` |
| L7 | Texture LOD (`TextureGroupOffset%c`) | Never created on the 360, so the offsets add 0: full-size mips (the textures themselves are lower resolution: texture pack) | offsets 0/0/0/1 at Maximum | works | |
| L8 | Render-target LOD | `RenderTargetLOD` 2 = High | same | works (by code) | |
| L9 | Water mesh, particle bounce, shells | `PGResolutionScale` 1, `ParticleBounceScale` 1, `ShellCasings` on | Maximum | works (by code) | |
| L10 | Bodies | `Server/Limits` Medium: 10 total, 4 within 500 units (Maximum has no cap) | Maximum | known ([PROGRESS.md](PROGRESS.md)); see EchoPatch "persistent world state" | |

## Table 2: EchoPatch's visual features (issues #2, #25)
EchoPatch's INI/README/source read as data; nothing copied.

| Feature | What EchoPatch does | 360 code | Status | Porting idea or check |
|---|---|---|---|---|
| Supersampling (`SSAAScale`) | Renders the scene larger and downscales; turns the game's FSAA off (on the PC it conflicts with soft shadows) | n/a | works | `nx_native_resolution_scale` 1–4 plus the game's own MSAA (checked at 2×/3×). Integer steps only; EchoPatch allows 1.5 |
| HD Reflections & Displays (`HighResolutionReflections`, #25) | Hooks the render-target-group effect's init and multiplies all six size fields (Low/Med/High width and height) by 4 | yes: the same `RenderTargetGroup` objects (properties `LowTextureWidth/Height`, `MedTextureWidth/Height`, `TextureWidth/Height`, `FogVolumes`, `LastFrameEffect`, `CurrFrameEffect`), up to 9 per level, `RenderTargetLOD` 2 | unverified | The renderer already scales render targets and resolve images by the draw scale, so at 3× these should be 3× per axis: confirm in a frame trace, compare sharpness at 1× and 3×. If not scaled, hook the group's init the same way (watch the 10 MB EDRAM limit: tile) |
| Aspect Ratio Blur Fixes (`FixAspectRatioBlur`) | Swaps in replacement shaders (soft-shadow blur, `blur`, `mirror`, `screeneffect` and `_bleach`, all `refract*`, `feedback*`, depth of field, neon) whose kernels follow the resolution instead of 4:3; also fixes slow-mo flicker with soft shadows | the 360 has its own compiled shaders for 16:9 720p; no 4:3 bug expected | unverified | The port feeds the guest's UV-space constants, so the look should scale with the frame. Check soft-shadow edge width and the slow-mo radial blur at 1× and 3×, and slow-mo flicker with shadows. Matters again only if an ultrawide mode is added |
| Improved Mipmap Bias (`ReducedMipMapBias`) | Lowers the mip bias by texture class: characters, FX, guns, UI, vehicles, security/elevator props and fences kept sharp, the rest a moderate bias | the 360 samples with its fetch-constant bias; the port overrides anisotropy | missing | Sampler `mipLodBias` in the texture cache keyed by catalogue name prefix (`chars\skins\`, `FX\`, `guns\`, `Interface\`…). At 3× UV derivatives already pick finer mips, so mostly a 1×/2× gain |
| No Model LOD Bias (`NoLODBias`, #21) | Rewrites any positive `ModelLODDistanceScale` to 0.003 and `CameraFirstPersonLODBias` to 0 | yes (L1, L2) | missing | A hook on the five `ModelLODDistanceScale` set sites (F.E.A.R. `0x823B40D0`, `0x823B626C`, `0x823B6728`, `0x823B8428`, `0x823B9100`) or the engine's console-variable setter: force 0.003. `CameraFirstPersonLODBias` is already −1,000,000 here |
| Persistent World State (`EnablePersistentWorldState`, #22) | Stops bodies, blood stains, debris, bullet holes, shell casings and glass shards despawning | yes: `Server/Limits` body caps, decal lifetimes in the database and FX | missing | Body caps are database values: the stick-tuning override (`look_tuning`) already patches `Loki.gamedb`; add `BodyCap*` = −1 and the decal fade settings. Combine with L6 (model decals) |
| Custom FOV (`CustomFOV`, #1) | Overrides the 70° FOV and corrects the weapon model for wide FOVs | `FovY` 70 as on the PC | works (`fear_fov`); the viewmodel correction unverified | Check the weapon model at 100°+; if it stretches, scale the weapon's FOV |
| Disable Letterboxing (`DisableLetterbox`, #18) | Removes the cutscene bars | the game's own cinematic bars | missing | Find the bar draw or its size property (client, "Cinematic") and hook as a PC Settings option |
| HUD Scaling | Scales HUD text, crosshair and icons to the resolution | the HUD draws at 720p guest space and scales with the frame | works | Sharper text at high scale is #42 |
| High FPS fixes, visual parts | Oversized particles (batch-marker lifetime), excessive water splash repetitions, frozen FX | batch markers: ported (4f1a7b5); splashes: #6; the 360's catch-up rule makes frozen FX a non-issue | oversized particles unverified; splashes missing | Check dust and impact puffs at 120 fps; water splash counts per shot |
| Havok interpolation (`HavokPhysicsFix`) | Fixed 60 Hz physics with interpolation for smooth motion at any frame rate | yes (#4, #5) | unverified | Watch ragdolls, props and debris at 120 fps for stepping; stability is #4/#5 |
| Weapon model after load (`WeaponFixes`) | Weapon model position not updated after loading a save | yes (#17) | missing | |
| Not applicable | Nvidia shadow fix, dynamic vsync, windowing, widescreen filter for Extraction Point, the developer console, DXGI VRAM | the port's renderer is its own | n/a | |

## Table 3: *F.E.A.R. Files* extras (not in the base 360 game)
Shaders present in the Files catalogue and not in F.E.A.R.'s, with the one material using each.

| Effect | What it is | Status | Check |
|---|---|---|---|
| `nightvision` (`FX-XP/Hud/Adv_Rifle_scope`) | Night-vision scope | unverified | Zoom the advanced rifle |
| `shadow_creature*` (`CharsXP/Materials/shadow_creature_halo`, `…_head_blur`, `ModelDecal_ShadowCreatureHit`) | Perseus Mandate shadow creatures: halo, head blur, hit decal | unverified | The shadow-creature scenes |
| `motion_trail` (`CharsXP/Materials/NCE_dodge`) | Dodge trail | unverified | The dodging enemy |
| `morphing_decal` (`FX-XP/Scorchmarks/Shade_Stain01`) | Morphing stain | unverified | |
| `skybox_void` (`Materials/Church/Void`) | Void sky | unverified | The church void |
| `fog_fullscreen` (`FX/Bloom/bloom_Fog`) | Fullscreen fog from the bloom manager (row 1) | unverified | |
| PC-origin: `depthoffield`, `neon_outside`, `glass_flat` (4), `specular_detail` (20), `twotonepaint` (5), `murky_water_high`, `refract_water_mirror` | Expansion content | unverified | XP2 research-facility floors and glass; church cars |

## Method and evidence (for follow-ups)
- Strings: `tools/analysis/x360img.py … str`; code references with `lis`; defaults read from `lfs` operands
  (e.g. `RenderTargetLOD` default `0x82004824` = 2.0; `ModelDecalPerformanceLevel` init `0x823467AC`, default
  `0x82001DB4` = 1.0). Engine render cvars are a table at `0x830A51A8` (name, hash, link, value).
- 360 catalogue read with the `uberCatalog.cat` format in [TEXTURES.md](TEXTURES.md); `.mat` files start with the
  shader path; `.fxo` files carry sampler names (`sDepthMapSampler` in 15 shaders, `sCurFrameMapSampler` 15,
  `sLastFrameMapSampler` 6, `sScreenLastFrameMapSampler` 2 = bloom). PC archives (`FEAR*.Arch00`, `.Mat00`)
  read the same way and compared by material name.
- PC SDK game code (not the engine source) for `CPerformanceMgr`, `ModelDecalMgr`, `PlayerMgr` melee damage,
  `RenderTargetGroupFx`, `VolumetricLightFX`; PC `FEAR.exe`/`GameClient.dll` strings for the engine side.
- Not done: no model-format parse (L1), no trace of the engine's own light/shadow LOD (L4), no run of anything.
