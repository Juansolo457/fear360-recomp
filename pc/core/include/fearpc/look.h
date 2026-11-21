// F.E.A.R. PC - the Xbox 360 game's right-stick look.
//
// Read from the 360's code (CPlayerMgr::CalculateAxisOffsets, sub_8237DC88 in default.xex
// e7fccdf6...; docs/PC-FINDINGS.md "The 360's right stick, from its code"):
//
//  - Dead zone per axis, not radial: the pad binding maps n = short / 32768 to 1.35 n, zeroes
//    |v| < 0.35 and takes 0.35 off the rest, so 25.9% of each axis is dead and the rest reads
//    0..1.
//  - The curve (Interface/XenonControls AimInputCurve: 0 at rest, 45 degrees a second at 0.75,
//    90 from 0.95 on) is applied to each axis on its own, keeping its sign.
//  - Times the aim sensitivity, GPadAimSensitivity = the profile's sensitivity x 0.0255. Medium
//    (the default) is 80, so 2.04: full stick turns 183 degrees a second, not 90.
//  - Pitch runs at half (GPadAimAspectRatio 0.5).
//  - Edge boost, yaw only: once the stick (after the dead zone) is past 0.95 with the vertical
//    under 0.5, after GPadAimEdgeDelayTime 0.3 s the yaw rate is blended over
//    GPadAimEdgeAccelTime 0.4 s toward GPadAimEdgeMaxTurnRate 360 degrees a second (not scaled by
//    the sensitivity).
//  - The 360 converts degrees with 0.01744 rather than pi / 180; kept, for the same numbers.
//  - No smoothing and no frame-time clamp.
//
// The 360's soft lock is not a slowdown: TurningSoftLock*Factor only switch on a target search,
// whose effect is an added pull toward the target (not ported yet). soft_lock_yaw / _pitch keep
// the older slowdown, which is off by default.
//
// The recomp ships the edge ramp off (play-test 2 there, port-nx/src/look_tuning.h); EdgeRamp
// offers the same four choices.
#pragma once

namespace fearpc {

enum class EdgeRamp : unsigned char {
  kConsole,  // as shipped: wait at the edge, then ramp
  kNoDelay,  // ramp as soon as the stick reaches the edge
  kInstant,  // full boost speed at the edge
  kOff,      // turn rate follows the stick only
};

struct LookTuning {
  static constexpr int kCurvePoints = 4;
  float curve_deflection[kCurvePoints] = {0.0f, 0.75f, 0.95f, 1.0f};
  float curve_rate[kCurvePoints] = {0.0f, 45.0f, 90.0f, 90.0f};  // degrees per second
  float dead_zone = 0.35f / 1.35f;  // per axis, of full travel
  float aim_sensitivity = 80.0f * 0.0255f;  // the 360's Medium
  float pitch_ratio = 0.5f;
  float edge_threshold = 0.95f;
  float edge_max_vertical = 0.5f;
  float edge_delay = 0.3f;   // seconds
  float edge_accel = 0.4f;   // seconds
  float edge_max_rate = 360.0f;
  float soft_lock_yaw = 0.3f;
  float soft_lock_pitch = 0.5f;
  float sensitivity = 1.0f;  // the player's setting: 1 is the 360's Medium
  EdgeRamp edge_ramp = EdgeRamp::kOff;
};

// The curve's rate for a deflection 0..1, in degrees per second.
float CurveRate(const LookTuning& tuning, float deflection);
// The 360's per-axis dead zone: -1..1 raw to -1..1 with the dead part removed.
float AxisDeadZone(const LookTuning& tuning, float value);

class LookModel {
 public:
  // x: -1 left .. 1 right, y: -1 down .. 1 up, raw (no dead zone applied). Writes the turn for a
  // frame of dt seconds, in degrees: yaw positive to the right, pitch positive downward.
  void Update(const LookTuning& tuning, float x, float y, float dt, bool soft_lock,
              float* yaw_degrees, float* pitch_degrees);
  // Forget the time spent at the edge (a level load, a menu).
  void Reset() { edge_time_ = 0.0f; }

 private:
  float edge_time_ = 0.0f;  // seconds the stick has been at its edge
};

}  // namespace fearpc
