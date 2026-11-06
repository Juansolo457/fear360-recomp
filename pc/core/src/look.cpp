// F.E.A.R. PC - the Xbox 360 game's right-stick look. See fearpc/look.h.
#include "fearpc/look.h"

#include <algorithm>
#include <cmath>

namespace fearpc {

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

void LookModel::Update(const LookTuning& tuning, float x, float y, float dt, bool soft_lock,
                       float* yaw_degrees, float* pitch_degrees) {
  // Square pads report corners past 1: the length is clamped, the direction kept.
  const float length = std::sqrt(x * x + y * y);
  const float deflection = std::min(length, 1.0f);
  if (length <= 0.0f || dt <= 0.0f) {
    if (length <= 0.0f) edge_time_ = 0.0f;
    *yaw_degrees = 0.0f;
    *pitch_degrees = 0.0f;
    return;
  }
  const float dir_x = x / length, dir_y = y / length;

  // The curve gives the rate along the stick's direction.
  const float rate = CurveRate(tuning, deflection);
  float yaw_rate = rate * dir_x;
  const float pitch_rate = -rate * dir_y * tuning.pitch_ratio;

  // Edge boost, on yaw only (as the PC code's edge acceleration).
  if (deflection >= tuning.edge_threshold && tuning.edge_ramp != EdgeRamp::kOff) {
    edge_time_ += dt;
    float boost = 0.0f;  // 0: curve rate .. 1: edge_max_rate
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
    const float speed = std::fabs(yaw_rate);
    if (boost > 0.0f && speed < tuning.edge_max_rate) {
      // Weighted by how sideways the push is, so a stick held straight up doesn't turn.
      const float sideways = std::fabs(dir_x);
      const float gain = (tuning.edge_max_rate - speed) * boost * sideways;
      yaw_rate = (yaw_rate < 0.0f ? -1.0f : 1.0f) * (speed + gain);
    }
  } else {
    edge_time_ = 0.0f;
  }

  float yaw_scale = tuning.sensitivity;
  float pitch_scale = tuning.sensitivity;
  if (soft_lock) {
    yaw_scale *= tuning.soft_lock_yaw;
    pitch_scale *= tuning.soft_lock_pitch;
  }
  *yaw_degrees = yaw_rate * yaw_scale * dt;
  *pitch_degrees = pitch_rate * pitch_scale * dt;
}

}  // namespace fearpc
