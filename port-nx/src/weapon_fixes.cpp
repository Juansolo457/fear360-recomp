// F.E.A.R. 360 - weapon fixes, after EchoPatch's "Weapon Fixes" for the PC version
// (github.com/Wemino/EchoPatch; its source, GPL-3.0, read as a reference only). The tracking
// issues are #14 to #17 in the repository.
//
// Weapon cycling with an empty slot between weapons (#16). CInterfaceMgr::NextWeapon takes the
// slot after the current weapon's (CPlayerStats::GetWeaponSlot + 1) and, if that slot is empty,
// goes back to slot 0; PreviousWeapon does the same downwards. With an empty slot between two
// weapons the ones past it can't be reached by cycling, and with "Weapon slots" above the 360's 3
// (fear_weapon_slots) gaps are more likely. As in EchoPatch, while NextWeapon or PreviousWeapon
// runs, GetWeaponSlot answers with the slot next to the next or previous weapon that is there, so
// the game's own +1 or -1 lands on it, and past either end it wraps to the first or last weapon.
// fear_weapon_cycle_fix (default on) switches it.
//                                  F.E.A.R.       F.E.A.R. Files
//   CInterfaceMgr::NextWeapon      sub_82363350   sub_82339660
//   CInterfaceMgr::PreviousWeapon  sub_823634E0   sub_823397F0
//   CPlayerStats::GetWeaponSlot    sub_823E5AC8   sub_823C2F90   (stats in r3, weapon in r4)
//     m_nWeaponCapacity (byte)     +76            +80
//     m_vecWeaponSlots begin, end  +316, +320     +428, +432
// Why each is believed to be that function, from the translated code (F.E.A.R.; F.E.A.R. Files
// in brackets):
//  - NextWeapon: called only by the interface's command handler (sub_82364950), for command 0x4D.
//    Calls GetWeaponSlot once, takes slot 0 for an answer of 255 and the answer + 1 otherwise,
//    then reads that slot (sub_823E5B20 [sub_823C2FE8]) and changes weapon.
//  - PreviousWeapon: the same handler, command 0x4C. Calls GetWeaponSlot once; an answer of 0 or
//    255 goes through sub_823E5B68 [sub_823C3030], anything else has 1 subtracted.
//  - GetWeaponSlot: walks the capacity byte over the slots' words and returns the index of the one
//    equal to r4, or 255. Three callers: the two above and CPlayerStats' weapon removal
//    (sub_823E6730 [sub_823C3C40]), which clears the slot it gets.
// The other weapon fixes (zoom after a save made in a cutscene, automatic weapons stuck firing,
// the gun model's position after a load: #14, #15, #17) wait for a check that the 360 has them.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "fear_title.h"
#include "hook_guard.h"
#include "weapon_cycle.h"

REXCVAR_DEFINE_BOOL(fear_weapon_cycle_fix, true, "F.E.A.R.",
                    "Next and previous weapon skip empty slots and wrap to the first or last "
                    "weapon (EchoPatch's weapon cycling fix, #16). Off = as shipped: an empty slot "
                    "sends the cycle back to slot 0");

namespace {

using GuestFn = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kCapacityOffset = fear::title::kFiles ? 80 : 76;
constexpr uint32_t kSlotsOffset = fear::title::kFiles ? 428 : 316;
// More slots than any "Weapon slots" setting gives (fear_weapon_slots, 10 at most).
constexpr uint32_t kMaxSlots = 32;

// +1 from NextWeapon's start, -1 from PreviousWeapon's, until their GetWeaponSlot call takes it:
// each makes one, before anything else asks. A later caller in the same cycle gets the game's own
// answer: CPlayerStats' weapon removal (sub_823E6730, sub_823C3C40 in F.E.A.R. Files) clears the
// slot GetWeaponSlot names.
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
  t_cycle = 0;
  if (!direction || !REXCVAR_GET(fear_weapon_cycle_fix)) {
    original(ctx, base);
    return;
  }
  const uint32_t stats = ctx.r3.u32;
  const uint32_t weapon = ctx.r4.u32;
  const uint32_t begin = LoadWord(base, stats + kSlotsOffset);
  const uint32_t end = LoadWord(base, stats + kSlotsOffset + 4);
  const uint32_t capacity = base[stats + kCapacityOffset];
  // The slots are a vector of 4-byte records, and the capacity a small count.
  if (!FEAR_HOOK_HOLDS("GetWeaponSlot", capacity <= kMaxSlots && begin % 4 == 0 && end >= begin &&
                                            (end - begin) % 4 == 0 &&
                                            (end - begin) / 4 <= kMaxSlots)) {
    original(ctx, base);
    return;
  }
  const uint32_t stored = begin ? (end - begin) / 4 : 0;
  std::array<uint32_t, kMaxSlots> records;
  const uint32_t count = std::min(capacity, stored);
  for (uint32_t slot = 0; slot < count; ++slot) records[slot] = LoadWord(base, begin + 4 * slot);

  const std::optional<uint8_t> answer =
      fear::weapon_cycle::Answer(std::span(records.data(), count), weapon, direction);
  if (answer) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("F.E.A.R.: weapon cycling skips empty slots (fear_weapon_cycle_fix)");
    }
  }
  // No weapons: the game's own handling.
  ctx.r3.u64 = answer.value_or(fear::weapon_cycle::kNoSlot);
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
