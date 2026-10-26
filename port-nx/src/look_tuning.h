// F.E.A.R. 360 - right-stick tuning (the "seam" when you start turning again).
//
// The 360 game turns at a rate proportional to the stick, and once the stick has been at its
// edge (GPadAimEdgeThreshold 0.95) for GPadAimEdgeDelayTime (0.3 s) it ramps the turn rate up
// over GPadAimEdgeAccelTime (0.4 s) to GPadAimEdgeMaxTurnRate (360 deg/s). Every stop restarts
// the wait, which reads as a seam. Near enemies, turning is also slowed to
// TurningSoftLockYawFactor / PitchFactor (0.3 / 0.5) of normal (soft lock aim assist).
// All of it is data in Game/database/Loki.gamedb (Client/Shared, Client/SoftLock).
//
// fear_look_ramp picks the stick response (off by default since play-test 2: "it feels
// significantly better without that ramp up"):
//   console  as shipped
//   nodelay  the boost ramp starts as soon as the stick reaches its edge
//   instant  full boost speed at the edge, no wait and no ramp
//   off      no edge boost: turn rate follows the stick, nothing ramps
// fear_aim_softlock false turns the soft-lock slowdown off.
//
// When either differs from the console tuning (so by default), a patched copy of the player's
// own Loki.gamedb is written to <user data>/overrides/ at startup and the game's path to the
// database is redirected to it (a VFS symbolic link to a second, read-only device). The game
// files are not changed.
//
// F.E.A.R. Files has no loose database (it is inside uberCatalog.cat), so there the values are
// changed on their way out of it instead: the game copies each one into a console variable of the
// same name (VarTrack::Init, which applies the value only when it creates the variable), and
// FearConsoleVarDefault, a [[entrypoint.midasm_hook]] at that function's entry in
// port-files/fear_manifest.toml, swaps in ours by name.
//
// The field of view goes the same way in both games (fear_fov, #1): FovY's default (70, the PC's
// too) is in the code, so FearConsoleVarDefault swaps it as the camera creates the variable, and
// FovYWidescreen with it in F.E.A.R. Files, whose camera uses that one on a widescreen display.
// F.E.A.R. has the hook too (port-nx/fear_manifest.toml, VarTrack::Init at 0x822FC710), where it
// also sees the stick's variables, already patched in the database, and sets the same values.
//
// For testing, the same hook starts any console variable at another value (fear_console_vars,
// "EnableVolumetricLight=0,VolumetricLightNoise=0"), and with fear_console_vars_log logs each one
// the game creates with its starting value. A variable the game sets again later (the detail
// options, a level's settings) keeps that later value.
//
// fear_invulnerable (the smoke test, #55; tools/launch.ps1 -Invulnerable) uses that too: it starts
// the PC's test variable HealthTest at 1, so where a hit would kill the player in single player or
// Instant Action, the server refills their health instead. The server then tells the client, which
// would play a death (a red overlay, a sound and "I was killed") and carry on;
// FearHealthTestNotice skips that.
//
// fear_body_marks (#47, on by default) starts ModelDecalPerformanceLevel at 3, the PC's Maximum, so
// bullet hits leave blood and armour marks on characters; the 360 starts it at 1, which allows none.
//
// More of EchoPatch's options (docs/ECHOPATCH-PUNCHLIST.md). Each is a PC Settings choice, off
// (as shipped) by default:
//   fear_weapon_slots         Arsenal/Global/Global MaxWeaponCapacity (3)                  database
//   fear_infinite_flashlight  Client/Flashlight/Player_Narrow MaxCharge 30 -> 0            database
//   fear_intro_videos         Client/Shared/Shared SplashMovies: skipped ones made ""      database
//   fear_bodies_stay          BodyCapTotalCount 10, BodyCapRadiusCount 4 -> 1000           console
//                             FX/ShatterType/* FadeOutStart, Lifetime -> 1e9 s (glass)     database
//                             effect keys and shell casings that fade (FearCreateFxKey,    hook
//                             FearShellCasingStays, below)
//   fear_letterbox (off)      LetterBoxDisabled 0 -> 1 (no cutscene bars)                  console
//   fear_full_detail_models   ModelLODDistanceScale -> 0.003 (FearModelLodScale, below)    hook
//   fear_fast_checkpoints     CheckPointOptimizeVideoMemory 1 -> 0 (on by default)         console
// The database ones go into the same patched copy as the stick tuning; F.E.A.R. Files keeps its
// database in uberCatalog.cat, so there only the console ones apply for now. The field of view
// (fear_fov) also moves the first-person camera forward at wide angles (CameraAttachedOffsetZ, by
// EchoPatch's table), so the gun keeps its place on screen.

#pragma once

#include <rex/ppc/context.h>

namespace rex {
class Runtime;
}

namespace fear::look_tuning {

// After the runtime has mounted the game data (ReXApp::OnPostSetup).
void Apply(rex::Runtime& runtime);

}  // namespace fear::look_tuning

// VarTrack::Init(this, client, name (r5), text default (r6), number default (f1)), at its entry.
// A variable with a text default ignores the number, so an override also clears r6.
void FearConsoleVarDefault(PPCRegister& r5, PPCRegister& r6, PPCRegister& f1);

// CInterfaceMgr::HandlePlayerEventMessage, at its case for the health test event: true (with
// fear_invulnerable) jumps to the handler's exit, skipping the death effect.
bool FearHealthTestNotice();

// fear_full_detail_models (#21): the camera sets the console variable ModelLODDistanceScale at
// five places, each through the engine's setter (g_pLTClient's console-variable float setter,
// interface slot 0x104 in F.E.A.R., 0x108 in F.E.A.R. Files) with the value in f1. A hook just
// before each call turns any positive value into 0.003, as EchoPatch's NoLODBias does.
//                 F.E.A.R. calls                                F.E.A.R. Files calls
//                 0x823B40E0 0x823B6284 0x823B673C               0x82389918 0x8238BAAC 0x8238BF54
//                 0x823B8440 0x823B9110                          0x8238DB44 0x8238E808
// (CameraFirstPersonLODBias, EchoPatch's other half, is already -1,000,000 on the 360.)
void FearModelLodScale(PPCRegister& f1);

// fear_intro_videos=none (#19), F.E.A.R. only: CInterfaceMgr's next-movie function (sub_82362C88)
// just after it counts SplashMovies (r23), the interface manager in r31: the count becomes 0.
// With the entries only blanked, the game tried to open the empty name (D:\Game\, a folder)
// twice a frame, 2,080 times over 8.7 s on 2026-10-25, and the menu came that much later.
void FearSplashMovieCount(PPCRegister& r23, PPCRegister& r31);

// For #19: CInterfaceMgr::ChangeState (sub_82366558) at its entry, the manager in r3 and the
// state asked for in r4. Logs each distinct change asked for once, with the flag the change
// needs (CInterfaceMgr+0x5958, set when the interface has finished setting up).
void FearLogStateChange(PPCRegister& r3, PPCRegister& r4);

// fear_bodies_stay (#22), the effects: bullet holes, blood splats and debris are ClientFX keys
// that end with their key's time. CClientFXMgr::CreateFXKey (F.E.A.R. sub_8234C538, F.E.A.R. Files
// sub_82320B50) at its entry, with the create struct in r4 (CLIENTFX_CREATESTRUCT, the effect's
// name first) and the key in r6 (its FX_REF, whose first word is the type's create function, and
// its props). For the keys EchoPatch's persistent world state keeps (every decal, every model but
// a rocket's debris, the stone bullet-hole, debris and Flesh_splat6 sprites), the props' end and
// lifetime (+8 and +12, as CBaseFXProps::SetLifetime writes them: F.E.A.R. sub_82BAC498, F.E.A.R.
// Files sub_82B6C5B0) become FLT_MAX before the key is made, as EchoPatch does on the PC's
// CreateFX. (On the 360, CreateFX takes the FX_REF instead of the type's name; its caller still
// has the name.)
void FearCreateFxKey(PPCRegister& r4, PPCRegister& r6);

// fear_all_intervals: the interval select (Campaign > Interval) lists the intervals the profile
// has reached, CXProfile's count of them (vtable slot 63: F.E.A.R. sub_823F87E8, F.E.A.R. Files
// sub_823D9508). That count is every mission in the database when the signed-in gamertag is the
// 360's unlock code (F3ARDAY1; TIME4FEAR in F.E.A.R. Files: sub_82395ED8, sub_823A6608), and
// ReXGlue's gamertag is always "User". Just after that check returns (F.E.A.R. 0x823F880C,
// F.E.A.R. Files 0x823D9528), r3 becomes 1, for the count alone: the main menu and the Instant
// Action summary make the same check and keep the real answer.
void FearAllIntervals(PPCRegister& r3);

// fear_bodies_stay (#22), shell casings: CShellCasingFX::Update (F.E.A.R. sub_824060E8, F.E.A.R.
// Files sub_823F6B58) counts m_fDieTime (+112) down by the frame time and removes the casing at 0.
// True skips that fsubs, as EchoPatch skips the PC's, so casings stay.
bool FearShellCasingStays();
