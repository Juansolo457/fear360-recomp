# Code review and optimisation pass (2026-10-22)

An adversarial pass over what this project adds on top of ReXGlue and fear-nx: the hooks in the
translations, the host modules in `port-nx/src/`, our changes to fear-nx's renderer
(`port-nx/src/native/`, listed in `UPSTREAM.md`), and the scripts. Written for a later
review in another context: each finding has the evidence, the files, and what to do.

**Method.** Static reading of the code, plus the renderer's own 10-second reports from the
play-test logs (F.E.A.R. Files at 4K/80 fps, `port-files/logs/run-20261022-123605.log`). The
game was not run for this pass (the user's rule: no test runs without asking). One change was
made in this pass and one right after (marked **Changed**); both are type-checked with the
build's flags, not yet built and played.

## Headline: throughput is fine, hitches are the problem
At 4K and 80 fps (a 12.5 ms frame) the renderer reports, per frame: **renderer CPU 0.4 to
1.6 ms, GPU 0.3 to 2.8 ms**, 400 to 700 draws, vkQueueSubmit 0.06 ms, present 0.17 ms. There is
an order of magnitude of headroom. What the player feels are one-off slow frames (50 to 440 ms),
and the renderer names their causes itself ("slow frame N ms: ... N shaders, N pipelines,
N texture uploads"). In that 6-minute session: 12 of 37 ten-second windows had a frame over
50 ms. The causes, in order:

1. **First-sight shader translation and pipeline creation** (most of them).
2. **Texture uploads** when an area comes into view, made worse at 80 fps by eviction (fixed).
3. **The game's own file loading** on its thread ("ring idle 64.5 ms" followed by the game
   opening videos and sound banks): the game's design, not ours.

## Findings, by priority

### P1. Shader translations are redone every session, one at a time
- *Where:* `port-nx/src/native/native_renderer.cpp`, `EnsureTranslated` / `TranslationThread`
  (around line 2290), `fear_shader_wait_ms` (line 102), skip reasons 6 (translation pending,
  ~line 3112) and 13 (pipeline pending, ~line 3285).
- *Evidence:* "shader translations and pipelines: 58 waited for (493.9 ms), 313 draws skipped
  (over the frame's wait budget)"; "slow frame 104 ms: ... 17 shaders, 4 pipelines";
  "skipped by reason: 6:109 13:108". Skipped draws are objects missing for a frame or two.
- *Why:* Xenos microcode is translated to SPIR-V on first use in every run; only the driver's
  pipeline cache persists (`cache/shaders/driver`). Translation runs on a single worker by
  design ("the first completed translation of a shader fills the shader's binding lists"), so
  a burst of 10 to 20 new shaders (a new effect, a new area) queues up behind one thread.
- *Options, cheapest first:*
  a. **Pre-warm list:** record each (ucode hash, shader type, modification) the first time it
     is translated, in the title's cache folder; at the next start, rebuild those shaders from
     stored microcode and translate them on the worker in the background. Needs the shader
     objects to be created from stored ucode the way the ring thread creates them; the
     single-thread rule is kept.
  b. **Persist the SPIR-V** with the binding lists the translator fills; load instead of
     translating. More plumbing (serialising the bindings), no translation at all.
  c. **More translation threads**, serialised per shader (not per translation). Shortens
     bursts; keeps first-sight cost.
- *Note:* the pipeline cache is already persisted and saved off-thread once pipelines settle
  (`SaveDriverCache`); with pre-warmed shaders, the pipeline step would also move earlier.
- **Changed (option a, same day):** `fear_shader_prewarm` (default on). `EnsureTranslated`
  records each translation a run asks for (the shader's microcode hash, type, microcode and the
  modification) in `prewarm`; the list is saved next to the driver cache
  (`cache/shaders/driver/prewarm-<title>-<vendor>-<device>.bin`, magic + size + XXH3, written to a
  temp file and renamed) once new ones stop appearing for 5 s, and at shutdown.
  `InitializeDriverCache` (before the guest runs, like the driver cache) loads it, creates the
  shader objects `LoadShader` will find by hash, analyses them, and queues their translations on
  the existing single worker in `prewarm_jobs`; the worker takes `translation_jobs` (draws
  waiting) first, and a draw that needs a still-queued pre-warm translation moves it to the
  front. The first run only records. Type-checked with the build's flags, not run yet.
  *To verify:* run twice through the same area; the second run logs "Shader pre-warm: N
  translations of M shaders queued" and "Shader pre-warm done: N translations in X s", and its
  "shader translations and pipelines: N waited for" and "skipped by reason: 6:" counts drop.
  Pipelines (skip reason 13) are not pre-warmed; the driver cache keeps them fast to create.
  **Verified (2026-10-22, F.E.A.R., two runs):** the second run logged "Shader pre-warm: 148
  translations of 110 shaders queued" and "done: 148 translations in 0.1 s" at startup. Shaders
  translated inside slow frames: 61 in the first run, 16 in the second (most of the second run was
  the boot and level load, where these spikes are; its 333 ms load frame had 0 shaders, 23
  pipelines). The user: the opening felt smooth. What's left in the load spikes is pipeline
  creation (23 to 33 per spike, with the driver cache) and texture uploads; the 16 shaders were
  new to the list. Next: record pipeline keys the same way and create them in the background.
- **Changed (pipelines, same day):** the pre-warm file (version 2) also holds the key of each
  pipeline a run creates (`NotePrewarmPipeline` in `GetPipeline`). The key names everything a
  pipeline needs: both shaders (hash and modification), the geometry shader's key, the layout's
  binding counts, the formats and fixed-function state. At the next start the keys wait in
  `prewarm_pipeline_pending`; each frame `PrewarmPipelinesStep` (ring thread, up to 64 keys a
  frame) turns those whose shaders are translated into jobs for the pipeline worker, which takes
  the jobs draws are waiting for first; a draw that needs a queued one moves it to the front. A
  version-1 file still loads its shaders. A build whose `PipelineKey` changed size skips the
  keys. Type-checked, not yet run. *To verify:* two runs again (the first writes version 2);
  the second logs "Shader pre-warm: ... N pipelines to follow" and "Pipeline pre-warm done", and
  its load spikes show few pipelines ("slow frame N ms: ... N pipelines").
  **Verified (2026-10-22, F.E.A.R., two ~18-minute runs through the same Docks stretch, 4K/80
  fps, texture pack):** run 2 logged "220 translations of 158 shaders queued, 212 pipelines to
  follow" and had them all built 0.1 s in. Draws waiting for a shader or pipeline: 220 waits
  (275 ms) and 4 skipped draws in run 1; 4 waits (1 ms) and none skipped in run 2. Frames over
  50 ms during play: run 1 had 6 (50-125 ms; 42 pipelines and 3 shaders created inside them),
  run 2 none: every 10-second window after the first 30 s had its longest frame at 12.5-13 ms.
  Run 2's 5 slow frames (69-219 ms) are all at boot and level load with the ring idle for the
  whole frame (the game itself loading, 25-36 draws: the loading screen), 0 shaders, 0
  pipelines. What remains is the game's own loading (P2 above).

### P1. Texture eviction counted frames, not time — **Changed**
- *Where:* `port-nx/src/native/native_textures.cpp`, `TextureCache::BeginFrame` (eviction at
  the end), state `eviction_marks` in `Impl`.
- *What was wrong:* textures unused for 600 frames were evicted, "about 20 seconds" at the
  Switch's 30 fps but **7.5 s at 80 fps**. Turning around or walking back into a room a few
  seconds later re-created its textures (texture-pack ones re-read from disk at 4x the size)
  while draws waited up to `fear_texture_stream_wait_ms` (100 ms).
- *Change:* every 60 frames a (frame, time) mark; textures whose last use is older than the
  newest mark at least 20 s old are evicted. Same as before at 30 fps.
- *To verify:* compile; play an area, leave it for 10 to 15 s, come back: no "slow frame ...
  N texture uploads" on return; `live_textures` in the report settles rather than climbs.
  VRAM use rises a little (textures live 20 s instead of 7.5 s at 80 fps).

### P2. The game's own loading stalls
- *Evidence:* "slow frame 67 ms: ring idle 64.5 ms, pacing hold 0.0 ms" with the game opening
  `D:\Game\videos\*.vid` and `D:\Game\worlds\...\*.xsb/.xwb` right after (some of them missing
  on the disc: `NtCreateFile FAILED ... 0xc000000f`, the 360 game probing optional files).
- *Meaning:* the guest thread is busy (file I/O, decompression) and submits no GPU work. The
  360 hitches here too. Possible later work: measure the time inside the VFS for these opens
  and reads; make failed lookups cheap; a read-ahead cache for the catalogue.

### P2 → fixed. A dynamic vertex range rewritten mid-frame can be drawn stale
- **Confirmed and fixed (play-test 5):** this was the bullet-tracer glitch above ~90 fps. With
  `fear_vertex_exact` (now on by default) every range that has ever changed is read again for
  each draw; the 11-minute 120 fps run counted ~408,000 draws the old check would have drawn
  stale and the tracers were clean. Cost: +1.9 ms renderer CPU a frame (hashing ~8 GB/s of whole
  ranges).
- **Spans (#27, written, not yet built):** in heavy F.E.A.R. Files Instant Action fights the
  per-draw reads reached 25–30 GB/s (8–12 ms renderer CPU a frame, frames at 67–90 fps): many
  ranges run to the end of a shared ring the game rewrites constantly, so they stay "recent".
  Now a range on per-draw reads is hashed and copied only over the vertices the draw can reach,
  when the vertex shader provably fetches it at the vertex index (see UPSTREAM.md); the
  10-second report gives "vertex spans: N reads, X MB hashed instead of Y MB".
- **Made cheaper (same day, not yet built):** the cost came from the rule "re-read every range
  that has ever changed (or is under 64 KB) on every draw": a range that changed once, such as
  memory reused at a level load, stayed on per-draw reads for good. Now a range is re-read per
  draw only while it changed in the last 120 frames (~1 s), or if it is 4 KB or less (a
  tracer's quad: right from its first rewrite). `fear_vertex_audit` (off) checks the rule: every
  copy's content is kept when it is made, and each draw that uses a copy compares it with guest
  memory; the 10-second report gives the draws that used changed data. *To verify:* one run with
  `--fear_vertex_audit true` (stale count near 0, tracers clean), then renderer CPU per frame
  without it (expected back near 1 ms).
- *Where:* `native_renderer.cpp`, `RangeHash` (~line 628) and the fetch-constant resolve
  (~line 3578: `fetch_resolved_frame`, `vertex_hashes`).
- *How:* a vertex range is hashed (or, once it changes every frame, given a per-frame hash
  without reading it) once per frame and its copy is shared by every later draw of the same
  (address, size) in that frame. If the game rewrites that range mid-frame (its dynamic vertex
  buffer wrapping around), later draws read the first copy.
- *Evidence that it can happen:* the 360's particle renderer gets partial dynamic-buffer locks
  (a wrap) in under 1% of particle renders (`fear_debug_weapon_fire`'s per-shot lines,
  PROGRESS.md open item 12). Not visible in play (the user compared with a real 360).
- *Cheap mitigation:* keep a small sample hash (first 256 bytes) of the range with the
  per-frame hash; on a same-frame re-request, re-sample, and if it differs give the range a new
  hash so it is copied again. Only helps if the draw's fetch base is where the new data starts
  (D3D stream offsets); if the game uses index offsets into one fetch range, it doesn't.

### P2. Display pacing edge cases
- *Where:* `port-nx/src/display_vblank.cpp`, `native_system.cpp` (`DisplayVblankLoop`).
- The vblank is always taken from the **primary** display (`MonitorFromPoint(0,0)`); with the
  game on another monitor (`monitor` cvar) it paces against the wrong display.
- The refresh rate is `dmDisplayFrequency`, an integer (59.94 reads as 59, 143.9 as 143). Only
  the reported rate and `video_mode_refresh_rate` are off; frames are held by counting real
  vblanks.
- Variable refresh (G-Sync/FreeSync) makes vblanks irregular; the loop then falls back to the
  timer (by design, logged).

### P3. Smaller things
- **Animation-time carry** (`port-nx/src/anim_timing.cpp`): a mutex and a hash-map lookup per
  model per frame, and per tracker whose rate isn't 1. Tens of microseconds a frame at most.
  If it ever shows in a profile: a flat open-addressed table, or per-thread maps.
- **Debug hooks are compiled into every build** (`fx_debug.cpp`: 18 effect-creation hooks,
  particle emission/render/update hooks, fire and impact). Off, each is one call and one
  load from a static; the particle ones run a few thousand times a frame: a few
  microseconds. Could move to a debug manifest section if the codegen ever supports it.
- **Pipeline cache save** copied the cache (tens of MB) with `vkGetPipelineCacheData` on the
  render thread before handing it to a writer thread: a one-off few-ms hitch after new
  pipelines settle. **Changed:** the copy is made on the writer thread (`SaveDriverCache`).
- **GPU ring waits** spin-yield up to 64 times before sleeping 50 us (`native_system.cpp`,
  WAIT_REG_MEM): keeps a core busy while the GPU idles. Upstream design (fear-nx).
- **Settings file written in place** (`fear_settings.cpp`, `WriteSettings`): a crash mid-write
  leaves a truncated `settings.toml`. Write to a temp file and rename. (That file is being
  refactored for the collection launcher; fix it there.)
- **XInput on empty slots is slow** (milliseconds per call on some systems): the settings
  panels poll connected slots every frame and empty ones once a second, only while a panel is
  open or its buttons are being released. Fine; keep it out of per-frame game paths.
- **Crash reporter** logs through the logger and builds strings inside the exception handler
  (`crash_report.cpp`): if the crash is inside the logger or the heap is corrupt it can
  deadlock or fail. Acceptable for a development tool; a pre-allocated, logger-free path would
  be safer.
- **Renderer warnings worth a look:** "Depth resolve not done: 768x720, 2x MSAA" (once per run;
  open item 6: MSAA depth copy-outs are skipped) and "D24S8 depth bias: legacy format-dependent
  offset (exact D24 bias unavailable)" (possible z-fighting or shadow acne differences).

## EchoPatch, mapped onto the 360 code
EchoPatch's high-FPS fixes are tuned for the PC at up to 300 fps. We cap at 90 fps (the 360's
tracers break above that), so most apply less. Status:

| EchoPatch fix | 360 version |
|---|---|
| Frozen FX (the 1 ms ClientFX update skip) | The 360's `HandleUpdateTime` (`sub_82BAE3B8`) skips under **5 ms**: matters only above 200 fps. Not needed at our cap. |
| Oversized particles (batch markers' lifetime -1 -> 0) | Same marker code on the 360 (`AddParticleBatchMarker` `sub_82BB65E0`, `EmitParticleBatch` `sub_82BB6E40`). Markers are removed in the same update that adds them, so they shouldn't be drawn; not seen. Cheap to apply if it ever is. |
| Animation timing (not an EchoPatch item, our own) | Fixed: `fear_anim_exact_time` (impact puffs at high fps). |
| Havok ragdoll / water physics instability, water splash repeats | Not checked at 80 fps. Test: ragdolls on stairs, shooting water. |
| Velocity damping on jump/landing, jumping out of water | Not checked. Feeds "movement feels bouncier" (open item 9). |
| Walk animation reverting to idle (camera stutter) | Not checked. EchoPatch rewrites `GetMaxRecentVelocityMag` with windowed displacement; candidate for item 9. |
| Jump kick, sliding on slopes | Not checked. |
| Slow-motion charge/timer desync | Not checked; slow-mo at 80 fps worth a test. |
| AI reactions | Not checked. |
| Save performance (buffered writes) | The 360 saves through its own path; measure save time first. |
| Nvidia shadow fix, DXGI VRAM, DirectInput HID, input hook | PC-only; our renderer and input are our own. |

## Hook inventory (calls from the translated code into ours)
| Hook (manifest) | Where | Frequency | Cost when idle |
|---|---|---|---|
| `FearAnimFrameMs` x2, `FearAnimTrackerMs` | model update, `trk_Update` | per model / tracker per frame | always active: mutex + map |
| `FearMenuAddItems`, `FearMenuShowItems`, `FearMenuCommand` | main menu build / Press Start / OnCommand | menu only | negligible |
| `FearConsoleVarDefault` (Files) | `VarTrack::Init` | startup | negligible |
| `FearDebugFxCreated*` x18, `FearDebugModelFx*`, `FearDebugDecalSize` | ClientFX | per effect created / per model effect per frame | one call + load |
| `FearDebugFireKey`, `FearDebugWeaponImpact` | weapon fire key, `CWeaponFX::CreateObject` | per shot / impact | one call + load |
| `FearDebugParticle*` x6 | particle emit / render / lock / update | thousands per frame | one call + load |

## For the next reviewer
- Read: `port-nx/src/native/UPSTREAM.md` (our renderer changes), `native_renderer.cpp` around
  the lines above, `native_textures.cpp` `BeginFrame`, `port-nx/src/*.cpp`, the manifests'
  `[[entrypoint.midasm_hook]]` entries, `tools/launch.ps1`.
- Evidence: the renderer's 10-second report ("slow frame", "shader translations and
  pipelines", "skipped by reason", "per frame") in any `logs/run-*.log`.
- Confirm on real runs: the shader pre-warm (P1) and the eviction change. Then pipelines:
  record pipeline keys the same way if skip reason 13 still shows.
