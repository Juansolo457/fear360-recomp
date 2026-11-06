// F.E.A.R. PC - the Xbox 360 game's right-stick look.
//
// The 360 turns at a rate set by how far the stick is pushed, read off a curve
// (Interface/XenonControls AimInputCurve in its Loki.gamedb): 0 at rest, 45 degrees a second at
// 75% travel, 90 from 95% on. Looking up and down runs at half that (GPadAimAspectRatio 0.5).
// Once the stick has sat at its edge (GPadAimEdgeThreshold 0.95) for GPadAimEdgeDelayTime 0.3 s,
// turning ramps over GPadAimEdgeAccelTime 0.4 s to GPadAimEdgeMaxTurnRate 360 degrees a second.
// With an enemy under the crosshair, turning slows to TurningSoftLockYawFactor 0.3 and
// TurningSoftLockPitchFactor 0.5 (Client/SoftLock), the 360's soft lock.
//
// The recomp found the edge ramp reads as a seam and ships it off by default (play-test 2,
// port-nx/src/look_tuning.h); EdgeRamp offers the same four choices.
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
  float pitch_ratio = 0.5f;
  float edge_threshold = 0.95f;
  float edge_delay = 0.3f;   // seconds
  float edge_accel = 0.4f;   // seconds
  float edge_max_rate = 360.0f;
  float soft_lock_yaw = 0.3f;
  float soft_lock_pitch = 0.5f;
  float sensitivity = 1.0f;  // scales every rate
  EdgeRamp edge_ramp = EdgeRamp::kOff;
};

// The curve's rate for a deflection 0..1, in degrees per second.
float CurveRate(const LookTuning& tuning, float deflection);

class LookModel {
 public:
  // x: -1 left .. 1 right, y: -1 down .. 1 up, dead zone already applied. Writes the turn for a
  // frame of dt seconds, in degrees: yaw positive to the right, pitch positive downward.
  void Update(const LookTuning& tuning, float x, float y, float dt, bool soft_lock,
              float* yaw_degrees, float* pitch_degrees);
  // Forget the time spent at the edge (a level load, a menu).
  void Reset() { edge_time_ = 0.0f; }

 private:
  float edge_time_ = 0.0f;  // seconds the stick has been at its edge
};

}  // namespace fearpc
