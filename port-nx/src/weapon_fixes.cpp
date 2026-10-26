// F.E.A.R. 360 - weapon fixes. See weapon_fixes.h.

#include "weapon_fixes.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "fear_title.h"

REXCVAR_DEFINE_BOOL(fear_weapon_cycle_fix, true, "F.E.A.R.",
                    "Next and previous weapon skip empty slots and wrap to the first or last "
                    "weapon (EchoPatch's weapon cycling fix, #16). Off = as shipped: an empty slot "
                    "sends the cycle back to slot 0");

namespace {

using GuestFn = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kCapacityOffset = fear::title::kFiles ? 80 : 76;
constexpr uint32_t kSlotsOffset = fear::title::kFiles ? 428 : 316;
constexpr uint8_t kNoSlot = 255;  // WDB_INVALID_WEAPON_INDEX

// +1 while NextWeapon runs, -1 while PreviousWeapon runs.
thread_local int t_cycle = 0;

uint32_t LoadWord(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return std::byteswap(value);
}

void CycleWeapon(PPCContext& ctx, uint8_t* base, GuestFn original, int direction) {
  t_cycle = direction;
  original(ctx, base);
  t_cycle = 0;
}

// GetWeaponSlot, answering for the cycle: the slot the game's +1 (or -1) turns into the next (or
// previous) weapon that is there.
void WeaponSlot(PPCContext& ctx, uint8_t* base, GuestFn original) {
  const int direction = t_cycle;
  if (!direction || !REXCVAR_GET(fear_weapon_cycle_fix)) {
    original(ctx, base);
    return;
  }
  const uint32_t stats = ctx.r3.u32;
  const uint32_t weapon = ctx.r4.u32;
  const uint32_t begin = LoadWord(base, stats + kSlotsOffset);
  const uint32_t end = LoadWord(base, stats + kSlotsOffset + 4);
  const int stored = begin && end > begin ? int((end - begin) / 4) : 0;
  const int count = std::min<int>(base[stats + kCapacityOffset], stored);
  auto held = [&](int slot) { return LoadWord(base, begin + 4 * uint32_t(slot)); };

  int current = -1, first = -1, last = -1;
  for (int slot = 0; slot < count; ++slot) {
    const uint32_t record = held(slot);
    if (record && current < 0 && record == weapon) current = slot;
    if (record) {
      if (first < 0) first = slot;
      last = slot;
    }
  }
  int answer = kNoSlot;  // no weapons: the game's own handling
  if (first >= 0) {
    if (direction > 0) {
      int next = current + 1;  // from before the first slot when nothing is held
      while (next < count && !held(next)) ++next;
      answer = (next < count ? next : first) - 1;  // -1 is 255: the game then takes slot 0
    } else {
      int previous = current < 0 ? count - 1 : current - 1;
      while (previous >= 0 && !held(previous)) --previous;
      answer = (previous >= 0 ? previous : last) + 1;
    }
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("F.E.A.R.: weapon cycling skips empty slots (fear_weapon_cycle_fix)");
    }
  }
  ctx.r3.u64 = uint8_t(answer);
}

}  // namespace

#define FEAR_WEAPON_CYCLE_HOOK(sub, direction) \
  REX_EXTERN(__imp__##sub);                    \
  REX_HOOK_RAW(sub) { CycleWeapon(ctx, base, __imp__##sub, direction); }
#define FEAR_WEAPON_SLOT_HOOK(sub) \
  REX_EXTERN(__imp__##sub);        \
  REX_HOOK_RAW(sub) { WeaponSlot(ctx, base, __imp__##sub); }

#if defined(FEAR_TITLE_FILES)
FEAR_WEAPON_CYCLE_HOOK(sub_82339660, +1)
FEAR_WEAPON_CYCLE_HOOK(sub_823397F0, -1)
FEAR_WEAPON_SLOT_HOOK(sub_823C2F90)
#else
FEAR_WEAPON_CYCLE_HOOK(sub_82363350, +1)
FEAR_WEAPON_CYCLE_HOOK(sub_823634E0, -1)
FEAR_WEAPON_SLOT_HOOK(sub_823E5AC8)
#endif
