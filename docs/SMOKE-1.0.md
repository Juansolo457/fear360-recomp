# Smoke test for 1.0

The play-through sheet (#55 the setup, #56 F.E.A.R., #57 F.E.A.R. Files). 1.0 is F.E.A.R. on the
Steam Deck, so its rows below are Deck results; *F.E.A.R. Files* and Windows are for the releases
after. The Build column says which build each row was played on: `0.9.0 cc266c5` is the release
candidate, the same game code as 1.0 (1.0 changes only the collection menu and the package). One
row per level; file each failure as its own issue and put its number in Notes.

**How to run it on the Deck (1.0):** `./fear-linux.sh --fear_invulnerable true` in the install's
folder (1.0's `FEAR_GOD=1` is gone since 1.5: releases don't offer it), otherwise the default
settings. The player can't
die: where a hit would have killed, health refills and play carries on, with no death effect (the
PC's test variable `HealthTest`). The log confirms it with `console variable HealthTest = 1` once a
level has loaded.

**On Windows (from 1.5):** `pwsh -NoProfile -File tools/launch.ps1 -Collection -Invulnerable`
(`fear_invulnerable`). Keep each game's `cache` folder: its `prewarm-*.bin` records every shader
seen, for a local shader warm-up list (#53, `tools/make_warmup.py`; the list stays local and is
never committed).

**A level passes when:**
- it loads, with its chapter card or loading screen shown correctly;
- it plays from start to finish and loads the next level (an Instant Action map: completed);
- there's no crash, hang, soft lock (a door, trigger or script that doesn't fire) or falling out
  of the world;
- nothing is missing or broken compared with the 360: geometry, lights, effects, videos, sound and
  subtitles;
- a checkpoint save loads back correctly once per level.

Note frame-rate trouble spots with the log name and place. Result: ✅ pass, ❌ fail, ⚠️ passes with
a problem filed.

## F.E.A.R. (#56)

| Interval | Level | Result | Date | Build | Log | Notes |
|---|---|---|---|---|---|---|
| 01 | `Intro` | ✅ | 2026-10-29 | 0.9.0 | | Deck |
| 02 | `Docks` | ✅ | 2026-10-29 | 0.9.0 | | Deck |
| 03 | `WTF_Entry` | ✅ | 2026-10-29 | 0.9.0 | | Deck |
| 03 | `WTF_Ambush` | ✅ | 2026-10-29 | 0.9.0 | | Deck |
| 03 | `Moody` | ✅ | 2026-10-29 | 0.9.0 | | Deck |
| 03 | `WTF_Exfil` | ⚠️ | 2026-10-29 | 0.9.0 cc266c5 | fear_132-134 | Deck: 0.08-0.16% of frames below 60 fps; the biggest water fight can dip for a second or two (#103) |
| 04 | `ATC_Roof` | ✅ | 2026-10-29 | 0.9.0 cc266c5 | fear_134 | Deck: 0.02% below 60 fps |
| 04 | `Admin` | ✅ | 2026-10-29 | 0.9.0 cc266c5 | fear_134 | Deck: 0.03% below 60 fps |
| 05 | `Bishop_Rescue` | ✅ | 2026-10-29 | 0.9.0 cc266c5 | fear_135 | Deck: 0.36% below 60 fps |
| 05 | `Bishop_Evac` | ✅ | 2026-10-29 | 0.9.0 cc266c5 | fear_135 | Deck: 0.18% below 60 fps (three water pools) |
| 06 | `Mapes_Elevator` | | | | | |
| 06 | `Badge` | | | | | |
| 06 | `Hives` | | | | | |
| 07 | `Alice` | | | | | |
| 07 | `Getting_Out` | | | | | |
| 08 | `Wades` | | | | | |
| 08 | `FactoryA` | | | | | |
| 08 | `FactoryB` | | | | | |
| 09 | `Facility_Upper` | | | | | |
| 09 | `Facility_Bypass` | | | | | |
| 10 | `Vault` | | | | | |
| 11 | `Alma` | | 2026-10-29 | 0.9.0 cc266c5 | fear_135 | Deck, partly played: 0.02% below 60 fps |
| Epilogue | `Aftermath` (and the credits) | | | | | |
| Bonus | `Holiday` (#39) | | | | | |
| Instant Action | `InstantAction1` | | | | | |
| Instant Action | `InstantAction2` | | | | | |
| Instant Action | `InstantAction3` | | | | | |
| Instant Action | `InstantAction4` | | | | | |

## F.E.A.R. Files (#57)

| Campaign | Level | Result | Date | Build | Log | Notes |
|---|---|---|---|---|---|---|
| Extraction Point | `XP_Intro` | | | | | |
| Extraction Point | `Church_01` | | | | | |
| Extraction Point | `Church_02a` | | | | | |
| Extraction Point | `Church_02b` | | | | | |
| Extraction Point | `Warehouse_01a` | | | | | |
| Extraction Point | `Warehouse_01b` | | | | | |
| Extraction Point | `Warehouse_02` | | | | | |
| Extraction Point | `Warehouse_03a` | | | | | |
| Extraction Point | `Warehouse_03b` | | | | | |
| Extraction Point | `Subway_01a` | | | | | |
| Extraction Point | `Subway_01b` | | | | | |
| Extraction Point | `Subway_02a` | | | | | |
| Extraction Point | `Subway_02b` | | | | | |
| Extraction Point | `Subway_03a` | | | | | |
| Extraction Point | `Subway_03b` | | | | | |
| Extraction Point | `Office_01a` | | | | | |
| Extraction Point | `Office_01b` | | | | | |
| Extraction Point | `Hospital_01a` | | | | | |
| Extraction Point | `Hospital_01b` | | | | | |
| Extraction Point | `Hospital_02` | | | | | |
| Extraction Point | `Hospital_03` (and the credits) | | | | | |
| Perseus Mandate | `W01_Introduction` | | | | | |
| Perseus Mandate | `W02_Sewer` | | | | | |
| Perseus Mandate | `W03_Streets` | | | | | |
| Perseus Mandate | `W04_Data_Center` | | | | | |
| Perseus Mandate | `W05_Computer_Core` | | | | | |
| Perseus Mandate | `W06_Landing_Zone` | | | | | |
| Perseus Mandate | `W07_Research_Facility` | | | | | |
| Perseus Mandate | `W08_Plaza` | | | | | |
| Perseus Mandate | `W09_Underground` | | | | | |
| Perseus Mandate | `W10_Subway` | | | | | |
| Perseus Mandate | `W11_Train_Yard` | | | | | |
| Perseus Mandate | `W12_Headquarters` | | | | | |
| Perseus Mandate | `W14_Mine` | | | | | |
| Perseus Mandate | `W15_Clone_Labs` | | | | | |
| Perseus Mandate | `W16_Clone_Production` | | | | | |
| Perseus Mandate | `W17_Escape` (and the credits) | | | | | |
| Instant Action (EP) | `IA_Subway` | | | | | |
| Instant Action (EP) | `IA_Garage` | | | | | |
| Instant Action (EP) | `IA_Catacombs` | | | | | |
| Instant Action (EP) | `IA_Asylum` | | | | | |
| Instant Action (EP) | `IA_Elevator` | | | | | |
| Instant Action (PM) | `IA_06_Clinic` | | | | | |
| Instant Action (PM) | `IA_07_Arena` | | | | | |
| Instant Action (PM) | `IA_08_Sprint` | | | | | |
