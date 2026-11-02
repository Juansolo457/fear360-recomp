// F.E.A.R. 360 - right-stick tuning. See look_tuning.h.

#include "look_tuning.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/string/utf8.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_STRING(fear_look_ramp, "off", "F.E.A.R.",
                      "Right-stick turn boost at the stick's edge: console (as shipped: 0.3 s "
                      "wait, 0.4 s ramp to 360 deg/s), nodelay (ramp starts at once), instant "
                      "(full boost at once), off (no boost; turn rate follows the stick)")
    .allowed({"console", "nodelay", "instant", "off"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_aim_softlock, true, "F.E.A.R.",
                    "Soft-lock aim assist: turning slows near enemies (to 30% yaw, 50% pitch)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(fear_fov, 70.0, "F.E.A.R.",
                      "Vertical field of view in degrees (the console variables FovY and "
                      "FovYWidescreen; 70 as shipped, the PC's too)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(fear_console_vars, "", "F.E.A.R.",
                      "For testing: console variables to start at other values, as Name=value "
                      "pairs separated by commas (e.g. EnableVolumetricLight=0), applied as the "
                      "game creates them")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_console_vars_log, false, "F.E.A.R.",
                    "Log each console variable the game creates, with its starting value")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_body_marks, true, "F.E.A.R.",
                    "Blood and armour marks on characters where bullets hit (the PC's detail at "
                    "Maximum); off = none, as the 360 starts (#47)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_invulnerable, false, "F.E.A.R.",
                    "For testing (the smoke test, #55): the player can't die in single player and "
                    "Instant Action; where a hit would kill, health refills, with no death effect")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(fear_weapon_slots, 3, "F.E.A.R.",
                      "Weapons the player can carry, 1 to 10 (3 as shipped; EchoPatch's weapon "
                      "capacity, #23). F.E.A.R. only for now")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_infinite_flashlight, false, "F.E.A.R.",
                    "The flashlight never runs down, and its battery bar goes (EchoPatch's infinite "
                    "flashlight, #20). F.E.A.R. only for now")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_john_wick, false, "F.E.A.R.",
                    "John Wick mode (#61): infinite ammo, dual pistols from the start, 25% faster "
                    "movement and a slow-mo meter that never runs out. F.E.A.R. only for now")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_auto_shotgun, false, "F.E.A.R.",
                    "The automatic shotgun the 360 disc has but never places (#127; not the PS3 "
                    "version's): every VK-12 becomes one. F.E.A.R. only")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(fear_ps3_game, "", "F.E.A.R.",
                      "The player's own PS3 F.E.A.R. data (PS3_GAME/USRDIR/game), for its "
                      "content; with fear_auto_shotgun, the shotgun's own animations (#127)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(fear_intro_videos, "all", "F.E.A.R.",
                      "Videos at startup: all (as shipped: ESRB, Sierra, Day 1, Monolith), "
                      "monolith (Monolith's logo only), none (#19). F.E.A.R. only for now")
    .allowed({"all", "monolith", "none"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_letterbox, true, "F.E.A.R.",
                    "Black bars in cutscenes, as shipped; off removes them (EchoPatch's disable "
                    "letterboxing, #18)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_full_detail_models, false, "F.E.A.R.",
                    "Characters and objects keep their full-detail models at any distance "
                    "(EchoPatch's no model LOD bias, #21)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_bodies_stay, false, "F.E.A.R.",
                    "Bodies stay instead of fading once there are 10 (4 within 500 units), and so "
                    "do bullet holes, blood splats, debris and shell casings; in F.E.A.R. broken "
                    "glass too (EchoPatch's persistent world state, #22)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fear_all_intervals, false, "F.E.A.R.",
                    "Campaign > Interval lists every interval, not just the ones this profile has "
                    "reached (the 360's own unlock, which it gives a gamertag of F3ARDAY1, or "
                    "TIME4FEAR in F.E.A.R. Files)");
REXCVAR_DEFINE_BOOL(fear_fast_checkpoints, true, "F.E.A.R.",
                    "Checkpoint saves skip the game's video memory clean-up "
                    "(CheckPointOptimizeVideoMemory 0, as EchoPatch's faster saves); off = as "
                    "shipped (#24)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fear::look_tuning {
namespace {

constexpr const char* kDatabase = "Game/database/Loki.gamedb";
// The game opens a file by resolving its folder and then looking the name up in it, so the
// redirect is the folder. The PS3 animation files get a link of their own, though the game reads
// the tree from uberCatalog.cat (PatchCatalog).
constexpr const char* kGuestFolder = "\\Device\\Harddisk0\\Partition1\\Game\\database";
constexpr const char* kGuestAnimFolder = "\\Device\\Harddisk0\\Partition1\\Game\\database_anim";
constexpr const char* kGuestGame = "\\Device\\Harddisk0\\Partition1\\Game";
constexpr const char* kOverrideMount = "\\Device\\FearOverride";
constexpr uint32_t kTypeFloat = 2;
constexpr uint32_t kTypeString = 4;

// The values the settings change, by attribute (and console variable) name; empty as shipped.
std::vector<std::pair<std::string, float>> Changes() {
  const std::string ramp = REXCVAR_GET(fear_look_ramp);
  std::vector<std::pair<std::string, float>> changes;
  if (ramp == "nodelay") {
    changes = {{"GPadAimEdgeDelayTime", 0.0f}};
  } else if (ramp == "instant") {
    changes = {{"GPadAimEdgeDelayTime", 0.0f}, {"GPadAimEdgeAccelTime", 0.001f}};
  } else if (ramp == "off") {
    changes = {{"GPadAimEdgeThreshold", 2.0f}};  // past any stick reading: the boost never starts
  }
  if (!REXCVAR_GET(fear_aim_softlock)) {
    changes.emplace_back("TurningSoftLockYawFactor", 1.0f);
    changes.emplace_back("TurningSoftLockPitchFactor", 1.0f);
  }
  return changes;
}

// The first-person camera's forward offset (CameraAttachedOffsetZ, 0 as shipped) for a field of
// view, so the gun keeps its place on screen at wide angles: EchoPatch's table (GPL-3.0, tuned by
// eye on the PC), interpolated, and carried on in a straight line past either end as EchoPatch
// does (65 degrees gives -2.5).
float GunOffset(double fov) {
  constexpr float kTable[][2] = {{70.0f, 0.0f},   {75.0f, 2.5f},    {78.0f, 4.5f},
                                 {80.0f, 5.75f},  {85.0f, 8.5f},    {93.0f, 12.0f},
                                 {95.0f, 13.0f},  {105.0f, 17.0f},  {115.0f, 19.5f},
                                 {128.0f, 23.0f}};
  constexpr size_t kCount = std::size(kTable);
  size_t i = 0;
  while (i + 2 < kCount && fov > kTable[i + 1][0]) ++i;
  const float t = float((fov - kTable[i][0]) / (kTable[i + 1][0] - kTable[i][0]));
  return kTable[i][1] + t * (kTable[i + 1][1] - kTable[i][1]);
}

// The field of view (fear_fov), applied only as the console variables are created: unlike the
// stick's values, FovY's 70 is in the code, not the database. The camera uses FovYWidescreen on a
// widescreen display in F.E.A.R. Files; F.E.A.R. has only FovY.
std::vector<std::pair<std::string, float>> FovChanges() {
  const double fov = std::clamp(REXCVAR_GET(fear_fov), 40.0, 130.0);
  if (std::abs(fov - 70.0) < 0.01) return {};
  return {{"FovY", float(fov)}, {"FovYWidescreen", float(fov)},
          {"CameraAttachedOffsetZ", GunOffset(fov)}};
}

// The rest of the Recompilation Settings options that are console variables, in both games.
std::vector<std::pair<std::string, float>> OptionChanges() {
  std::vector<std::pair<std::string, float>> changes;
  // fear_bodies_stay (#22): the server fades the farthest bodies beyond BodyCapTotalCount (10 on
  // the 360) and beyond BodyCapRadiusCount (4) within BodyCapRadius of a new one. Both start from
  // Server/Limits as console variables (CCharacter's body cap check). The check clamps a count to
  // 0 or more before using it, so -1 is a cap of 0: tried first, every body faded about 3 s after
  // its death (2026-10-25 recording). A cap no level reaches keeps them all.
  if (REXCVAR_GET(fear_bodies_stay)) {
    changes.emplace_back("BodyCapTotalCount", 1000.0f);
    changes.emplace_back("BodyCapRadiusCount", 1000.0f);
  }
  // fear_letterbox (#18): the interface draws the cutscene bars unless the console variable
  // LetterBoxDisabled is set (CInterfaceMgr's letterbox update; 0 as shipped).
  if (!REXCVAR_GET(fear_letterbox)) {
    changes.emplace_back("LetterBoxDisabled", 1.0f);
  }
  // fear_fast_checkpoints (#24): at a checkpoint save the client calls the interface's
  // DoOptimizeVideoMemory while the save message is built (CClientSaveLoadMgr's save, as in the
  // PC SDK), unless CheckPointOptimizeVideoMemory (1) is 0. EchoPatch's faster saves set it to 0.
  if (REXCVAR_GET(fear_fast_checkpoints)) {
    changes.emplace_back("CheckPointOptimizeVideoMemory", 0.0f);
  }
  return changes;
}

// A change to every value of an attribute, in the records of a category ("*": all of them).
struct DbChange {
  // kScale: a float times `number`. kReplaceWord: a value equal to `from` becomes `word`.
  enum class Kind { kFloat, kWord, kBlankStrings, kScale, kReplaceWord, kStringOffset };
  std::string category, record, attribute;
  Kind kind = Kind::kFloat;
  float number = 0.0f;              // kFloat, kScale
  uint32_t word = 0;                // kWord, kReplaceWord
  std::vector<std::string> blank;   // kBlankStrings: values containing one of these become ""
  uint32_t from = 0;                // kReplaceWord
  // kStringOffset: a string value becomes `word`, an offset into the string table.
  // Instead of category and record: every record whose "category/record" path starts with this.
  std::string prefix;
  int index = -1;  // only this value of the attribute (-1: every value)
  size_t applied = 0;
};

// The Recompilation Settings options that are database values. F.E.A.R. only for now: F.E.A.R.
// Files keeps its database inside uberCatalog.cat.
std::vector<DbChange> DatabaseChanges() {
  std::vector<DbChange> changes;
  // fear_weapon_slots (#23): WeaponDB's GetMaxWeaponCapacity reads it, for the server and the
  // client alike.
  const uint32_t slots = std::clamp(REXCVAR_GET(fear_weapon_slots), 1u, 10u);
  if (slots != 3) {
    DbChange change{.category = "Arsenal/Global", .record = "Global",
                    .attribute = "MaxWeaponCapacity"};
    change.kind = DbChange::Kind::kWord;
    change.word = slots;
    changes.push_back(change);
  }
  // fear_infinite_flashlight (#20): the player's flashlight drains only while MaxCharge is above
  // 0 (CPlayerMgr's update), and the HUD hides the battery bar at 0. The AI's flashlights
  // (CharacterFX) and the wide one already have 0.
  if (REXCVAR_GET(fear_infinite_flashlight)) {
    changes.push_back({.category = "Client/Flashlight", .record = "Player_Narrow",
                       .attribute = "MaxCharge", .kind = DbChange::Kind::kFloat, .number = 0.0f});
  }
  // fear_john_wick (#61), from the weapons' and slow-mo's own records:
  //   - InfiniteAmmo on every weapon (the PC's flag, which the a2x4 already has): the reserve
  //     never runs down; magazines still reload.
  //   - MovementMultiplier, each weapon's pace (0.6 to 1), 25% higher.
  //   - Arsenal/Global's AdditionalDefaultWeapons, which the player always gets (the Pistol, the
  //     SMG and frag grenades: links to the weapon records, numbered as Arsenal/Loadout's lists
  //     show), with Dual Pistols (record 8, the Pistol's own DualWeapon) for the Pistol (18).
  //   - The slow-mo meter's records (Default, SlowMo): a period of 1e9 s and an instant recharge.
  //     At 1e9 a frame's drain is below the float's precision, so the charge never moves. The HUD
  //     bar (Interface/HUD/HUDSlowMo2) is as wide as the period times its first AdditionalFloat
  //     (10 per second: 100 for the 10 s default), so that scale shrinks by the same factor.
  if (REXCVAR_GET(fear_john_wick)) {
    DbChange ammo{.attribute = "InfiniteAmmo", .kind = DbChange::Kind::kWord, .word = 1};
    ammo.prefix = "Arsenal/Weapons/Default/";
    changes.push_back(ammo);
    DbChange pace{.attribute = "MovementMultiplier", .kind = DbChange::Kind::kScale,
                  .number = 1.25f};
    pace.prefix = "Arsenal/Weapons/Default/";
    changes.push_back(pace);
    DbChange dual{.category = "Arsenal/Global", .record = "Global",
                  .attribute = "AdditionalDefaultWeapons", .kind = DbChange::Kind::kReplaceWord,
                  .word = 0x290008};
    dual.from = 0x290012;
    changes.push_back(dual);
    for (const char* record : {"Default", "SlowMo"}) {
      changes.push_back({.category = "SlowMo", .record = record, .attribute = "Period",
                         .kind = DbChange::Kind::kFloat, .number = 1.0e9f});
      changes.push_back({.category = "SlowMo", .record = record, .attribute = "RechargeRate",
                         .kind = DbChange::Kind::kFloat, .number = 1.0e10f});
    }
    DbChange bar{.category = "Interface/HUD", .record = "HUDSlowMo2",
                 .attribute = "AdditionalFloat", .kind = DbChange::Kind::kScale,
                 .number = 10.0f / 1.0e9f};
    bar.index = 0;
    changes.push_back(bar);
  }
  // fear_intro_videos (#19): the splash screen plays SplashMovies in turn and moves on from one
  // that doesn't open (CInterfaceMgr's next-movie loop, the same on the 360), so an empty name is
  // skipped.
  const std::string intro = REXCVAR_GET(fear_intro_videos);
  if (intro != "all") {
    DbChange change{.category = "Client/Shared", .record = "Shared", .attribute = "SplashMovies",
                    .kind = DbChange::Kind::kBlankStrings};
    change.blank = {"ESRB_", "sierralogo", "Day1Logo"};
    if (intro == "none") change.blank.push_back("MonolithLogo");
    changes.push_back(change);
  }
  // fear_bodies_stay (#22), glass: each shatter type's shards fade from FadeOutStart and go at
  // Lifetime (15 and 20 s, 5 and 10 for small panes). EchoPatch makes the lifetime endless.
  if (REXCVAR_GET(fear_bodies_stay)) {
    changes.push_back({.category = "FX/ShatterType", .record = "*", .attribute = "FadeOutStart",
                       .kind = DbChange::Kind::kFloat, .number = 1.0e9f});
    changes.push_back({.category = "FX/ShatterType", .record = "*", .attribute = "Lifetime",
                       .kind = DbChange::Kind::kFloat, .number = 1.0e9f});
  }
  return changes;
}

// fear_console_vars: "Name=value,Name=value", for testing a console variable's effect (#38).
std::vector<std::pair<std::string, float>> TestChanges() {
  std::vector<std::pair<std::string, float>> changes;
  const std::string list = REXCVAR_GET(fear_console_vars);
  size_t pos = 0;
  while (pos < list.size()) {
    size_t end = list.find(',', pos);
    if (end == std::string::npos) end = list.size();
    const std::string item = list.substr(pos, end - pos);
    const size_t equals = item.find('=');
    if (equals != std::string::npos) {
      std::string name = item.substr(0, equals);
      name.erase(0, name.find_first_not_of(' '));
      name.erase(name.find_last_not_of(' ') + 1);
      changes.emplace_back(name, std::strtof(item.c_str() + equals + 1, nullptr));
    }
    pos = end + 1;
  }
  // fear_invulnerable (#55): the PC's test variable HealthTest, still in both games. Where a hit
  // would kill the player, the server refills their health instead and tells the client, which
  // FearHealthTestNotice keeps from playing a death.
  if (REXCVAR_GET(fear_invulnerable)) {
    changes.emplace_back("HealthTest", 1.0f);
  }
  // fear_body_marks (#47): the model decals' detail level starts at 1, whose record
  // (FX/ModelDecal/Settings) allows none, and nothing on the 360 raises it; the PC's detail menu
  // sets 3 at Maximum (10 per character, 200 in all). Played 2026-10-24: the marks show up.
  if (REXCVAR_GET(fear_body_marks)) {
    changes.emplace_back("ModelDecalPerformanceLevel", 3.0f);
  }
  return changes;
}

// The database path of each attribute.
std::string DatabasePath(const std::string& name) {
  return (name.starts_with("TurningSoftLock") ? "Client/SoftLock/SoftLock/"
                                              : "Client/Shared/Shared/") + name;
}

uint32_t ReadWord(const std::vector<uint8_t>& data, size_t pos) {
  uint32_t value;
  std::memcpy(&value, data.data() + pos, 4);
  return std::byteswap(value);
}

// "Category/Record/Attribute" -> file offset of the first value of each float attribute.
// fear_auto_shotgun (#127): Arsenal/Weapons/Default/AutoShotgun is a complete weapon on the 360
// disc (its model, skins, HUD icons, sounds and name; the files are named Watson SAS-12), in
// PlayerWeapons, but no level places it. Played 2026-11-01: it is an automatic shotgun, but not
// the PS3 version's, which still needs the PS3 dump. Levels and the AI's drops name the VK-12 ("Shotgun"), so its record takes the
// Watson's values, attribute by attribute (both records have the same 172). The VK-12's ammo
// stays, so shotgun shells still feed it (the two ammo records differ only in pickup amounts), and
// so does its AI weapon.
struct RecordCopy {
  std::string category, from, to;
  std::set<std::string> keep;
  size_t applied = 0;
};

std::vector<RecordCopy> RecordCopies() {
  std::vector<RecordCopy> copies;
  if (REXCVAR_GET(fear_auto_shotgun)) {
    copies.push_back({.category = "Arsenal/Weapons/Default", .from = "AutoShotgun",
                      .to = "Shotgun", .keep = {"AmmoName", "AIWeaponName"}});
  }
  return copies;
}

// The format is described in tools/gamedb.py.
bool FindFloats(const std::vector<uint8_t>& data, std::map<std::string, size_t>& wanted) {
  if (data.size() < 0x1C || std::memcmp(data.data(), "BDAG", 4)) return false;
  const size_t strings = 0x1C;
  const size_t string_size = ReadWord(data, 8);
  auto text = [&](uint32_t offset) -> std::string {
    const size_t start = strings + offset;
    if (start >= data.size()) return {};
    const auto* begin = reinterpret_cast<const char*>(data.data() + start);
    return std::string(begin, strnlen(begin, data.size() - start));
  };
  size_t pos = strings + string_size;
  auto word = [&]() -> uint32_t {
    if (pos + 4 > data.size()) throw std::out_of_range("gamedb");
    const uint32_t value = ReadWord(data, pos);
    pos += 4;
    return value;
  };
  size_t found = 0;
  try {
    for (uint32_t c = 0, categories = word(); c < categories; ++c) {
      const std::string category = text(word());
      for (uint32_t r = 0, records = word(); r < records; ++r) {
        const std::string record = text(word());
        for (uint32_t a = 0, attributes = word(); a < attributes; ++a) {
          const uint32_t name = word(), type = word();
          word();  // flags
          const uint32_t count = word();
          if (type == kTypeFloat && count) {
            auto it = wanted.find(category + "/" + record + "/" + text(name));
            if (it != wanted.end()) {
              it->second = pos;
              ++found;
            }
          }
          pos += size_t(count) * 4;
        }
      }
    }
  } catch (const std::out_of_range&) {
    return false;
  }
  return found == wanted.size();
}

// Applies each change to every value of its attribute, counting the values changed. A string
// value is an offset into the string table, so a blanked one points at the NUL that ends it.
bool PatchAttributes(std::vector<uint8_t>& data, std::vector<DbChange>& changes) {
  if (data.size() < 0x1C || std::memcmp(data.data(), "BDAG", 4)) return false;
  const size_t strings = 0x1C;
  const size_t string_size = ReadWord(data, 8);
  auto text = [&](uint32_t offset) -> std::string {
    const size_t start = strings + offset;
    if (start >= data.size()) return {};
    const auto* begin = reinterpret_cast<const char*>(data.data() + start);
    return std::string(begin, strnlen(begin, data.size() - start));
  };
  size_t pos = strings + string_size;
  auto word = [&]() -> uint32_t {
    if (pos + 4 > data.size()) throw std::out_of_range("gamedb");
    const uint32_t value = ReadWord(data, pos);
    pos += 4;
    return value;
  };
  auto put = [&](size_t at, uint32_t value) {
    const uint32_t swapped = std::byteswap(value);
    std::memcpy(data.data() + at, &swapped, 4);
  };
  try {
    for (uint32_t c = 0, categories = word(); c < categories; ++c) {
      const std::string category = text(word());
      for (uint32_t r = 0, records = word(); r < records; ++r) {
        const std::string record = text(word());
        for (uint32_t a = 0, attributes = word(); a < attributes; ++a) {
          const uint32_t name = word(), type = word();
          word();  // flags
          const uint32_t count = word();
          if (pos + size_t(count) * 4 > data.size()) return false;
          for (DbChange& change : changes) {
            const bool place = change.prefix.empty()
                                   ? change.category == category &&
                                         (change.record == "*" || change.record == record)
                                   : (category + "/" + record).starts_with(change.prefix);
            if (!place || change.attribute != text(name)) continue;
            for (uint32_t v = 0; v < count; ++v) {
              if (change.index >= 0 && v != uint32_t(change.index)) continue;
              const size_t at = pos + size_t(v) * 4;
              if (change.kind == DbChange::Kind::kFloat && type == kTypeFloat) {
                put(at, std::bit_cast<uint32_t>(change.number));
                ++change.applied;
              } else if (change.kind == DbChange::Kind::kScale && type == kTypeFloat) {
                const float value = std::bit_cast<float>(ReadWord(data, at));
                put(at, std::bit_cast<uint32_t>(value * change.number));
                ++change.applied;
              } else if (change.kind == DbChange::Kind::kReplaceWord && type != kTypeFloat &&
                         type != kTypeString) {
                if (ReadWord(data, at) == change.from) {
                  put(at, change.word);
                  ++change.applied;
                }
              } else if (change.kind == DbChange::Kind::kWord && type != kTypeFloat &&
                         type != kTypeString) {
                put(at, change.word);
                ++change.applied;
              } else if (change.kind == DbChange::Kind::kStringOffset && type == kTypeString) {
                put(at, change.word);
                ++change.applied;
              } else if (change.kind == DbChange::Kind::kBlankStrings && type == kTypeString) {
                const uint32_t offset = ReadWord(data, at);
                const std::string value = text(offset);
                for (const std::string& part : change.blank) {
                  if (value.find(part) != std::string::npos) {
                    put(at, offset + uint32_t(value.size()));
                    ++change.applied;
                    break;
                  }
                }
              }
            }
          }
          pos += size_t(count) * 4;
        }
      }
    }
  } catch (const std::out_of_range&) {
    return false;
  }
  return true;
}

// Adds a string at the end of the string table and returns its offset there. Values hold offsets
// into the table and the records follow it in order, so nothing else moves; the header's table
// size (word 2) grows by the string's 4-byte aligned length.
uint32_t AppendString(std::vector<uint8_t>& data, const std::string& text) {
  const uint32_t string_size = ReadWord(data, 8);
  std::vector<uint8_t> bytes(text.begin(), text.end());
  bytes.resize((bytes.size() + 4) & ~size_t(3), 0);  // NUL-terminated, 4-byte aligned
  data.insert(data.begin() + 0x1C + string_size, bytes.begin(), bytes.end());
  const uint32_t grown = std::byteswap(string_size + uint32_t(bytes.size()));
  std::memcpy(data.data() + 8, &grown, 4);
  return string_size;
}

// fear_auto_shotgun with fear_ps3_game: the PS3 version's animation tree, which hooks the first-
// person watson_* animations (already in the 360's chars\models\player_watson.mdl) up as
// WEAP_AUTOSHOTGUN. The 360's tree lacks them, so its weapon record borrows WEAP_NailGun. The
// same format on both (LTNA, GADB); the PS3's adds the Watson's entries and renames the 360's
// unused WEAP_watson property. Returns the folder holding anim.anmtree and anim.gamedb, if found.
std::filesystem::path Ps3AnimFolder() {
  const std::string root = REXCVAR_GET(fear_ps3_game);
  if (!REXCVAR_GET(fear_auto_shotgun) || root.empty()) return {};
  std::error_code error;
  for (const char* folder : {"database_anim", "Database_Anim"}) {
    const auto path = std::filesystem::path(root) / folder;
    if (std::filesystem::exists(path / "anim.anmtree", error) &&
        std::filesystem::exists(path / "anim.gamedb", error)) {
      return path;
    }
  }
  REXLOG_WARN("F.E.A.R.: Recompilation Settings: no database_anim/anim.anmtree and anim.gamedb "
              "under fear_ps3_game ({}); the automatic shotgun keeps the nail gun's animations",
              root);
  return {};
}

// A catalogue (uberCatalog.cat, the same version 3 layout on both discs): a big-endian header
// (count at +8, name table offset and size at +12 and +16), then 16-byte entries (name offset,
// data offset, size, compressed size; zlib, or stored when the compressed size is the size).
struct Catalog {
  std::vector<uint8_t> entries, names;
  uint32_t count = 0;
};

uint32_t ReadBe(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool ReadCatalog(std::istream& in, Catalog& catalog) {
  uint8_t header[32];
  if (!in.seekg(0) || !in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
  catalog.count = ReadBe(header + 8);
  catalog.entries.resize(size_t(catalog.count) * 16);
  catalog.names.resize(ReadBe(header + 16));
  in.read(reinterpret_cast<char*>(catalog.entries.data()), std::streamsize(catalog.entries.size()));
  in.seekg(ReadBe(header + 12));
  in.read(reinterpret_cast<char*>(catalog.names.data()), std::streamsize(catalog.names.size()));
  return bool(in);
}

// The index of the entry named name (lower case), or -1.
int FindEntry(const Catalog& catalog, std::string_view name) {
  for (uint32_t i = 0; i < catalog.count; ++i) {
    const uint32_t at = ReadBe(&catalog.entries[size_t(i) * 16]);
    if (at >= catalog.names.size()) continue;
    std::string text(reinterpret_cast<const char*>(&catalog.names[at]));
    std::transform(text.begin(), text.end(), text.begin(),
                   [](char c) { return char(std::tolower(uint8_t(c))); });
    if (text == name) return int(i);
  }
  return -1;
}

// The game reads the animation tree from uberCatalog.cat (database_anim\Anim.anmtree, never the
// loose file: a run on 2026-11-01 never opened Game\database_anim). target becomes a copy of the
// catalogue with the PS3 tree appended, stored (compressed size = size, as 9 of the 360's entries
// are), and the Watson's first-person animations and gun model taken from the PS3 catalogue as
// they are (both zlib, the same model format), each entry pointed at its new data. A model the
// PS3 catalogue lacks keeps the 360's. The copy is kept while the sources are unchanged (a stamp
// next to it), since the catalogue is 428 MB. Returns the number of entries replaced, or -1.
int PatchCatalog(const std::filesystem::path& catalog, const std::filesystem::path& ps3_game,
                 const std::filesystem::path& tree, const std::filesystem::path& target) {
  static constexpr const char* kModels[] = {"chars\\models\\player_watson.mdl",
                                            "guns\\models_hh\\watsonsas.mdl"};
  std::error_code error;
  auto ps3_catalog = ps3_game / "ubercatalog.cat";
  if (!std::filesystem::exists(ps3_catalog, error)) ps3_catalog = ps3_game / "uberCatalog.cat";
  const auto describe = [&](const std::filesystem::path& path) {
    return fmt::format("{} {}", std::filesystem::file_size(path, error),
                       std::filesystem::last_write_time(path, error).time_since_epoch().count());
  };
  const std::string stamp =
      fmt::format("2 {} {} {}", describe(catalog), describe(tree), describe(ps3_catalog));
  auto stamp_path = target;
  stamp_path += ".stamp";
  {
    std::ifstream in(stamp_path);
    std::string old, replaced;
    std::getline(in, old);
    std::getline(in, replaced);
    if (old == stamp && std::filesystem::exists(target, error)) return std::atoi(replaced.c_str());
  }
  std::filesystem::remove(stamp_path, error);
  if (!std::filesystem::copy_file(catalog, target,
                                  std::filesystem::copy_options::overwrite_existing, error)) {
    return -1;
  }
  std::fstream file(target, std::ios::binary | std::ios::in | std::ios::out);
  Catalog ours;
  if (!ReadCatalog(file, ours)) return -1;
  // Appends data (size bytes once unpacked) and points entry i at it.
  const auto replace = [&](int i, const std::vector<char>& data, uint32_t size) {
    file.seekp(0, std::ios::end);
    const uint32_t at = uint32_t(file.tellp());
    file.write(data.data(), std::streamsize(data.size()));
    const uint32_t words[3] = {std::byteswap(at), std::byteswap(size),
                               std::byteswap(uint32_t(data.size()))};
    file.seekp(32 + std::streamoff(i) * 16 + 4);
    file.write(reinterpret_cast<const char*>(words), sizeof(words));
  };
  const int tree_entry = FindEntry(ours, "database_anim\\anim.anmtree");
  std::ifstream tree_in(tree, std::ios::binary);
  std::vector<char> tree_data((std::istreambuf_iterator<char>(tree_in)), {});
  if (tree_entry < 0 || tree_data.empty()) return -1;
  replace(tree_entry, tree_data, uint32_t(tree_data.size()));
  int replaced = 1;
  std::ifstream ps3(ps3_catalog, std::ios::binary);
  Catalog theirs;
  if (ps3 && ReadCatalog(ps3, theirs)) {
    for (const char* model : kModels) {
      const int from = FindEntry(theirs, model), to = FindEntry(ours, model);
      if (from < 0 || to < 0) {
        REXLOG_WARN("F.E.A.R.: Recompilation Settings: {} not in both catalogues; the 360's kept",
                    model);
        continue;
      }
      const uint8_t* entry = &theirs.entries[size_t(from) * 16];
      const uint32_t size = ReadBe(entry + 8), stored = ReadBe(entry + 12);
      std::vector<char> data(stored ? stored : size);
      ps3.seekg(ReadBe(entry + 4));
      if (!ps3.read(data.data(), std::streamsize(data.size()))) return -1;
      replace(to, data, size);
      ++replaced;
    }
  } else {
    REXLOG_WARN("F.E.A.R.: Recompilation Settings: no PS3 uberCatalog.cat in {}; the shotgun "
                "keeps the 360's models",
                ps3_game.string());
  }
  file.close();
  if (!file) return -1;
  std::ofstream(stamp_path) << stamp << "\n" << replaced << "\n";
  return replaced;
}

// The game opens D:\Game\uberCatalog.cat as a name looked up in the Game folder's entry (the VFS
// resolves only the parent path, so a link on the file's own path never applies). The folder's
// uberCatalog.cat child becomes an entry for the patched copy instead; children_ is protected, so
// it is reached through a member pointer taken in a derived class.
struct EntryChildren : rex::filesystem::Entry {
  static std::vector<std::unique_ptr<Entry>>& Of(Entry& entry) {
    return entry.*(&EntryChildren::children_);
  }
};

bool SwapCatalogEntry(rex::filesystem::VirtualFileSystem& vfs, const std::filesystem::path& copy) {
  rex::filesystem::Entry* game = vfs.ResolvePath(kGuestGame);
  rex::filesystem::FileInfo info;
  if (!game || !rex::filesystem::GetInfo(copy, &info)) return false;
  for (auto& child : EntryChildren::Of(*game)) {
    if (!rex::string::utf8_equal_case(child->name(), "uberCatalog.cat")) continue;
    info.name = rex::to_path(child->name());
    child.reset(rex::filesystem::HostPathEntry::Create(game->device(), game, copy, info));
    REXLOG_INFO("F.E.A.R.: Recompilation Settings: D:\\Game\\{} now opens {}", info.name.string(),
                copy.string());
    return true;
  }
  return false;
}

// Calls visit(category, record, attribute, type, count, offset of the first value) for every
// attribute.
template <typename Visit>
bool WalkAttributes(const std::vector<uint8_t>& data, Visit&& visit) {
  if (data.size() < 0x1C || std::memcmp(data.data(), "BDAG", 4)) return false;
  const size_t strings = 0x1C;
  const size_t string_size = ReadWord(data, 8);
  auto text = [&](uint32_t offset) -> std::string {
    const size_t start = strings + offset;
    if (start >= data.size()) return {};
    const auto* begin = reinterpret_cast<const char*>(data.data() + start);
    return std::string(begin, strnlen(begin, data.size() - start));
  };
  size_t pos = strings + string_size;
  auto word = [&]() -> uint32_t {
    if (pos + 4 > data.size()) throw std::out_of_range("gamedb");
    const uint32_t value = ReadWord(data, pos);
    pos += 4;
    return value;
  };
  try {
    for (uint32_t c = 0, categories = word(); c < categories; ++c) {
      const std::string category = text(word());
      for (uint32_t r = 0, records = word(); r < records; ++r) {
        const std::string record = text(word());
        for (uint32_t a = 0, attributes = word(); a < attributes; ++a) {
          const uint32_t name = word(), type = word();
          word();  // flags
          const uint32_t count = word();
          if (pos + size_t(count) * 4 > data.size()) return false;
          visit(category, record, text(name), type, count, pos);
          pos += size_t(count) * 4;
        }
      }
    }
  } catch (const std::out_of_range&) {
    return false;
  }
  return true;
}

// Gives each copy's `to` record the values of its `from` record, where an attribute has the same
// name, type and value count in both (string values are offsets into the shared string table, so
// they copy as words), counting the attributes changed.
bool CopyRecords(std::vector<uint8_t>& data, std::vector<RecordCopy>& copies) {
  for (RecordCopy& copy : copies) {
    std::map<std::string, std::pair<uint32_t, std::vector<uint8_t>>> source;
    if (!WalkAttributes(data, [&](const std::string& category, const std::string& record,
                                  const std::string& name, uint32_t type, uint32_t count,
                                  size_t at) {
          if (category != copy.category || record != copy.from) return;
          source[name] = {type, std::vector<uint8_t>(data.begin() + at,
                                                     data.begin() + at + size_t(count) * 4)};
        })) {
      return false;
    }
    std::vector<std::pair<size_t, const std::vector<uint8_t>*>> writes;
    if (!WalkAttributes(data, [&](const std::string& category, const std::string& record,
                                  const std::string& name, uint32_t type, uint32_t count,
                                  size_t at) {
          if (category != copy.category || record != copy.to || copy.keep.contains(name)) return;
          auto it = source.find(name);
          if (it == source.end() || it->second.first != type ||
              it->second.second.size() != size_t(count) * 4) {
            return;
          }
          if (std::memcmp(data.data() + at, it->second.second.data(), it->second.second.size())) {
            writes.emplace_back(at, &it->second.second);
          }
        })) {
      return false;
    }
    for (const auto& [at, values] : writes) {
      std::memcpy(data.data() + at, values->data(), values->size());
    }
    copy.applied = writes.size();
  }
  return true;
}

// The override device, logging each path the game resolves through it once (patched or the
// original's), so a run shows the tuning, and which animation files, were really used.
class OverrideDevice final : public rex::filesystem::HostPathDevice {
 public:
  OverrideDevice(std::string_view mount_path, const std::filesystem::path& overrides,
                 const std::filesystem::path& original)
      : HostPathDevice(mount_path, overrides, true),
        original_(std::string(mount_path) + "Original", original, true) {}
  bool Initialize() override { return HostPathDevice::Initialize() && original_.Initialize(); }
  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    rex::filesystem::Entry* entry = HostPathDevice::ResolvePath(path);
    const bool patched = entry != nullptr;
    if (!patched) entry = original_.ResolvePath(path);
    {
      std::lock_guard lock(mutex_);
      if (logged_.insert(std::string(path)).second) {
        REXLOG_INFO("F.E.A.R.: look tuning: the game opened {} from the {} folder{}", path,
                    patched ? "patched" : "original", entry ? "" : " (not there)");
      }
    }
    return entry;
  }

 private:
  // The player's own Game folder, for everything the override folder doesn't have.
  rex::filesystem::HostPathDevice original_;
  std::mutex mutex_;
  std::set<std::string> logged_;
};

void WriteFloat(std::vector<uint8_t>& data, size_t pos, float value) {
  const uint32_t word = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(data.data() + pos, &word, 4);
}

}  // namespace

void Apply(rex::Runtime& runtime) {
  const std::string ramp = REXCVAR_GET(fear_look_ramp);
  const bool softlock = REXCVAR_GET(fear_aim_softlock);
  const auto changes = Changes();
  auto options = DatabaseChanges();
  auto copies = RecordCopies();
  if (changes.empty() && options.empty() && copies.empty()) return;

  const auto source = runtime.game_data_root() / kDatabase;
  std::error_code exists_error;
  if (!std::filesystem::exists(source, exists_error)) {
    // F.E.A.R. Files: the database is in the catalogue; FearConsoleVarDefault does the stick's.
    REXLOG_INFO("F.E.A.R.: look tuning: stick edge boost '{}', soft lock {} (as the game sets "
                "up its console variables)", ramp, softlock ? "on" : "off");
    if (!options.empty() || !copies.empty()) {
      REXLOG_WARN("F.E.A.R.: Recompilation Settings: weapon slots, infinite flashlight, intro "
                  "videos and lasting glass change the game database, which F.E.A.R. Files keeps "
                  "in its catalogue: not applied here yet");
    }
    return;
  }
  std::ifstream in(source, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  const auto ps3_anims = Ps3AnimFolder();
  if (!ps3_anims.empty() && data.size() >= 0x1C && !std::memcmp(data.data(), "BDAG", 4)) {
    const uint32_t property = AppendString(data, "WEAP_AUTOSHOTGUN");
    for (const char* category :
         {"Arsenal/Weapons/Default", "Arsenal/Weapons/AI", "Arsenal/Weapons/Multiplayer"}) {
      options.push_back({.category = category, .record = "AutoShotgun",
                         .attribute = "AnimationProperty",
                         .kind = DbChange::Kind::kStringOffset, .word = property});
    }
  }
  if (!changes.empty()) {
    std::map<std::string, size_t> at;
    for (const auto& [name, value] : changes) at[DatabasePath(name)] = 0;
    if (data.empty() || !FindFloats(data, at)) {
      REXLOG_ERROR("F.E.A.R.: look tuning: could not read {}; the console tuning stays",
                   source.string());
      return;
    }
    for (const auto& [name, value] : changes) WriteFloat(data, at[DatabasePath(name)], value);
  }
  if (!options.empty()) {
    if (!PatchAttributes(data, options)) {
      REXLOG_ERROR(
          "F.E.A.R.: Recompilation Settings: could not read {}; its options stay as shipped",
          source.string());
      return;
    }
    for (const DbChange& option : options) {
      const std::string where = option.prefix.empty() ? option.category + "/" + option.record + "/"
                                                      : option.prefix + "*/";
      if (option.applied) {
        REXLOG_INFO("F.E.A.R.: Recompilation Settings: {}{}: {} value(s) changed", where,
                    option.attribute, option.applied);
      } else {
        REXLOG_WARN("F.E.A.R.: Recompilation Settings: {}{} not found in the database", where,
                    option.attribute);
      }
    }
  }
  if (!copies.empty()) {
    if (!CopyRecords(data, copies)) {
      REXLOG_ERROR("F.E.A.R.: Recompilation Settings: could not read {}; its weapons stay as "
                   "shipped",
                   source.string());
      return;
    }
    for (const RecordCopy& copy : copies) {
      if (copy.applied) {
        REXLOG_INFO("F.E.A.R.: Recompilation Settings: {}/{} takes {}'s values: {} attribute(s) "
                    "changed",
                    copy.category, copy.to, copy.from, copy.applied);
      } else {
        REXLOG_WARN("F.E.A.R.: Recompilation Settings: {}/{} or {} not found in the database",
                    copy.category, copy.to, copy.from);
      }
    }
  }

  std::error_code error;
  const auto root = runtime.user_data_root() / "overrides" / "Game";
  std::filesystem::create_directories(root / "database", error);
  {
    std::ofstream out(root / "database" / "Loki.gamedb", std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!out) {
      REXLOG_ERROR("F.E.A.R.: look tuning: could not write the patched database under {}",
                   root.string());
      return;
    }
  }
  // The PS3 animation files go next to the database, under the 360's names, with a redirect of
  // their own (kGuestAnimFolder, below).
  std::filesystem::remove_all(root / "database_anim", error);
  bool ps3_copied = !ps3_anims.empty();
  if (ps3_copied) {
    std::filesystem::create_directories(root / "database_anim", error);
    std::error_code copy_error;
    std::filesystem::copy_file(ps3_anims / "anim.anmtree", root / "database_anim" / "Anim.anmtree",
                               copy_error);
    if (!copy_error) {
      std::filesystem::copy_file(ps3_anims / "anim.gamedb", root / "database_anim" / "Anim.gamedb",
                                 copy_error);
    }
    if (copy_error) {
      REXLOG_ERROR("F.E.A.R.: Recompilation Settings: could not copy the PS3 animation files: {}",
                   copy_error.message());
      std::filesystem::remove_all(root / "database_anim", error);
      ps3_copied = false;
    } else {
      REXLOG_INFO("F.E.A.R.: Recompilation Settings: the automatic shotgun's animations from the "
                  "PS3 data in {}",
                  ps3_anims.string());
    }
  }
  auto device =
      std::make_unique<OverrideDevice>(kOverrideMount, root, runtime.game_data_root() / "Game");
  auto* vfs = runtime.file_system();
  if (!vfs || !device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink(kGuestFolder, std::string(kOverrideMount) + "\\database")) {
    REXLOG_ERROR("F.E.A.R.: look tuning: could not redirect the game database");
    return;
  }
  if (ps3_copied &&
      !vfs->RegisterSymbolicLink(kGuestAnimFolder, std::string(kOverrideMount) + "\\database_anim")) {
    REXLOG_ERROR("F.E.A.R.: Recompilation Settings: could not redirect the animation files");
  }
  if (ps3_copied) {
    const int replaced =
        PatchCatalog(runtime.game_data_root() / "Game" / "uberCatalog.cat",
                     ps3_anims.parent_path(), ps3_anims / "anim.anmtree", root / "uberCatalog.cat");
    if (replaced < 0 || !SwapCatalogEntry(*vfs, root / "uberCatalog.cat")) {
      REXLOG_ERROR("F.E.A.R.: Recompilation Settings: could not put the PS3 animation tree into "
                   "a copy of uberCatalog.cat");
    } else {
      REXLOG_INFO("F.E.A.R.: Recompilation Settings: a copy of uberCatalog.cat with {} PS3 "
                  "entries (the animation tree, then the Watson's models)",
                  replaced);
    }
  }
  REXLOG_INFO("F.E.A.R.: look tuning: stick edge boost '{}', soft lock {} (patched copy of {})",
              ramp, softlock ? "on" : "off", source.string());
}

}  // namespace fear::look_tuning

void FearConsoleVarDefault(PPCRegister& r5, PPCRegister& r6, PPCRegister& f1) {
  static const auto changes = [] {  // the cvars are final by now
    auto all = fear::look_tuning::Changes();
    const auto fov = fear::look_tuning::FovChanges();
    all.insert(all.end(), fov.begin(), fov.end());
    const auto options = fear::look_tuning::OptionChanges();
    all.insert(all.end(), options.begin(), options.end());
    const auto test = fear::look_tuning::TestChanges();
    all.insert(all.end(), test.begin(), test.end());
    return all;
  }();
  static const bool log_all = REXCVAR_GET(fear_console_vars_log);
  if ((changes.empty() && !log_all) || !r5.u32) return;
  const auto* name = reinterpret_cast<const char*>(
      rex::system::kernel_memory()->virtual_membase() + r5.u32);
  const std::string_view view(name, strnlen(name, 64));
  static std::mutex mutex;
  static std::set<std::string, std::less<>> logged;
  for (const auto& [wanted, value] : changes) {
    if (view != wanted) continue;
    // A variable created with a text default (r6) starts from the text and ignores the number
    // (VarTrack::Init), so ours needs the text gone: ModelDecalPerformanceLevel starts as "1",
    // which kept the blood marks off while the log said 3.
    const char* text = r6.u32 ? reinterpret_cast<const char*>(
                                    rex::system::kernel_memory()->virtual_membase() + r6.u32)
                              : nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (logged.insert(wanted).second) {
        const std::string game = text ? fmt::format("\"{}\"", std::string_view(text, strnlen(text, 32)))
                                      : fmt::format("{}", f1.f64);
        REXLOG_INFO("F.E.A.R.: console variable {} = {} (the game's {})", wanted, value, game);
      }
    }
    f1.f64 = value;
    r6.u64 = 0;
    return;
  }
  if (log_all) {
    std::lock_guard<std::mutex> lock(mutex);
    if (logged.emplace(view).second) {
      REXLOG_INFO("F.E.A.R.: console variable {} starts at {}", view, f1.f64);
    }
  }
}

bool FearHealthTestNotice() { return REXCVAR_GET(fear_invulnerable); }

namespace {

uint32_t GuestWord(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, rex::system::kernel_memory()->virtual_membase() + address, 4);
  return std::byteswap(value);
}

uint8_t GuestByte(uint32_t address) {
  return rex::system::kernel_memory()->virtual_membase()[address];
}

}  // namespace

void FearModelLodScale(PPCRegister& f1) {
  static const bool full = REXCVAR_GET(fear_full_detail_models);
  // EchoPatch's value: small enough that every model stays on its first LOD, and 0 (which the
  // game may mean as "off") is left alone.
  if (full && f1.f64 > 0.0) f1.f64 = 0.003;
}

namespace {

// How far the current zoom has gone, 0 (the normal view) to 1 (fully zoomed). Only the camera's
// update touches it.
float g_zoom_amount = 0.0f;

}  // namespace

void FearZoomFov(PPCRegister& f0, PPCRegister& f28, PPCRegister& f29) {
  const double range = f29.f64 - f28.f64;
  g_zoom_amount = range > 0.0 ? float(std::clamp((f29.f64 - f0.f64) / range, 0.0, 1.0)) : 0.0f;
}

void FearCameraOffsetZ(PPCRegister& r28, PPCRegister& f30) {
  switch (GuestWord(r28.u32 + 368)) {
    case 0:  // zooming in
    case 2:  // zooming out
      f30.f64 *= 1.0 - g_zoom_amount;
      break;
    case 1:  // zoomed
      f30.f64 = 0.0;
      break;
    default:  // 3: not zoomed
      break;
  }
}

void FearSplashMovieCount(PPCRegister& r23, PPCRegister& r31) {
  static const bool none = REXCVAR_GET(fear_intro_videos) == "none";
  if (!none || !r31.u32) return;
  // The interface's game state: 4 and 9 are the ones that play SplashMovies (the others, the
  // attract loop's AttractMovies).
  const uint32_t state = GuestWord(r31.u32 + 8);
  if (state == 4 || state == 9) r23.u64 = 0;
}

void FearLogStateChange(PPCRegister& r3, PPCRegister& r4) {
  if (!r3.u32) return;
  const uint32_t from = GuestWord(r3.u32 + 8);
  const uint32_t to = r4.u32;
  const uint32_t ready = GuestByte(r3.u32 + 0x5958);
  static std::mutex mutex;
  static std::set<uint64_t> logged;
  std::lock_guard<std::mutex> lock(mutex);
  if (logged.insert(uint64_t(from) << 40 | uint64_t(to) << 8 | ready).second) {
    REXLOG_INFO("F.E.A.R.: interface state {} -> {} asked for (interface ready: {})", from, to,
                ready);
  }
}

namespace {

// Each ClientFX type's create function, the first word of its FX_REF (the type tables at
// 0x830F2850 in F.E.A.R. and 0x831E4488 in F.E.A.R. Files: name, create, delete and the props').
#if defined(FEAR_TITLE_FILES)
constexpr uint32_t kCreateSprite = 0x82B48C28, kCreateLtbModel = 0x82B48C98,
                   kCreateDecal = 0x82B49188;
#else
constexpr uint32_t kCreateSprite = 0x82B8ED20, kCreateLtbModel = 0x82B8ED90,
                   kCreateDecal = 0x82B8F210;
#endif

// Whether a key of this type, in the effect of this name, stays: EchoPatch's choice (#22). Every
// decal (bullet holes, scorch and blood on walls); every model but a rocket's debris; and the
// sprites of stone bullet holes, the debris chunks (electronics, wood, mugs, vases) and the
// Flesh_splat6 blood.
bool KeyStays(uint32_t create, std::string_view effect) {
  if (create == kCreateDecal) return true;
  if (create == kCreateLtbModel) return !(effect.size() >= 10 && effect.substr(2, 8) == "ocket_De");
  if (create != kCreateSprite) return false;
  for (const char* prefix : {"Stone_bu", "Debris_E", "Debris_W", "Debris_M", "Debris_V"}) {
    if (effect.starts_with(prefix)) return true;
  }
  return effect.size() >= 12 && effect.substr(4, 8) == "h_splat6";
}

}  // namespace

void FearCreateFxKey(PPCRegister& r4, PPCRegister& r6) {
  static const bool stay = REXCVAR_GET(fear_bodies_stay);
  if (!stay || !r4.u32 || !r6.u32) return;
  const uint32_t ref = GuestWord(r6.u32);
  const uint32_t props = GuestWord(r6.u32 + 4);
  if (!ref || !props) return;
  const auto* name = reinterpret_cast<const char*>(
      rex::system::kernel_memory()->virtual_membase() + r4.u32);
  if (!KeyStays(GuestWord(ref), std::string_view(name, strnlen(name, 128)))) return;
  // CBaseFXProps' m_tmEnd (+8) and m_tmLifetime (+12), shared by every instance of the key.
  const uint32_t never = std::byteswap(std::bit_cast<uint32_t>(FLT_MAX));
  std::memcpy(rex::system::kernel_memory()->virtual_membase() + props + 8, &never, 4);
  std::memcpy(rex::system::kernel_memory()->virtual_membase() + props + 12, &never, 4);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("F.E.A.R.: bodies and debris stay: effects like {} no longer fade",
                std::string_view(name, strnlen(name, 128)));
  }
}

void FearAllIntervals(PPCRegister& r3) {
  if (!REXCVAR_GET(fear_all_intervals)) return;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) REXLOG_INFO("F.E.A.R.: interval select lists every interval");
  r3.u64 = 1;
}

bool FearShellCasingStays() {
  static const bool stay = REXCVAR_GET(fear_bodies_stay);
  if (stay) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) REXLOG_INFO("F.E.A.R.: bodies and debris stay: shell casings stay");
  }
  return stay;
}
