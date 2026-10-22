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

#pragma once

namespace rex {
class Runtime;
}

namespace fear::look_tuning {

// After the runtime has mounted the game data (ReXApp::OnPostSetup).
void Apply(rex::Runtime& runtime);

}  // namespace fear::look_tuning
