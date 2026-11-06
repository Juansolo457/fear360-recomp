// F.E.A.R. 360 - the weapon cycle fix's slot choice (weapon_fixes.cpp), on its own so that
// port-nx/tests/weapon_cycle_test.cpp runs it without the game or the SDK.

#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace fear::weapon_cycle {

inline constexpr uint8_t kNoSlot = 255;  // WDB_INVALID_WEAPON_INDEX

// What GetWeaponSlot answers during a cycle, so that the game's own step lands on the next
// (direction > 0) or previous weapon that is there, wrapping past either end:
//   NextWeapon      takes slot 0 for an answer of 255, else the answer + 1;
//   PreviousWeapon  takes the answer - 1 (an answer of 0 or 255 goes its other way).
// slots holds each slot's weapon record, 0 for an empty slot; weapon is the current weapon's.
// No answer when no slot holds a weapon.
inline std::optional<uint8_t> Answer(std::span<const uint32_t> slots, uint32_t weapon,
                                     int direction) {
  const int count = int(slots.size());
  int current = -1, first = -1, last = -1;
  for (int slot = 0; slot < count; ++slot) {
    const uint32_t record = slots[slot];
    if (record && current < 0 && record == weapon) current = slot;
    if (record) {
      if (first < 0) first = slot;
      last = slot;
    }
  }
  if (first < 0) return std::nullopt;
  if (direction > 0) {
    int next = current + 1;  // from before the first slot when nothing is held
    while (next < count && !slots[next]) ++next;
    return uint8_t((next < count ? next : first) - 1);  // -1 is 255: the game then takes slot 0
  }
  int previous = current < 0 ? count - 1 : current - 1;
  while (previous >= 0 && !slots[previous]) --previous;
  return uint8_t((previous >= 0 ? previous : last) + 1);
}

}  // namespace fear::weapon_cycle
