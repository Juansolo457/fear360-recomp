// F.E.A.R. 360 - fixes for game code that misbehaves at high frame rates, after EchoPatch's
// "High FPS Fixes" for the PC version (github.com/Wemino/EchoPatch). The 360 build is the PC
// game's code, so it has the same problems once it runs above the console's 30 fps. Each fix is a
// [[entrypoint.midasm_hook]] in fear_manifest.toml (F.E.A.R.) and port-files/fear_manifest.toml
// (F.E.A.R. Files), or a REX_HOOK_RAW in high_fps.cpp, and the tracking issue is #2 in the
// repository. EchoPatch's source (GPL-3.0) was read as a reference only: the fixes here are
// written for the 360 code, with EchoPatch's values (a 60 Hz step, 1/60 s).
//
// AI updates (issue #3, EchoPatch's "Degraded AI reactions"). An AI asks the engine to update it
// again in 0.01 s (CCharacter's SetNextServerUpdate: the engine's set-next-update for its object,
// and the delay kept in the object). Below 100 fps a frame is longer than that, so the AI updates
// every frame; at 120 (8.3 ms frames) it updates every other frame, and its timers (perception,
// reactions, decisions) advance by one frame's time per update, so AI time runs at about half
// speed: enemies "caught off guard … still very accurate but kind of slow". With fear_ai_every_frame
// (default on) the AI's 0.01 becomes a delay shorter than any frame, so it updates every frame, as
// EchoPatch does. A delay of 0 (stop updating) is left alone: on the 360 a third caller passes it.
//                       F.E.A.R.        F.E.A.R. Files   PC GameServer.dll (1.08)
//   SetNextServerUpdate sub_8263D2A0    sub_8266F620     0x10009220
//     the hook (f1)     0x8263D2B8      0x8266F638
//     delay kept at     +0xB34          +0xC8C           +0xA74
//   0.01 callers (bl)   0x826480C0,     0x8267B480,      0x10013382 (update),
//                       0x826481B8      0x8267B578       0x1001347E (initial update)
//   0.0 caller (bl)     0x82647760      0x8267AA40
// EchoPatch also leaves a character at 0.01 once a limb is severed, because a dismembered body
// updated every frame moves erratically. The hook reads CCharacter's m_bSevered for that: the
// death code (CCharacter's, as in the PC SDK) sets it to 1 when DamageTracker::CheckForSever
// returns true, just before PrepareToSever and the sever-frequency counter.
//                       F.E.A.R.        F.E.A.R. Files   PC GameServer.dll (1.08)
//   CheckForSever       sub_826E4418    sub_8273E2D0     0x100EB600
//     called at         0x825B4138      0x825FF604
//   m_bSevered          +0x680          +0x714
// At the hook, r3 is still the character (SetNextServerUpdate's this; mr r31,r3 comes next).
//
// The rest of EchoPatch's list goes under one switch, fear_high_fps_fixes (default on), as
// EchoPatch has them under one. Each place was found from EchoPatch's patch site in the PC 1.08
// GameClient.dll, the public SDK's game code (CMoveMgr, CPolyGridFX, CPlayerMgr,
// CGameServerShell) and the 360 code's own debug strings ("CPlayerMgr::EnterSlowMo" and so on).
//  - Sliding on slopes (#12). CMoveMgr::UpdateNormalFriction slows the player by the frame time
//    times RunVel / SlideToStopTime each frame; with short frames the slope wins. The frame time
//    it reads (f1, right after its GetTimerElapsedS) is made at least 1/60 s.
//  - Jump kick (#11). CMoveMgr::UpdateOnGround clears m_bJumped (+120) whenever the player stands
//    on something, and at a high frame rate that is the frame after the jump, before the melee
//    code can see it. Once a jump starts, m_bJumped is kept for 1/60 s.
//  - Momentum on landing (#9). CMoveMgr::UpdateVelocity scales the horizontal velocity down while
//    the camera's height smoothing is more than 1 unit off (CameraHeightVelocityDampUp/Down), which
//    at a high frame rate eats much of a landing's speed. The block is skipped, as in EchoPatch.
//  - Jumping out of water (#8). CMoveMgr::UpdateInLiquid zeroes the upward velocity and
//    acceleration while the player swims on the surface and moves, which stops the jump out at a
//    high frame rate. Skipped, as in EchoPatch. (EchoPatch also keeps part of the upward speed in
//    the engine's SetVelocity right after; that half is in the engine and isn't ported.)
//  - Water waves (#5). CPolyGridFX::UpdateWaveProp steps the wave simulation by the frame time,
//    which goes unstable with short frames. Its per-frame call (in CPolyGridFX::Update) steps it at
//    60 Hz instead, at most 5 steps a frame, each water surface keeping its own remainder
//    (EchoPatch keeps one for all of them). The start-up frames (HandleStartupFrames) are left alone.
//  - Water splashes (#6). CPolyGridFX::CollisionHandlerCB, the water's physics contact callback,
//    runs once per pair of bodies per frame: each pair now splashes at most every 1/60 s. A skipped
//    contact is still ignored by the physics, as the callback always says.
//  - Slow-mo meter (#13). The client spends its charge by the frame time while the server times
//    the period on its own, so at a high frame rate slow-mo outlasts the meter. As in EchoPatch: a
//    player's slow-mo period is clamped to the charge left (CPlayerMgr::EnterSlowMo), the server
//    ends slow-mo once it has seen the client's charge and that charge reaches 0
//    (CGameServerShell::UpdateSlowMo), and a server message can't lower a recharging meter or raise
//    one in use (CPlayerMgr::HandleMsgSlowMo). Client and server share the process, so the charge
//    is shared directly.
//                                     F.E.A.R.                 F.E.A.R. Files           PC (1.08)
//   UpdateNormalFriction              sub_82391710             sub_82368360             10039A10
//     after GetTimerElapsedS (f1)     0x8239190C               0x82368554
//   UpdateOnGround (m_bJumped +120)   sub_823924A8             sub_823690D8             1003BD30
//   UpdateVelocity damping, skipped   0x82392FF0-0x823930D8    0x82369C18-0x82369D00    1003B2AC
//   UpdateInLiquid zeroing, skipped   0x8238CC48-0x8238CCB4    0x82363868-0x823638D4    10035104
//   UpdateWaveProp (r3, f1)           sub_82407820             sub_823F8268             100C05A0
//     its per-frame call              0x824094AC               0x823FA2AC
//   CollisionHandlerCB (bool& in r8)  sub_824090D8             sub_823F9ED8             100C1B20
//   CPlayerMgr::EnterSlowMo           sub_8237CBB8             sub_82353460
//     (period f1, bPlayer r7)
//   CPlayerMgr::UpdateSlowMo          sub_82382148             sub_82358580             100BAE30
//   CPlayerMgr::HandleMsgSlowMo       sub_82381BF0             sub_82358078             100BAA60
//     state, charge (double)          +632, +864               +640, +872               +640, +880
//   CGameServerShell::UpdateSlowMo    sub_8251E578             sub_82564978
//     m_hSlowMoRecord                 +0xB368                  +0xEEF0
//   CGameServerShell::ExitSlowMo      sub_8251E2E8             sub_82564718
// Not ported: Havok ragdolls (#4, EchoPatch's largest fix, in the engine) and the walk/idle
// animation choice (#10, a long smoothing of the player's speed): docs/ECHOPATCH-PUNCHLIST.md.

#pragma once

#include <rex/ppc/context.h>

// At the start of SetNextServerUpdate: the character in r3, the delay still in f1.
void FearAiNextUpdate(PPCRegister& r3, PPCRegister& f1);

// UpdateNormalFriction, right after GetTimerElapsedS: the frame time in f1.
void FearFrictionFrameTime(PPCRegister& f1);

// UpdateVelocity's camera-height damping, at its start: true skips it.
bool FearSkipLandingDamping();

// UpdateInLiquid's zeroing of the upward speed on the surface, at its start: true skips it.
bool FearSkipSurfaceStop();

// CPolyGridFX::Update, just before it calls UpdateWaveProp for the frame.
void FearWaveFrameCall();

// CPlayerMgr::EnterSlowMo at its entry: the player manager in r3, bPlayer in r7, the period in f1.
void FearSlowMoEnter(PPCRegister& r3, PPCRegister& r7, PPCRegister& f1);

// CPlayerMgr::UpdateSlowMo at its entry: the player manager in r3.
void FearSlowMoUpdate(PPCRegister& r3);
