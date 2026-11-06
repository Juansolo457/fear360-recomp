// F.E.A.R. PC - stick and trigger dead zones. See fearpc/pad.h.
#include <cmath>

#include "fearpc/pad.h"

namespace fearpc {

void ApplyStickDeadZone(float dead_zone, float* x, float* y) {
  const float m = std::sqrt(*x * *x + *y * *y);
  if (m <= dead_zone || m <= 0.0f) {
    *x = *y = 0.0f;
    return;
  }
  const float scaled = m >= 1.0f ? 1.0f : (m - dead_zone) / (1.0f - dead_zone);
  *x *= scaled / m;
  *y *= scaled / m;
}

float ApplyTriggerDeadZone(float dead_zone, float value) {
  if (value <= dead_zone) return 0.0f;
  return value >= 1.0f ? 1.0f : (value - dead_zone) / (1.0f - dead_zone);
}

}  // namespace fearpc
