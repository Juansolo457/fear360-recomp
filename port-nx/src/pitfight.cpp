// F.E.A.R. 360 - PitFight in a F.E.A.R. map. See pitfight.h.

#include "pitfight.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>

REXCVAR_DEFINE_STRING(fear_pitfight, "", "F.E.A.R.",
                      "PitFight (#67): set Condemned's AI fight pit up in this map (e.g. "
                      "InstantAction1); empty = off")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(fear_pitfight_teams, "Soldier_Fatigue,HeavyArmor", "F.E.A.R.",
                      "PitFight: the two fighters' models (Character/Models records); each clones "
                      "the map's template AI of that model, or else a soldier's")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(fear_pitfight_score, 5, "F.E.A.R.",
                      "PitFight: the score that ends a match (0: no end)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fear::pitfight {
namespace {

// The game opens D:\Game\Worlds\<folder>\<map>.*, D: being this device path.
constexpr const char* kGuestWorlds = "\\Device\\Harddisk0\\Partition1\\Game\\Worlds\\";
constexpr const char* kMount = "\\Device\\FearPitFight";
constexpr const char* kArena = "PitFight00";
// Team 0 hates team 1 and the player; team 1 hates team 0 and likes the player
// (Character/Alignments).
constexpr const char* kAlignments[2] = {"BadCharacter", "GoodCharacter"};
// Starting points at least this far apart (game units), so the two see each other coming.
constexpr float kMinApart = 600.0f;

// Property value types in a .wldsrvr. Strings sit in the object's block (NUL-terminated, padded
// with NULs to a multiple of 4), as do vectors and colours (3 floats) and rotations (4); reals,
// bools and integers are the table's value word itself.
constexpr uint32_t kString = 0, kVector = 1, kColor = 2, kReal = 3, kBool = 4, kLong = 5,
                   kRotation = 6, kCommand = 7, kStringId = 8;

bool EqualNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
         });
}

bool StartsNoCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && EqualNoCase(text.substr(0, prefix.size()), prefix);
}

struct Prop {
  std::string name;
  uint32_t type = kString;
  std::string text;            // kString, kCommand, kStringId
  std::vector<uint8_t> bytes;  // kVector, kColor, kRotation
  uint32_t word = 0;           // kReal, kBool, kLong
};

struct Object {
  std::string cls;
  std::vector<Prop> props;

  const Prop* Find(std::string_view name) const {
    for (const Prop& prop : props) {
      if (EqualNoCase(prop.name, name)) return &prop;
    }
    return nullptr;
  }
  Prop* Find(std::string_view name) {
    return const_cast<Prop*>(static_cast<const Object*>(this)->Find(name));
  }
  std::string Text(std::string_view name) const {
    const Prop* prop = Find(name);
    return prop ? prop->text : std::string();
  }
  uint32_t Word(std::string_view name) const {
    const Prop* prop = Find(name);
    return prop ? prop->word : 0;
  }
  // These change a property the object already has; a clone keeps its class's property list.
  void SetText(std::string_view name, std::string value) {
    if (Prop* prop = Find(name)) prop->text = std::move(value);
  }
  void SetWord(std::string_view name, uint32_t value) {
    if (Prop* prop = Find(name)) prop->word = value;
  }
  void SetBytes(std::string_view name, std::vector<uint8_t> value) {
    if (Prop* prop = Find(name)) prop->bytes = std::move(value);
  }
};

uint32_t U32(const std::vector<uint8_t>& data, size_t at) {
  return uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 |
         data[at + 3];
}

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(value >> shift));
}

// A NUL-terminated string at block offset `offset`, within the block.
std::optional<std::string> BlockString(const std::vector<uint8_t>& data, size_t block,
                                       size_t size, uint32_t offset) {
  if (offset >= size) return std::nullopt;
  const auto first = data.begin() + std::ptrdiff_t(block + offset);
  const auto last = data.begin() + std::ptrdiff_t(block + size);
  const auto nul = std::find(first, last, uint8_t(0));
  if (nul == last) return std::nullopt;
  return std::string(first, nul);
}

// Every object of a .wldsrvr ("SDLW", a version, the object count, then the objects; nothing
// follows them). Each: u16 class name length, the class name, u32 property count, u32 block size,
// the block, then per property {u32 name offset, u32 type, u32 value or value offset}.
std::optional<std::vector<Object>> ParseObjects(const std::vector<uint8_t>& data) {
  if (data.size() < 12 || std::memcmp(data.data(), "SDLW", 4) != 0) return std::nullopt;
  const uint32_t count = U32(data, 8);
  auto has = [&](size_t at, size_t n) { return at <= data.size() && n <= data.size() - at; };
  std::vector<Object> objects;
  objects.reserve(count);
  size_t p = 12;
  for (uint32_t i = 0; i < count; ++i) {
    if (!has(p, 2)) return std::nullopt;
    const size_t name_size = size_t(data[p]) << 8 | data[p + 1];
    if (!has(p + 2, name_size + 8)) return std::nullopt;
    Object object;
    object.cls.assign(data.begin() + std::ptrdiff_t(p + 2),
                      data.begin() + std::ptrdiff_t(p + 2 + name_size));
    p += 2 + name_size;
    const uint32_t count_props = U32(data, p);
    const uint32_t block_size = U32(data, p + 4);
    const size_t block = p + 8;
    if (!has(block, size_t(block_size) + size_t(count_props) * 12)) return std::nullopt;
    const size_t table = block + block_size;
    for (uint32_t k = 0; k < count_props; ++k) {
      const size_t at = table + 12 * size_t(k);
      Prop prop;
      const auto name = BlockString(data, block, block_size, U32(data, at));
      if (!name) return std::nullopt;
      prop.name = *name;
      prop.type = U32(data, at + 4);
      const uint32_t value = U32(data, at + 8);
      switch (prop.type) {
        case kString:
        case kCommand:
        case kStringId: {
          const auto text = BlockString(data, block, block_size, value);
          if (!text) return std::nullopt;
          prop.text = *text;
          break;
        }
        case kVector:
        case kColor:
        case kRotation: {
          const size_t size = prop.type == kRotation ? 16 : 12;
          if (value > block_size || size > block_size - value) return std::nullopt;
          prop.bytes.assign(data.begin() + std::ptrdiff_t(block + value),
                            data.begin() + std::ptrdiff_t(block + value + size));
          break;
        }
        case kReal:
        case kBool:
        case kLong:
          prop.word = value;
          break;
        default:
          return std::nullopt;
      }
      object.props.push_back(std::move(prop));
    }
    p = table + size_t(count_props) * 12;
    objects.push_back(std::move(object));
  }
  if (p != data.size()) return std::nullopt;
  return objects;
}

void PutBlockString(std::vector<uint8_t>& block, const std::string& text) {
  block.insert(block.end(), text.begin(), text.end());
  block.push_back(0);
  while (block.size() % 4) block.push_back(0);
}

// One object in the layout the game's own files use: each property's name, then its value, in
// table order (every object of the shipped maps rebuilds byte for byte this way).
void AppendObject(std::vector<uint8_t>& out, const Object& object) {
  std::vector<uint8_t> block;
  std::vector<uint8_t> table;
  for (const Prop& prop : object.props) {
    const uint32_t name_offset = uint32_t(block.size());
    PutBlockString(block, prop.name);
    uint32_t value = prop.word;
    if (prop.type == kString || prop.type == kCommand || prop.type == kStringId) {
      value = uint32_t(block.size());
      PutBlockString(block, prop.text);
    } else if (prop.type == kVector || prop.type == kColor || prop.type == kRotation) {
      value = uint32_t(block.size());
      block.insert(block.end(), prop.bytes.begin(), prop.bytes.end());
    }
    PutU32(table, name_offset);
    PutU32(table, prop.type);
    PutU32(table, value);
  }
  out.push_back(uint8_t(object.cls.size() >> 8));
  out.push_back(uint8_t(object.cls.size()));
  out.insert(out.end(), object.cls.begin(), object.cls.end());
  PutU32(out, uint32_t(object.props.size()));
  PutU32(out, uint32_t(block.size()));
  out.insert(out.end(), block.begin(), block.end());
  out.insert(out.end(), table.begin(), table.end());
}

struct Vec {
  float x = 0, y = 0, z = 0;
};

Vec ToVec(const std::vector<uint8_t>& bytes) {
  Vec v;
  if (bytes.size() >= 12) {
    v.x = std::bit_cast<float>(U32(bytes, 0));
    v.y = std::bit_cast<float>(U32(bytes, 4));
    v.z = std::bit_cast<float>(U32(bytes, 8));
  }
  return v;
}

float Distance(const Vec& a, const Vec& b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                   (a.z - b.z) * (a.z - b.z));
}

Vec PosOf(const Object& object) {
  const Prop* pos = object.Find("Pos");
  return pos ? ToVec(pos->bytes) : Vec{};
}

Prop Text(std::string name, uint32_t type, std::string text) {
  Prop prop;
  prop.name = std::move(name);
  prop.type = type;
  prop.text = std::move(text);
  return prop;
}

Prop Bytes(std::string name, uint32_t type, std::vector<uint8_t> bytes) {
  Prop prop;
  prop.name = std::move(name);
  prop.type = type;
  prop.bytes = std::move(bytes);
  return prop;
}

std::vector<std::string> SplitList(const std::string& list) {
  std::vector<std::string> items;
  size_t pos = 0;
  while (pos <= list.size()) {
    size_t end = list.find(',', pos);
    if (end == std::string::npos) end = list.size();
    std::string item = list.substr(pos, end - pos);
    item.erase(0, item.find_first_not_of(' '));
    item.erase(item.find_last_not_of(' ') + 1);
    if (!item.empty()) items.push_back(item);
    pos = end + 1;
  }
  return items;
}

// The override folder, handing what it doesn't have to the player's own Game folder: the redirect
// is a path prefix, so Worlds\Release also catches Worlds\ReleaseMultiplayer (as look_tuning's
// database folder catches database_anim).
class FallbackDevice final : public rex::filesystem::HostPathDevice {
 public:
  FallbackDevice(std::string_view mount_path, const std::filesystem::path& overrides,
                 const std::filesystem::path& original)
      : HostPathDevice(mount_path, overrides, true),
        original_(std::string(mount_path) + "Original", original, true) {}
  bool Initialize() override { return HostPathDevice::Initialize() && original_.Initialize(); }
  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    if (rex::filesystem::Entry* entry = HostPathDevice::ResolvePath(path)) return entry;
    return original_.ResolvePath(path);
  }

 private:
  rex::filesystem::HostPathDevice original_;
};

// The fighter's template: the map's own template AI of that model, or a soldier's with the model
// swapped in (its other settings, goal set included, stay a soldier's).
std::optional<Object> FighterTemplate(const std::vector<const Object*>& templates,
                                      const std::string& model, bool& exact) {
  for (const Object* t : templates) {
    if (EqualNoCase(t->Text("ModelTemplate"), model)) {
      exact = true;
      return *t;
    }
  }
  for (const Object* t : templates) {
    if (StartsNoCase(t->Text("ModelTemplate"), "Soldier")) {
      Object copy = *t;
      copy.SetText("ModelTemplate", model);
      for (Prop& prop : copy.props) {
        if (StartsNoCase(prop.name, "ChildModel_")) prop.text.clear();
      }
      exact = false;
      return copy;
    }
  }
  return std::nullopt;
}

}  // namespace

void Apply(rex::Runtime& runtime) {
  const std::string map = REXCVAR_GET(fear_pitfight);
  if (map.empty()) return;

  // The map: Game/Worlds/<folder>/<map>.wldsrvr in the player's game folder.
  std::error_code error;
  const auto worlds = runtime.game_data_root() / "Game" / "Worlds";
  std::filesystem::path source;
  for (const auto& entry : std::filesystem::directory_iterator(worlds, error)) {
    const auto candidate = entry.path() / (map + ".wldsrvr");
    if (entry.is_directory(error) && std::filesystem::exists(candidate, error)) {
      source = candidate;
      break;
    }
  }
  if (source.empty()) {
    REXLOG_ERROR("F.E.A.R.: PitFight: no {}.wldsrvr under {}; PitFight is off", map,
                 worlds.string());
    return;
  }
  const std::string folder = source.parent_path().filename().string();

  std::ifstream in(source, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  const auto objects = ParseObjects(data);
  if (!objects) {
    REXLOG_ERROR("F.E.A.R.: PitFight: could not read the objects in {}; PitFight is off",
                 source.string());
    return;
  }

  const auto models = SplitList(REXCVAR_GET(fear_pitfight_teams));
  if (models.size() != 2) {
    REXLOG_ERROR("F.E.A.R.: PitFight: fear_pitfight_teams needs two models, has '{}'",
                 REXCVAR_GET(fear_pitfight_teams));
    return;
  }

  // What the new objects are cloned from, and where they go.
  const Object* start = nullptr;
  const Object* startup = nullptr;
  std::vector<const Object*> spawners;
  std::vector<const Object*> templates;
  for (const Object& object : *objects) {
    if (object.cls == "GameStartPoint" && !start) start = &object;
    if (object.cls == "StartupCommand" && !startup) startup = &object;
    if (object.cls == "AISpawner") spawners.push_back(&object);
    if (object.cls == "CAI" && object.Word("Template")) templates.push_back(&object);
    if (EqualNoCase(object.Text("Name"), kArena)) {
      REXLOG_WARN("F.E.A.R.: PitFight: {} already has an object named {}", map, kArena);
    }
  }
  // Every spawner and the player start have a position and a rotation in the shipped maps.
  std::erase_if(spawners, [](const Object* s) { return !s->Find("Pos") || !s->Find("Rotation"); });
  if (!start || !start->Find("Pos") || !startup || spawners.size() < 2 || templates.empty()) {
    REXLOG_ERROR("F.E.A.R.: PitFight: {} lacks a player start, a startup command, two AI "
                 "spawners or a template AI; PitFight is off", map);
    return;
  }

  // The starting points: the map's spawner nearest the player's start, and the one nearest that
  // at least kMinApart away (spawners stand where the AI can).
  const Vec home = PosOf(*start);
  std::sort(spawners.begin(), spawners.end(), [&](const Object* a, const Object* b) {
    return Distance(PosOf(*a), home) < Distance(PosOf(*b), home);
  });
  const Object* first = spawners[0];
  std::vector<const Object*> by_first(spawners.begin() + 1, spawners.end());
  std::sort(by_first.begin(), by_first.end(), [&](const Object* a, const Object* b) {
    return Distance(PosOf(*a), PosOf(*first)) < Distance(PosOf(*b), PosOf(*first));
  });
  const Object* second = by_first.back();
  for (const Object* candidate : by_first) {
    if (Distance(PosOf(*candidate), PosOf(*first)) >= kMinApart) {
      second = candidate;
      break;
    }
  }
  const Object* points[2] = {first, second};

  const std::vector<uint8_t> identity = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3F, 0x80, 0, 0};
  std::vector<Object> added;
  {
    Object arena;
    arena.cls = "PitFight";
    arena.props = {Text("Name", kString, kArena), Bytes("Pos", kVector, start->Find("Pos")->bytes),
                   Bytes("Rotation", kRotation, identity)};
    Prop template_flag;
    template_flag.name = "Template";
    template_flag.type = kBool;
    arena.props.push_back(template_flag);
    added.push_back(std::move(arena));
  }
  std::string setup = fmt::format("msg {0} (NUMTEAMS 2);msg {0} (SCORETOWIN {1})", kArena,
                                  std::min(REXCVAR_GET(fear_pitfight_score), 255u));
  std::string summary;
  for (int team = 0; team < 2; ++team) {
    const std::string& model = models[team];
    bool exact = false;
    auto fighter = FighterTemplate(templates, model, exact);
    if (!fighter) {
      REXLOG_ERROR("F.E.A.R.: PitFight: {} has no template AI to make a {} from", map, model);
      return;
    }
    const std::string template_name = fmt::format("PitFight_Template{}", team + 1);
    // The arena tells the teams apart by the first number in a fighter's name: its team plus one.
    const std::string fighter_name = fmt::format("PitFight_Fighter{}", team + 1);
    const std::string source_template = fighter->Text("Name");
    fighter->SetText("Name", template_name);
    fighter->SetText("DeathCommand", fmt::format("msg {} (FIGHTERDIED {})", kArena, team));
    fighter->SetText("Alignment", kAlignments[team]);
    fighter->SetText("Awareness", "Alert");
    fighter->SetWord("SeekEnemy", 1);
    fighter->SetText("BehaviorNode", "");
    fighter->SetText("InitialNode", "");
    fighter->SetWord("Template", 1);
    added.push_back(std::move(*fighter));

    Object spawner = *spawners[0];
    spawner.SetText("Name", fmt::format("{}_AISpawner_{:02}", model, team));
    spawner.SetBytes("Pos", points[team]->Find("Pos")->bytes);
    spawner.SetText("Target", template_name);
    spawner.SetText("SpawnedAIName", fighter_name);
    spawner.SetText("InitialCommand", "");
    spawner.SetText("Alignment", kAlignments[team]);
    spawner.SetText("Awareness", "Alert");
    spawner.SetWord("SeekEnemy", 1);
    spawner.SetText("BehaviorNode", "");
    spawner.SetText("InitialNode", "");
    spawner.SetWord("Template", 0);
    added.push_back(std::move(spawner));

    Object point;
    point.cls = "Point";
    point.props = {Text("Name", kString, fmt::format("StartingPoint{:02}", team)),
                   Bytes("Pos", kVector, points[team]->Find("Pos")->bytes),
                   Bytes("Rotation", kRotation, points[team]->Find("Rotation")->bytes)};
    added.push_back(std::move(point));

    setup += fmt::format(";msg {} (TEAMMODEL {} {})", kArena, team, model);
    summary += fmt::format("{}team {}: {} ({} from {}, {}) at {}", team ? "; " : "", team, model,
                           exact ? "template" : "soldier template", source_template,
                           kAlignments[team], points[team]->Text("Name"));
  }
  setup += fmt::format(";delay 3 (msg {} START)", kArena);
  {
    Object go = *startup;
    go.SetText("Name", "PitFight_Start");
    go.SetText("Command", setup);
    go.SetWord("Template", 0);
    added.push_back(std::move(go));
  }

  std::vector<uint8_t> patched = data;
  const uint32_t count = uint32_t(objects->size() + added.size());
  for (int i = 0; i < 4; ++i) patched[8 + i] = uint8_t(count >> (24 - 8 * i));
  for (const Object& object : added) AppendObject(patched, object);

  // The override folder: the patched map, and hard links to the folder's other files (no copies,
  // the same drive), as the game resolves the folder and then looks each name up in it. Off the
  // game's drive, only this map's own files are copied, and the other maps of the folder won't
  // load while PitFight is on.
  const auto root = runtime.user_data_root() / "overrides" / "PitFight" / "Game";
  const auto dest = root / "Worlds" / folder;
  std::filesystem::create_directories(dest, error);
  size_t linked = 0, copied = 0, missing = 0;
  for (const auto& entry : std::filesystem::directory_iterator(source.parent_path(), error)) {
    if (!entry.is_regular_file(error)) continue;
    const auto name = entry.path().filename();
    if (EqualNoCase(name.string(), source.filename().string())) continue;
    const auto target = dest / name;
    std::error_code link_error;
    std::filesystem::remove(target, link_error);
    std::filesystem::create_hard_link(entry.path(), target, link_error);
    if (!link_error) {
      ++linked;
    } else if (StartsNoCase(name.string(), map + ".") &&
               std::filesystem::copy_file(entry.path(), target, link_error)) {
      ++copied;
    } else {
      ++missing;
    }
  }
  {
    std::ofstream out(dest / source.filename(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(patched.data()), std::streamsize(patched.size()));
    if (!out) {
      REXLOG_ERROR("F.E.A.R.: PitFight: could not write the patched map under {}", dest.string());
      return;
    }
  }
  auto device =
      std::make_unique<FallbackDevice>(kMount, root, runtime.game_data_root() / "Game");
  auto* vfs = runtime.file_system();
  if (!vfs || !device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink(std::string(kGuestWorlds) + folder,
                                 fmt::format("{}\\Worlds\\{}", kMount, folder))) {
    REXLOG_ERROR("F.E.A.R.: PitFight: could not redirect the game's Worlds\\{} folder", folder);
    return;
  }
  REXLOG_INFO("F.E.A.R.: PitFight in {}: {}; {:.0f} apart, {:.0f} from the player's start; first "
              "to {} ({} objects added to the map's {}; folder {}: {} linked, {} copied, {} left "
              "out)",
              map, summary, Distance(PosOf(*first), PosOf(*second)), Distance(PosOf(*first), home),
              REXCVAR_GET(fear_pitfight_score), added.size(), objects->size(), dest.string(),
              linked, copied, missing);
}

}  // namespace fear::pitfight
