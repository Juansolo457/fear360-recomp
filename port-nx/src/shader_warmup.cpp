// F.E.A.R. 360 - the shader warm-up list and its progress panel. See shader_warmup.h.

#include "shader_warmup.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <imgui.h>
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fear_panel.h"

// zlib, for the catalog's packed files: stb_image's decoder, private to this file.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

extern const rex::PPCImageInfo PPCImageConfig;

REXCVAR_DEFINE_BOOL(fear_shader_warmup, true, "F.E.A.R.",
                    "Before the game starts, build the shaders and pipelines seen in play (this "
                    "build's list and the earlier runs') on all cores, with a progress panel; off "
                    "= build them as the game first draws with them (#53)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

// This game's list (<project>/warmup/shader_warmup.inc, from tools/make_warmup.py). Its lines:
//   warmup 1 <title id> <PipelineKey bytes>
//   s <hash> <type> <dwords> <modification,...> cat <effect> <byte offset> [<dword>:<xor> ...]
//   s <hash> <type> <dwords> <modification,...> exe <offset in the image> [<dword>:<xor> ...]
//   p <PipelineKey's 32-bit words>
// Numbers in hex except dwords and dword indices.
constexpr char kList[] =
#include "shader_warmup.inc"
    ;

std::filesystem::path g_game_data_root;

std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_total{0};
std::atomic<uint32_t> g_done{0};
std::atomic<int64_t> g_begun{0};  // steady_clock ticks
std::mutex g_callback_mutex;
std::function<void()> g_on_begin;

std::vector<std::string_view> Split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find(separator, pos);
    if (end == std::string_view::npos) end = text.size();
    if (end > pos) parts.push_back(text.substr(pos, end - pos));
    pos = end + 1;
  }
  return parts;
}

template <typename T>
bool Number(std::string_view text, T& value, int base = 16) {
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  return error == std::errc() && end == text.data() + text.size();
}

// Game/uberCatalog.cat: a header (big-endian: version, entry table offset, entry count, name table
// offset and size), 16-byte entries (name offset, data offset, size, stored size) and the names.
// A file stored smaller than its size is zlib.
class Catalog {
 public:
  explicit Catalog(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    uint32_t header[5];
    if (!Read(0, header, sizeof(header))) return;
    for (uint32_t& word : header) word = std::byteswap(word);
    const uint32_t table = header[1], count = header[2], names_at = header[3];
    const uint32_t names_size = header[4];
    if (count > 1u << 20 || names_size > 64u << 20) return;
    std::vector<uint32_t> entries(size_t(count) * 4);
    std::string names(names_size, '\0');
    if (!Read(table, entries.data(), entries.size() * 4) ||
        !Read(names_at, names.data(), names.size())) {
      return;
    }
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t name_at = std::byteswap(entries[i * 4]);
      if (name_at >= names.size()) continue;
      const std::string_view name(names.c_str() + name_at);
      entries_.emplace(std::string(name),
                       Entry{std::byteswap(entries[i * 4 + 1]), std::byteswap(entries[i * 4 + 2]),
                             std::byteswap(entries[i * 4 + 3])});
    }
  }

  bool ok() const { return !entries_.empty(); }

  // A file's contents, unpacked; empty if missing or damaged.
  std::vector<uint8_t> Get(const std::string& name) {
    const auto it = entries_.find(name);
    if (it == entries_.end() || it->second.size > 64u << 20 || it->second.stored > 64u << 20) {
      return {};
    }
    const Entry& entry = it->second;
    std::vector<uint8_t> stored(entry.stored);
    if (!Read(entry.offset, stored.data(), stored.size())) return {};
    if (entry.stored == entry.size) return stored;
    std::vector<uint8_t> data(entry.size);
    const int unpacked = stbi_zlib_decode_buffer(
        reinterpret_cast<char*>(data.data()), int(data.size()),
        reinterpret_cast<const char*>(stored.data()), int(stored.size()));
    if (unpacked != int(entry.size)) return {};
    return data;
  }

 private:
  struct Entry {
    uint32_t offset, size, stored;
  };

  bool Read(uint64_t offset, void* out, size_t size) {
    file_.seekg(std::streamoff(offset));
    file_.read(static_cast<char*>(out), std::streamsize(size));
    return bool(file_);
  }

  std::ifstream file_;
  std::map<std::string, Entry> entries_;
};

class WarmupPanel final : public fear::panel::Panel {
 public:
  explicit WarmupPanel(rex::ui::ImGuiDrawer* drawer) : Panel(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    if (!g_active.load()) {
      if (!closing_) {
        closing_ = true;
        Close();
      }
      return;
    }
    // Nothing for the first half second: with the driver's cache warm it is done by then.
    const auto begun = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(g_begun.load()));
    if (std::chrono::steady_clock::now() - begun < std::chrono::milliseconds(500)) return;
    const uint32_t total = std::max(g_total.load(), 1u);
    const uint32_t done = std::min(g_done.load(), total);
    if (fear::panel::BeginPanel("##fear_warmup", 26.0f)) {
      ImGui::TextUnformatted("PREPARING SHADERS");
      ImGui::Separator();
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.62f, 0.10f, 0.08f, 0.95f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
      ImGui::ProgressBar(float(done) / float(total), ImVec2(-1.0f, 0.0f),
                         fmt::format("{} / {}", done, total).c_str());
      ImGui::PopStyleColor(2);
      fear::panel::Dim(
          "Building the game's shaders for this graphics card and driver, once: later starts skip "
          "this.");
    }
    fear::panel::EndPanel();
  }

 private:
  bool closing_ = false;
};

}  // namespace

namespace fear::shader_warmup {

bool Enabled() { return REXCVAR_GET(fear_shader_warmup); }

void SetGameDataRoot(const std::filesystem::path& root) { g_game_data_root = root; }

List Load(uint32_t title_id) {
  List list;
  if (!Enabled()) return list;
  const auto lines = Split(std::string_view(kList, sizeof(kList) - 1), '\n');
  if (lines.empty()) return list;
  const auto header = Split(lines[0], ' ');
  uint32_t list_title = 0;
  if (header.size() != 4 || header[0] != "warmup" || header[1] != "1" ||
      !Number(header[2], list_title) || !Number(header[3], list.key_size, 10)) {
    REXLOG_WARN("F.E.A.R.: shader warm-up list unreadable");
    return list;
  }
  if (list_title != title_id) {
    REXLOG_WARN("F.E.A.R.: shader warm-up list is for title {:08X}, not {:08X}", list_title,
                title_id);
    return list;
  }

  Catalog catalog(g_game_data_root / "Game" / "uberCatalog.cat");
  std::map<std::string, std::vector<uint8_t>> effects;
  auto* memory = rex::system::kernel_memory();
  const uint8_t* image =
      memory ? memory->virtual_membase() + PPCImageConfig.image_base : nullptr;
  const uint32_t image_size = PPCImageConfig.image_size;

  size_t listed = 0;
  for (size_t i = 1; i < lines.size(); ++i) {
    const auto parts = Split(lines[i], ' ');
    if (parts.empty()) continue;
    if (parts[0] == "p") {
      std::vector<uint32_t> words(parts.size() - 1);
      bool ok = words.size() * 4 == list.key_size;
      for (size_t w = 0; ok && w < words.size(); ++w) ok = Number(parts[w + 1], words[w]);
      if (ok) list.pipelines.push_back(std::move(words));
      continue;
    }
    if (parts[0] != "s" || parts.size() < 7) continue;
    ++listed;
    Shader shader;
    uint32_t dwords = 0;
    if (!Number(parts[1], shader.hash) || !Number(parts[2], shader.type, 10) || shader.type > 1 ||
        !Number(parts[3], dwords, 10) || !dwords || dwords > 65536) {
      continue;
    }
    bool ok = true;
    for (std::string_view text : Split(parts[4], ',')) {
      uint64_t modification = 0;
      ok = ok && Number(text, modification);
      shader.modifications.push_back(modification);
    }
    // Where the microcode is.
    const uint8_t* source = nullptr;
    size_t patches_at = 0;
    if (parts[5] == "cat" && parts.size() >= 8) {
      const std::string name(parts[6]);
      auto effect = effects.find(name);
      if (effect == effects.end()) {
        effect = effects.emplace(name, catalog.ok() ? catalog.Get(name) : std::vector<uint8_t>{})
                     .first;
      }
      uint32_t offset = 0;
      if (Number(parts[7], offset) && size_t(offset) + dwords * 4 <= effect->second.size()) {
        source = effect->second.data() + offset;
      }
      patches_at = 8;
    } else if (parts[5] == "exe") {
      uint32_t offset = 0;
      if (image && Number(parts[6], offset) && size_t(offset) + dwords * 4 <= image_size) {
        source = image + offset;
      }
      patches_at = 7;
    }
    if (!ok || !source) continue;
    shader.ucode.resize(dwords);
    std::memcpy(shader.ucode.data(), source, size_t(dwords) * 4);
    // A bound vertex shader: the vertex fetch dwords D3D patched, as XORs of their values.
    for (size_t p = patches_at; ok && p < parts.size(); ++p) {
      const size_t colon = parts[p].find(':');
      uint32_t index = 0, change = 0;
      ok = colon != std::string_view::npos && Number(parts[p].substr(0, colon), index, 10) &&
           Number(parts[p].substr(colon + 1), change) && index < dwords;
      if (ok) {
        shader.ucode[index] = std::byteswap(std::byteswap(shader.ucode[index]) ^ change);
      }
    }
    if (!ok || XXH3_64bits(shader.ucode.data(), shader.ucode.size() * 4) != shader.hash) {
      continue;
    }
    list.shaders.push_back(std::move(shader));
  }
  REXLOG_INFO("F.E.A.R.: shader warm-up list: {} of {} shaders rebuilt from the game data, {} "
              "pipelines",
              list.shaders.size(), listed, list.pipelines.size());
  return list;
}

void Begin(uint32_t total) {
  g_total = total;
  g_done = 0;
  g_begun = std::chrono::steady_clock::now().time_since_epoch().count();
  g_active = true;
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    callback = g_on_begin;
  }
  if (callback) callback();
}

void Advance(uint32_t done) { g_done = done; }

void End() { g_active = false; }

void SetBeginCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_on_begin = std::move(callback);
}

void OpenPanel(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || !g_active.load()) return;
  new WarmupPanel(drawer);  // deletes itself when closed (ImGuiDialog)
}

}  // namespace fear::shader_warmup
