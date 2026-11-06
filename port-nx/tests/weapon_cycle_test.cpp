// The weapon cycle fix's slot choice (src/weapon_cycle.h), without the game. Each test applies
// the game's own step to the answer, as NextWeapon and PreviousWeapon do, and checks the slot it
// lands on: the answer alone is only half of the fix.

#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "weapon_cycle.h"

namespace {

using fear::weapon_cycle::Answer;
using Slots = std::vector<uint32_t>;

constexpr int kNext = +1, kPrevious = -1;
constexpr int kOtherPath = -1;  // PreviousWeapon's other way, for an answer of 0 or 255

// The slot the game takes for an answer (weapon_fixes.cpp's header has where this was read).
int GameStep(uint8_t answer, int direction) {
  if (direction > 0) return answer == 255 ? 0 : uint8_t(answer + 1);
  return answer == 0 || answer == 255 ? kOtherPath : answer - 1;
}

// The slot a cycle lands on, or nullopt when the fix leaves it to the game.
std::optional<int> Land(const Slots& slots, uint32_t weapon, int direction) {
  const std::optional<uint8_t> answer = Answer(slots, weapon, direction);
  if (!answer) return std::nullopt;
  return GameStep(*answer, direction);
}

// Written the plain way, for the comparison below: step around the slots from the current one
// until a weapon is found. With no current weapon, next starts before slot 0 and previous after
// the last slot.
std::optional<int> Expected(const Slots& slots, int current, int direction) {
  const int count = int(slots.size());
  int slot = current >= 0 ? current : direction > 0 ? -1 : count;
  for (int step = 0; step < count; ++step) {
    slot = (slot + direction + count) % count;
    if (slots[slot]) return slot;
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("three full slots step one at a time and wrap") {
  const Slots slots{10, 20, 30};
  CHECK(Land(slots, 10, kNext) == 1);
  CHECK(Land(slots, 20, kNext) == 2);
  CHECK(Land(slots, 30, kNext) == 0);
  CHECK(Land(slots, 30, kPrevious) == 1);
  CHECK(Land(slots, 20, kPrevious) == 0);
  CHECK(Land(slots, 10, kPrevious) == 2);
}

// The bug the fix is for (#16): as shipped, an empty slot sent the cycle back to slot 0.
TEST_CASE("an empty slot between two weapons is skipped") {
  const Slots slots{10, 0, 30, 0, 50};
  CHECK(Land(slots, 10, kNext) == 2);
  CHECK(Land(slots, 30, kNext) == 4);
  CHECK(Land(slots, 50, kNext) == 0);
  CHECK(Land(slots, 50, kPrevious) == 2);
  CHECK(Land(slots, 30, kPrevious) == 0);
  CHECK(Land(slots, 10, kPrevious) == 4);
}

TEST_CASE("several empty slots in a row, and at both ends") {
  const Slots slots{0, 0, 30, 0, 0, 0, 70, 80, 0, 0};
  CHECK(Land(slots, 30, kNext) == 6);
  CHECK(Land(slots, 70, kNext) == 7);
  CHECK(Land(slots, 80, kNext) == 2);
  CHECK(Land(slots, 30, kPrevious) == 7);
  CHECK(Land(slots, 80, kPrevious) == 6);
  CHECK(Land(slots, 70, kPrevious) == 2);
}

TEST_CASE("a single weapon stays selected") {
  const Slots slots{0, 20, 0};
  CHECK(Land(slots, 20, kNext) == 1);
  CHECK(Land(slots, 20, kPrevious) == 1);
}

TEST_CASE("with no weapon in hand, next takes the first and previous the last") {
  const Slots slots{0, 20, 30, 0, 50};
  CHECK(Land(slots, 0, kNext) == 1);
  CHECK(Land(slots, 0, kPrevious) == 4);
  // A weapon that is in no slot counts the same.
  CHECK(Land(slots, 99, kNext) == 1);
  CHECK(Land(slots, 99, kPrevious) == 4);
}

TEST_CASE("no weapons at all is left to the game") {
  CHECK(Answer(Slots{0, 0, 0}, 0, kNext) == std::nullopt);
  CHECK(Answer(Slots{0, 0, 0}, 10, kPrevious) == std::nullopt);
  CHECK(Answer(Slots{}, 10, kNext) == std::nullopt);
}

// #132: weapon removal asks GetWeaponSlot too, and must get the slot the weapon is in.
TEST_CASE("only the first slot lookup of a cycle takes its direction") {
  fear::weapon_cycle::Direction cycle;
  CHECK(cycle.Take() == 0);  // outside a cycle
  for (int direction : {kNext, kPrevious}) {
    cycle.Set(direction);  // NextWeapon or PreviousWeapon starts
    CHECK(cycle.Take() == direction);  // its own lookup
    CHECK(cycle.Take() == 0);  // a later one in the same cycle
    cycle.Set(0);  // it returns
  }
  cycle.Set(kNext);  // a cycle that made no lookup
  cycle.Set(0);
  CHECK(cycle.Take() == 0);
}

// Every way of filling up to 10 slots (the largest "Weapon slots" setting), from every current
// weapon and from none, in both directions.
TEST_CASE("every arrangement of up to 10 slots lands on the neighbouring weapon") {
  for (int count = 1; count <= 10; ++count) {
    for (uint32_t filled = 1; filled < (1u << count); ++filled) {
      Slots slots(count);
      for (int slot = 0; slot < count; ++slot) {
        if (filled & (1u << slot)) slots[slot] = 100 + slot;  // a different record per slot
      }
      for (int current = -1; current < count; ++current) {
        if (current >= 0 && !slots[current]) continue;
        const uint32_t weapon = current >= 0 ? slots[current] : 0;
        for (int direction : {kNext, kPrevious}) {
          const std::optional<int> landed = Land(slots, weapon, direction);
          if (landed != Expected(slots, current, direction)) {
            INFO("slots " << count << ", filled mask " << filled << ", current " << current
                          << ", direction " << direction);
            REQUIRE(landed == Expected(slots, current, direction));
          }
        }
      }
    }
  }
  SUCCEED();
}
