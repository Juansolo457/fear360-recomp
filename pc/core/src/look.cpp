// F.E.A.R. PC - the Xbox 360 game's right-stick look. See fearpc/look.h.
#include "fearpc/look.h"

#include <algorithm>
#include <cmath>

namespace fearpc {

namespace {
// The 360 turns degrees into radians with 0.01744; the caller uses pi / 180.
constexpr float kConsoleDegree = 0.01744f / (3.14159265358979f / 180.0f);
}  // namespace

float CurveRate(const LookTuning& tuning, float deflection) {
  const float* x = tuning.curve_deflection;
  const float* y = tuning.curve_rate;
  const int n = LookTuning::kCurvePoints;
  if (deflection <= x[0]) return y[0];
  for (int i = 1; i < n; ++i) {
    if (deflection <= x[i]) {
      const float span = x[i] - x[i - 1];
      const float t = span > 0.0f ? (deflection - x[i - 1]) / span : 1.0f;
      return y[i - 1] + (y[i] - y[i - 1]) * t;
    }
  }
  return y[n - 1];
}

float AxisDeadZone(const LookTuning& tuning, float value) {
  const float magnitude = std::min(std::fabs(value), 1.0f);
  if (magnitude <= tuning.dead_zone) return 0.0f;
  const float live = (magnitude - tuning.dead_zone) / (1.0f - tuning.dead_zone);
  return value < 0.0f ? -live : live;
}

void LookModel::Update(const LookTuning& tuning, float x, float y, float dt, bool soft_lock,
                       float* yaw_degrees, float* pitch_degrees) {
  *yaw_degrees = 0.0f;
  *pitch_degrees = 0.0f;
  x = AxisDeadZone(tuning, x);
  y = AxisDeadZone(tuning, y);
  if (dt <= 0.0f) return;

  // Each axis through the curve on its own, signed.
  const float cx = (x < 0.0f ? -1.0f : 1.0f) * CurveRate(tuning, std::fabs(x));
  const float cy = (y < 0.0f ? -1.0f : 1.0f) * CurveRate(tuning, std::fabs(y));
  const float sens = tuning.aim_sensitivity * tuning.sensitivity;
  float yaw = cx * sens * dt;
  const float pitch = -cy * sens * tuning.pitch_ratio * dt;

  // Edge boost: the yaw blended toward the edge's turn rate.
  const float magnitude = std::sqrt(x * x + y * y);
  if (magnitude > tuning.edge_threshold && std::fabs(y) < tuning.edge_max_vertical &&
      tuning.edge_ramp != EdgeRamp::kOff) {
    edge_time_ += dt;
    float boost = 0.0f;  // 0: the curve's yaw .. 1: edge_max_rate
    switch (tuning.edge_ramp) {
      case EdgeRamp::kInstant:
        boost = 1.0f;
        break;
      case EdgeRamp::kNoDelay:
      case EdgeRamp::kConsole: {
        const float delay = tuning.edge_ramp == EdgeRamp::kConsole ? tuning.edge_delay : 0.0f;
        const float ramp = edge_time_ - delay;
        if (ramp > 0.0f)
          boost = tuning.edge_accel > 0.0f ? std::min(ramp / tuning.edge_accel, 1.0f) : 1.0f;
        break;
      }
      case EdgeRamp::kOff:
        break;
    }
    if (boost > 0.0f) {
      const float speed = std::fabs(yaw);
      // Never below the curve's own rate: a sensitivity near 2 passes edge_max_rate, and the
      // boost must not slow the turn.
      const float edge = std::max(tuning.edge_max_rate * dt, speed);
      yaw = (yaw < 0.0f ? -1.0f : 1.0f) * (speed + (edge - speed) * boost);
    }
  } else {
    edge_time_ = 0.0f;
  }

  float yaw_scale = kConsoleDegree;
  float pitch_scale = kConsoleDegree;
  if (soft_lock) {
    yaw_scale *= tuning.soft_lock_yaw;
    pitch_scale *= tuning.soft_lock_pitch;
  }
  *yaw_degrees = yaw * yaw_scale;
  *pitch_degrees = pitch * pitch_scale;
}

}  // namespace fearpc
