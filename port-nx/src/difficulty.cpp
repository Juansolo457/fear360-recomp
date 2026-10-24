// F.E.A.R. 360 - how the enemies fight at each difficulty. See difficulty.h.

#include "difficulty.h"

#include <atomic>
#include <cmath>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(fear_ai, "sharp", "F.E.A.R.",
                      "Enemy AI: sharp = Very Hard's tactics (aim, grenades, dodging) at every "
                      "difficulty, how hard they hit still by difficulty; pc = the PC version's "
                      "difficulties; 360 = the console's (an easier Normal)")
    .allowed({"sharp", "pc", "360"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

// Both databases' values: Easy, Normal, Hard, Very Hard. Only Normal differs.
constexpr double kFactor360[] = {0.5, 0.7, 1.25, 1.5};
constexpr double kFactorPc[] = {0.5, 1.0, 1.25, 1.5};
constexpr double kDamage360[] = {0.25, 0.45, 0.85, 1.2};
constexpr double kDamagePc[] = {0.25, 0.65, 0.85, 1.2};
constexpr const char* kNames[] = {"Easy", "Normal", "Hard", "Very Hard"};

// Which difficulty a value read from the 360's database belongs to (-1: none of them).
int Level(double value, const double (&table)[4]) {
  for (int i = 0; i < 4; ++i) {
    if (std::abs(value - table[i]) < 1e-4) return i;
  }
  return -1;
}

void Replace(PPCRegister& f1, double value, const char* what, int level, std::atomic<uint32_t>& logged) {
  if (!(logged.fetch_or(1u << level) & (1u << level))) {
    REXLOG_INFO("F.E.A.R.: {} {} {} (fear_ai {}; the 360's {})", kNames[level], what, value,
                REXCVAR_GET(fear_ai), f1.f64);
  }
  f1.f64 = value;
}

}  // namespace

void FearDifficultyFactor(PPCRegister& f1) {
  static std::atomic<uint32_t> logged{0};
  const int level = Level(f1.f64, kFactor360);
  if (level < 0) return;
  const std::string ai = REXCVAR_GET(fear_ai);
  if (ai == "sharp") {
    Replace(f1, kFactorPc[3], "AI difficulty factor", level, logged);
  } else if (ai == "pc") {
    Replace(f1, kFactorPc[level], "AI difficulty factor", level, logged);
  }
}

void FearDamageAdjustment(PPCRegister& f1) {
  static std::atomic<uint32_t> logged{0};
  const int level = Level(f1.f64, kDamage360);
  if (level < 0) return;
  if (REXCVAR_GET(fear_ai) != "360") {
    Replace(f1, kDamagePc[level], "enemy damage", level, logged);
  }
}
