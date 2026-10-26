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
// The other weapon fixes (zoom after a save made in a cutscene, automatic weapons stuck firing,
// the gun model's position after a load: #14, #15, #17) wait for a check that the 360 has them.

#pragma once
