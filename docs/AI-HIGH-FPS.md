# The AI at high frame rates (#3)

Written 2026-10-23. The user still finds the AI "a little slow" at 120 fps on PC and around 90
on the Steam Deck, after the fix for #3. This note looks for a second cause. Nothing was built or
run for it.

## What's already fixed
Each AI asked the engine to update it again in 0.01 s. Above 100 fps a frame is shorter than
that, so the AI updated every other frame, and its timers advanced by one frame's time per update:
AI time ran at about half speed. `fear_ai_every_frame` (on by default, `port-nx/src/high_fps.*`,
commit `a894ca7`) replaces that 0.01 with a delay shorter than a frame, at
`SetNextServerUpdate` (F.E.A.R. `sub_8263D2A0`, F.E.A.R. Files `sub_8266F620`). It leaves the
game's 0.0 ("stop updating") alone. Every recent log shows it running ("AI updates every frame
... the game asked for 0.009999999776482582 s"): PC stage A and B, Deck `fear_021`, `fear_027` and
`fear_028`. EchoPatch's high-fps list has exactly one AI item, "Degraded AI reactions", and it's
this one.

## What the game code does with time (the public SDK's game code)
Read from Monolith's public F.E.A.R. SDK game code (`Dev/Source/FEAR/ObjectDLL`). That's the PC
revision, so the 360 build may differ in places. Read for reference only; nothing is copied here.
- **Updates.** `CAI::EngineMessageFn`'s `MID_UPDATE` and `MID_INITIALUPDATE` call
  `SetNextServerUpdate(c_fUpdateDelta)`, with `c_fUpdateDelta = 0.01f` (`AIUtils.cpp`). No other
  AI code sets a delay. Other objects use `UPDATE_NEXT_FRAME` (`ServerUtilities.h`: 1e-7, every
  frame) or delays of 0.1 s and longer. `Door.cpp` and `VolumeBrush.cpp` define an
  `UPDATE_DELTA` of 0.01, but doors time their movement by game time (`GetTime`), and volume
  brushes update every frame with the frame time. Neither depends on the frame rate.
- **Sensors.** Each sensor has an update rate from the game database (`AIDB`
  `fSensorUpdateRate`) and a next-update time in game time (`AISensorAbstract.cpp`,
  `AISensorMgr::UpdateSensors`). Spreading the cost: `CAIMgr::UpdateAISensors` lets at most
  **one expensive ("distributed") sensor update happen per call across all AI**, round robin
  (`AIMgr.cpp`). `CGameServerShell::Update` calls `CAIMgr::Update`, and through it
  `UpdateAISensors`, once per server frame. So the number of expensive sensor updates per second
  equals the frame rate. A higher frame rate makes perception faster, not slower. At the 360's
  30 fps it was the tightest.
- **Timers.** The AI's perception and decision timers use game time (`GetTime`, for example
  `AISensorAbstractStimulatable`'s `m_fLastSensorUpdate`), not update or frame counts. The only
  `GetFrameTime` in the AI code is a velocity scale in `AIWeaponMgr.cpp`.

So once the update delay is fixed, nothing in the AI's scheduling slows it down at high frame
rates, and the sensors speed up. The 360 build shares this code: its `SetNextServerUpdate` and its
two 0.01 callers match the PC's (#3).

## What's left
1. **Most likely: the 360's own AI tuning.** The 360 version reads its AI values (sensor update
   rates, reaction and aiming attributes) from its own game database. A console build may well
   tune them for controller play, slower than the PC version the user remembers. That isn't a
   bug in the port.
2. **The test that tells, with no build:** the same fight at the console's 30 fps,
   `tools/launch.ps1 -Collection -FrameRate 30 -Timer` (PC) or `fear_frame_rate_limit = 60` and
   then 30 on the Deck. If the AI feels just as slow at 30, it's the original tuning. If it feels
   quicker at 30, something still depends on frame rate, and the next step is a diagnostic build:
   count AI updates and expensive sensor updates per second, at 30 and at 120.
3. **Still open on #3, unrelated to speed:** EchoPatch sets a character back to the 0.01 delay
   once a limb is severed (`CheckForSever`, PC `GameServer.dll` `0x100EB600`), or its ragdoll
   twitches. That isn't ported yet. Watch dismembered bodies (#4).
