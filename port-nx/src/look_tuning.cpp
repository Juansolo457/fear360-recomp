// F.E.A.R. 360 - right-stick tuning. See look_tuning.h.

#include "look_tuning.h"

#include <algorithm>
#include <atomic>
#include <bit>
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
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>
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

namespace fear::look_tuning {
namespace {

constexpr const char* kDatabase = "Game/database/Loki.gamedb";
// The game opens a file by resolving its folder and then looking the name up in it, so the
// redirect is the folder. That prefix also matches Game\database_anim, which the override
// device hands to the original folder.
constexpr const char* kGuestFolder = "\\Device\\Harddisk0\\Partition1\\Game\\database";
constexpr const char* kOverrideMount = "\\Device\\FearOverride";
constexpr uint32_t kTypeFloat = 2;

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

// The field of view (fear_fov), applied only as the console variables are created: unlike the
// stick's values, FovY's 70 is in the code, not the database. The camera uses FovYWidescreen on a
// widescreen display in F.E.A.R. Files; F.E.A.R. has only FovY.
std::vector<std::pair<std::string, float>> FovChanges() {
  const double fov = std::clamp(REXCVAR_GET(fear_fov), 40.0, 130.0);
  if (std::abs(fov - 70.0) < 0.01) return {};
  return {{"FovY", float(fov)}, {"FovYWidescreen", float(fov)}};
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

// The override device, logging the first time the game opens the patched database, so a run
// shows the tuning was really used.
class OverrideDevice final : public rex::filesystem::HostPathDevice {
 public:
  OverrideDevice(std::string_view mount_path, const std::filesystem::path& overrides,
                 const std::filesystem::path& original)
      : HostPathDevice(mount_path, overrides, true),
        original_(std::string(mount_path) + "Original", original, true) {}
  bool Initialize() override { return HostPathDevice::Initialize() && original_.Initialize(); }
  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    if (rex::filesystem::Entry* entry = HostPathDevice::ResolvePath(path)) {
      if (!logged_.exchange(true)) {
        REXLOG_INFO("F.E.A.R.: look tuning: the game reached the patched database folder ({})",
                    path);
      }
      return entry;
    }
    return original_.ResolvePath(path);
  }

 private:
  // The player's own Game folder, for everything the override folder doesn't have.
  rex::filesystem::HostPathDevice original_;
  std::atomic<bool> logged_{false};
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
  if (changes.empty()) return;

  const auto source = runtime.game_data_root() / kDatabase;
  std::error_code exists_error;
  if (!std::filesystem::exists(source, exists_error)) {
    // F.E.A.R. Files: the database is in the catalogue; FearConsoleVarDefault does it.
    REXLOG_INFO("F.E.A.R.: look tuning: stick edge boost '{}', soft lock {} (as the game sets "
                "up its console variables)", ramp, softlock ? "on" : "off");
    return;
  }
  std::ifstream in(source, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  std::map<std::string, size_t> at;
  for (const auto& [name, value] : changes) at[DatabasePath(name)] = 0;
  if (data.empty() || !FindFloats(data, at)) {
    REXLOG_ERROR("F.E.A.R.: look tuning: could not read {}; the console tuning stays",
                 source.string());
    return;
  }
  for (const auto& [name, value] : changes) WriteFloat(data, at[DatabasePath(name)], value);

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
  auto device =
      std::make_unique<OverrideDevice>(kOverrideMount, root, runtime.game_data_root() / "Game");
  auto* vfs = runtime.file_system();
  if (!vfs || !device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink(kGuestFolder, std::string(kOverrideMount) + "\\database")) {
    REXLOG_ERROR("F.E.A.R.: look tuning: could not redirect the game database");
    return;
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
