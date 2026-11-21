// F.E.A.R. PC - the Xbox 360 game's Instant Action score.
//
// From the 360 executable (CScreenInstantActionSummary's score, sub_824C7928 in title
// 565507D9; docs/PC-FINDINGS.md, "Instant Action on the 360"). A run is one of four maps with
// a 15 minute clock; it ends when the map's script ends the level (completed) or the player
// dies (failed), and the summary screen adds the score up from the run's statistics. There is
// no difficulty multiplier and no rank.
#pragma once

#include <cstddef>

namespace fearpc {

// The run's clock, seconds.
constexpr float kInstantActionSeconds = 900.0f;

struct InstantActionRun {
  bool completed = false;       // the map ended the level; false if the player died
  float seconds = 0.0f;         // how long the run took
  int shots = 0;
  int hits = 0;
  int enemy_kills = 0;
  int medkits = 0;              // held at the end
  float armor = 0.0f;           // at the end
  int boosters = 0;             // Health and Reflex boosters picked up
  const int* combos = nullptr;  // enemies killed by one shot or one explosion, each time
  size_t combo_count = 0;
};

// Seconds left on the clock: never below 0, and 0 for a failed run.
float InstantActionTimeRemaining(const InstantActionRun& run);
// Hits per shot, 0 to 1; 0 with no shots.
float InstantActionHitRate(const InstantActionRun& run);
// The enemies in combos: the sum of the combos of 2 or more.
int InstantActionComboKills(const InstantActionRun& run);
float InstantActionScore(const InstantActionRun& run);

}  // namespace fearpc
