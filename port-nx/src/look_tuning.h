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
