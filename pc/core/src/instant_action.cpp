// F.E.A.R. PC - the Xbox 360 game's Instant Action score. See fearpc/instant_action.h.
#include "fearpc/instant_action.h"

namespace fearpc {

namespace {

// The 360's weights.
constexpr float kPerMedkit = 10.0f;
constexpr float kHitRate = 100.0f;
constexpr float kPerBooster = 10.0f;
constexpr float kComboSquared = 5.0f;
constexpr float kPerKill = 10.0f;
constexpr float kPerSecondLeft = 0.5f;
constexpr int kSmallestCombo = 2;

}  // namespace

float InstantActionTimeRemaining(const InstantActionRun& run) {
  if (!run.completed) return 0.0f;
  const float left = kInstantActionSeconds - run.seconds;
  return left > 0.0f ? left : 0.0f;
}

float InstantActionHitRate(const InstantActionRun& run) {
  if (run.shots <= 0) return 0.0f;
  const float rate = static_cast<float>(run.hits) / static_cast<float>(run.shots);
  return rate > 1.0f ? 1.0f : (rate < 0.0f ? 0.0f : rate);
}

int InstantActionComboKills(const InstantActionRun& run) {
  int total = 0;
  for (size_t i = 0; i < run.combo_count; ++i) {
    if (run.combos[i] >= kSmallestCombo) total += run.combos[i];
  }
  return total;
}

float InstantActionScore(const InstantActionRun& run) {
  float combos = 0.0f;
  for (size_t i = 0; i < run.combo_count; ++i) {
    if (run.combos[i] >= kSmallestCombo) {
      combos += kComboSquared * static_cast<float>(run.combos[i]) * static_cast<float>(run.combos[i]);
    }
  }
  return kPerMedkit * static_cast<float>(run.medkits) + kHitRate * InstantActionHitRate(run) + run.armor +
         kPerBooster * static_cast<float>(run.boosters) + combos +
         kPerKill * static_cast<float>(run.enemy_kills) + kPerSecondLeft * InstantActionTimeRemaining(run);
}

}  // namespace fearpc
