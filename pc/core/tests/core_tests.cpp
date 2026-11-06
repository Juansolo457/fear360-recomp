// F.E.A.R. PC - unit tests for pc/core (no SDL, no game). Exit code 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "fearpc/layout.h"
#include "fearpc/look.h"
#include "fearpc/pad.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what, int line) {
  if (!ok) {
    std::printf("FAIL line %d: %s\n", line, what);
    ++g_failures;
  }
}
#define CHECK(x) Check((x), #x, __LINE__)

bool Near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

using namespace fearpc;

void TestCurve() {
  LookTuning t;
  CHECK(Near(CurveRate(t, 0.0f), 0.0f));
  CHECK(Near(CurveRate(t, 0.375f), 22.5f));  // halfway to the 0.75 point
  CHECK(Near(CurveRate(t, 0.75f), 45.0f));
  CHECK(Near(CurveRate(t, 0.85f), 67.5f));
  CHECK(Near(CurveRate(t, 0.95f), 90.0f));
  CHECK(Near(CurveRate(t, 1.0f), 90.0f));
  CHECK(Near(CurveRate(t, 2.0f), 90.0f));
}

// Total yaw over `seconds` with the stick held at (x, y), in 1/60 s frames.
float Turn(LookModel& m, const LookTuning& t, float x, float y, float seconds, float* pitch = nullptr) {
  float yaw_total = 0.0f, pitch_total = 0.0f;
  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < static_cast<int>(std::lround(seconds / dt)); ++i) {
    float yaw = 0.0f, p = 0.0f;
    m.Update(t, x, y, dt, false, &yaw, &p);
    yaw_total += yaw;
    pitch_total += p;
  }
  if (pitch) *pitch = pitch_total;
  return yaw_total;
}

void TestLook() {
  LookTuning t;  // edge ramp off by default
  LookModel m;
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 90.0f, 0.1f));    // full right: 90 deg/s
  CHECK(Near(Turn(m, t, -0.75f, 0.0f, 1.0f), -45.0f, 0.1f));
  float pitch = 0.0f;
  CHECK(Near(Turn(m, t, 0.0f, 1.0f, 1.0f, &pitch), 0.0f));   // straight up: no yaw
  CHECK(Near(pitch, -45.0f, 0.1f));                           // looks up at half rate
  CHECK(Near(Turn(m, t, 0.0f, 0.0f, 1.0f), 0.0f));

  // As shipped: 0.3 s at the edge, then 0.4 s of ramp to 360 deg/s.
  t.edge_ramp = EdgeRamp::kConsole;
  m.Reset();
  const float first = Turn(m, t, 1.0f, 0.0f, 0.3f);   // still the curve rate
  CHECK(Near(first, 27.0f, 0.5f));
  const float ramp = Turn(m, t, 1.0f, 0.0f, 0.4f);     // averages (90 + 360) / 2 over 0.4 s
  CHECK(Near(ramp, 90.0f, 3.0f));
  const float after = Turn(m, t, 1.0f, 0.0f, 1.0f);    // full boost
  CHECK(Near(after, 360.0f, 0.5f));
  // Letting go restarts the wait.
  Turn(m, t, 0.0f, 0.0f, 0.1f);
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 0.3f), 27.0f, 0.5f));

  t.edge_ramp = EdgeRamp::kInstant;
  m.Reset();
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 360.0f, 0.5f));
  CHECK(Near(Turn(m, t, 0.9f, 0.0f, 1.0f), CurveRate(t, 0.9f), 0.5f));  // below the edge: no boost
  float up_pitch = 0.0f;
  CHECK(Near(Turn(m, t, 0.0f, 1.0f, 1.0f, &up_pitch), 0.0f));         // straight up at the edge: no spin

  // Soft lock slows turning.
  t.edge_ramp = EdgeRamp::kOff;
  float yaw = 0.0f, p = 0.0f;
  m.Update(t, 1.0f, -1.0f, 1.0f, true, &yaw, &p);
  CHECK(Near(yaw, 90.0f * 0.7071f * 0.3f, 0.1f));
  CHECK(Near(p, 90.0f * 0.7071f * 0.5f * 0.5f, 0.1f));
}

void TestDeadZones() {
  float x = 0.2f, y = 0.0f;
  ApplyStickDeadZone(0.24f, &x, &y);
  CHECK(x == 0.0f && y == 0.0f);
  x = 0.62f; y = 0.0f;
  ApplyStickDeadZone(0.24f, &x, &y);
  CHECK(Near(x, 0.5f));
  x = 0.6f; y = 0.8f;  // magnitude 1
  ApplyStickDeadZone(0.24f, &x, &y);
  CHECK(Near(x, 0.6f) && Near(y, 0.8f));
  CHECK(ApplyTriggerDeadZone(0.12f, 0.1f) == 0.0f);
  CHECK(Near(ApplyTriggerDeadZone(0.12f, 1.0f), 1.0f));
}

void TestLayouts() {
  CHECK(LayoutInput(PadLayout::kNormal, PadAction::kFire) == PadInput::kRightTrigger);
  CHECK(LayoutInput(PadLayout::kNormal, PadAction::kSlowMo) == PadInput::kLeftShoulder);
  CHECK(LayoutInput(PadLayout::kNormal, PadAction::kUseOrReload) == PadInput::kX);
  CHECK(LayoutInput(PadLayout::kLeaner, PadAction::kLeanLeft) == PadInput::kLeftShoulder);
  CHECK(LayoutInput(PadLayout::kLeaner, PadAction::kSlowMo) == PadInput::kY);
  // Every action has exactly one input, and no input carries two actions.
  for (PadLayout layout : {PadLayout::kNormal, PadLayout::kLeaner}) {
    size_t count = 0;
    const PadBinding* b = LayoutBindings(layout, &count);
    CHECK(count == static_cast<size_t>(PadAction::kCount));
    for (size_t i = 0; i < count; ++i)
      for (size_t j = i + 1; j < count; ++j) {
        CHECK(b[i].action != b[j].action);
        CHECK(b[i].input != b[j].input);
      }
  }
}

}  // namespace

int main() {
  TestCurve();
  TestLook();
  TestDeadZones();
  TestLayouts();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("pc/core: all checks passed\n");
  return 0;
}
