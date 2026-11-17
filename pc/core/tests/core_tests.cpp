// F.E.A.R. PC - unit tests for pc/core (no SDL, no game). Exit code 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "fearpc/instant_action.h"
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
  // The 360's numbers: curve x 2.04 (Medium) x 0.01744 / (pi / 180).
  const float k = 2.04f * 0.01744f / (3.14159265f / 180.0f);
  CHECK(Near(t.aim_sensitivity, 2.04f));
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 90.0f * k, 0.2f));  // full right: 183.4 deg/s
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 183.45f, 0.3f));
  // Per-axis dead zone, 25.9% of travel, the rest rescaled.
  CHECK(Near(Turn(m, t, 0.25f, 0.0f, 1.0f), 0.0f));
  const float live = (0.5f - 0.35f / 1.35f) / (1.0f - 0.35f / 1.35f);
  CHECK(Near(Turn(m, t, -0.5f, 0.0f, 1.0f), -CurveRate(t, live) * k, 0.2f));
  float pitch = 0.0f;
  CHECK(Near(Turn(m, t, 0.0f, 1.0f, 1.0f, &pitch), 0.0f));   // straight up: no yaw
  CHECK(Near(pitch, -45.0f * k, 0.2f));                       // looks up at half rate
  CHECK(Near(Turn(m, t, 0.0f, 0.0f, 1.0f), 0.0f));
  // The curve is per axis: a diagonal turns each axis at its own deflection's rate.
  CHECK(Near(Turn(m, t, 0.7071f, 0.7071f, 1.0f), CurveRate(t, AxisDeadZone(t, 0.7071f)) * k, 0.2f));
  // The player's sensitivity scales it.
  t.sensitivity = 1.5f;
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 90.0f * k * 1.5f, 0.3f));
  t.sensitivity = 1.0f;

  // As shipped: 0.3 s at the edge, then 0.4 s of blend toward 360 deg/s (not scaled by the
  // sensitivity).
  const float edge = 360.0f * k / 2.04f;
  t.edge_ramp = EdgeRamp::kConsole;
  m.Reset();
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 0.3f), 90.0f * k * 0.3f, 1.0f));  // still the curve
  Turn(m, t, 1.0f, 0.0f, 0.4f);
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), edge, 0.5f));            // full boost
  Turn(m, t, 0.0f, 0.0f, 0.1f);                                     // letting go restarts the wait
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 0.3f), 90.0f * k * 0.3f, 1.0f));
  // Pushed mostly up at the edge: no boost.
  m.Reset();
  t.edge_ramp = EdgeRamp::kInstant;
  CHECK(Near(Turn(m, t, 0.0f, 1.0f, 1.0f), 0.0f));
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), edge, 0.5f));
  CHECK(Near(Turn(m, t, 0.9f, 0.0f, 1.0f), CurveRate(t, AxisDeadZone(t, 0.9f)) * k, 0.3f));

  // A sensitivity whose curve rate passes the edge's 360 deg/s: the boost doesn't slow it.
  t.sensitivity = 2.0f;
  m.Reset();
  CHECK(Near(Turn(m, t, 1.0f, 0.0f, 1.0f), 90.0f * k * 2.0f, 0.5f));
  t.sensitivity = 1.0f;

  // The old soft-lock slowdown, still there behind its setting.
  t.edge_ramp = EdgeRamp::kOff;
  float yaw = 0.0f, p = 0.0f;
  m.Update(t, 1.0f, 0.0f, 1.0f, true, &yaw, &p);
  CHECK(Near(yaw, 90.0f * k * 0.3f, 0.2f));
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
  // The 360's button characters, and every input has one inside the first set of cells.
  CHECK(ButtonGlyph(PadInput::kA) == 0xE000);
  CHECK(ButtonGlyph(PadInput::kRightShoulder) == 0xE009);
  CHECK(ButtonGlyph(PadInput::kDpadRight) == 0xE00D);
  CHECK(ButtonGlyph(PadInput::kRightTrigger) == 0xE011);
  CHECK(ButtonGlyph(PadInput::kCount) == 0);
  for (int i = 0; i < static_cast<int>(PadInput::kCount); ++i) {
    const char16_t c = ButtonGlyph(static_cast<PadInput>(i));
    CHECK(c >= kGlyphFirst && c < kGlyphFirst + kGlyphsPerSet);
  }
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

// The 360's Instant Action score: each term on its own, then together.
void TestInstantAction() {
  InstantActionRun run;
  CHECK(Near(InstantActionScore(run), 0.0f));
  CHECK(Near(InstantActionHitRate(run), 0.0f));  // no shots

  const int combos[] = {2, 3, 1};  // a "combo" of 1 doesn't count
  run.completed = true;
  run.seconds = 300.0f;
  run.shots = 100;
  run.hits = 50;
  run.enemy_kills = 20;
  run.medkits = 3;
  run.armor = 40.0f;
  run.boosters = 1;
  run.combos = combos;
  run.combo_count = 3;
  CHECK(Near(InstantActionTimeRemaining(run), 600.0f));
  CHECK(Near(InstantActionHitRate(run), 0.5f));
  CHECK(InstantActionComboKills(run) == 5);
  // 30 medkits + 50 hit rate + 40 armor + 10 booster + 5*(4+9) combos + 200 kills + 300 time
  CHECK(Near(InstantActionScore(run), 695.0f));

  // A failed run keeps everything but the time.
  run.completed = false;
  CHECK(Near(InstantActionTimeRemaining(run), 0.0f));
  CHECK(Near(InstantActionScore(run), 395.0f));

  // Past the clock the time is worth nothing, not less.
  run.completed = true;
  run.seconds = 1000.0f;
  CHECK(Near(InstantActionTimeRemaining(run), 0.0f));
  CHECK(Near(InstantActionScore(run), 395.0f));
}

}  // namespace

int main() {
  TestInstantAction();
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
