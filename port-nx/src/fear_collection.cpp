// F.E.A.R. 360 - the collection menu. See fear_collection.h.

#include "fear_collection.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "fear_process.h"
#include "fear_title.h"
#include "fear_version.h"

REXCVAR_DEFINE_STRING(fear_collection, "", "F.E.A.R.",
                      "Collection file (tools/launch.ps1 -Collection writes it): show the menu of "
                      "F.E.A.R. and F.E.A.R. Files before a game starts; empty = start this game")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(fear_collection_start, "", "F.E.A.R.",
                      "Set when the collection menu starts the other game's program: start that "
                      "title without showing the menu")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fear::collection {
namespace {

namespace fs = std::filesystem;
using namespace fear::panel;

struct Title {
  std::string id;
  std::string name;
  std::string subtitle;
  fs::path exe;
  fs::path working_dir;
  std::vector<std::string> args;
};

fs::path Utf8Path(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::vector<Title> LoadTitles(const std::string& file) {
  std::vector<Title> titles;
  try {
    const toml::table table = toml::parse_file(file);
    const toml::array* list = table["title"].as_array();
    if (!list) return titles;
    for (const toml::node& node : *list) {
      const toml::table* entry = node.as_table();
      if (!entry) continue;
      Title title;
      title.id = (*entry)["id"].value_or(std::string());
      title.name = (*entry)["name"].value_or(std::string(title.id));
      title.subtitle = (*entry)["subtitle"].value_or(std::string());
      title.exe = Utf8Path((*entry)["exe"].value_or(std::string()));
      title.working_dir = Utf8Path((*entry)["working_dir"].value_or(std::string()));
      if (const toml::array* args = (*entry)["args"].as_array()) {
        for (const toml::node& arg : *args) {
          if (auto value = arg.value<std::string>()) title.args.push_back(*value);
        }
      }
      if (!title.id.empty()) titles.push_back(std::move(title));
    }
  } catch (const toml::parse_error& error) {
    REXLOG_ERROR("F.E.A.R. collection: can't read {}: {}", file, error.description());
  }
  return titles;
}

class CollectionMenu final : public Panel {
 public:
  CollectionMenu(const Host& host, std::string file, std::vector<Title> titles,
                 std::function<void()> start)
      : Panel(host.drawer),
        host_(host),
        file_(std::move(file)),
        titles_(std::move(titles)),
        start_(std::move(start)) {
    for (const Title& title : titles_) {
      std::error_code error;
      installed_.push_back(title.id == fear::title::kId || fs::exists(title.exe, error));
    }
    // Start on this program's own game.
    for (size_t i = 0; i < titles_.size(); ++i) {
      if (titles_[i].id == fear::title::kId) selected_ = int(i);
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const int rows = int(titles_.size()) + 1;  // the titles, Quit
    const uint32_t input = closing_ ? 0 : navigator_.Poll();
    if (input & kUp) selected_ = (selected_ + rows - 1) % rows;
    if (input & kDown) selected_ = (selected_ + 1) % rows;
    if (input & kAccept) Act(selected_);

    DrawMenu(ImGui::GetIO().DisplaySize);
  }

 private:
  // The game's menu look (#43): a full-screen backdrop in the main menus' dark teal with faint
  // scanlines, the title large over a spaced subtitle, each game's name large with its subtitle
  // smaller and dimmer under it, the selected one in the menus' pale yellow beside a teal bar, Quit
  // smaller and apart, and the help line and the A prompt where the game puts its own.
  static constexpr ImU32 kWhite = IM_COL32(232, 238, 236, 255);
  static constexpr ImU32 kSelected = IM_COL32(236, 226, 160, 255);  // the menus' highlight
  static constexpr ImU32 kTeal = IM_COL32(110, 190, 172, 255);
  static constexpr ImU32 kDimTeal = IM_COL32(120, 150, 142, 255);
  static constexpr ImU32 kGrey = IM_COL32(130, 138, 136, 255);

  // A selectable row: the mouse moving over it selects it, a click picks it.
  bool HitRow(int index, ImVec2 min, ImVec2 max) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(index);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(max.x - min.x, max.y - min.y));
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
      selected_ = index;
    }
    ImGui::PopID();
    if (clicked) selected_ = index;
    return clicked;
  }

  void DrawMenu(ImVec2 size) {
    const float em = FontSize();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    PushFont();
    ImFont* font = ImGui::GetFont();
    const bool open = ImGui::Begin(
        "##fear_collection", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    if (open) {
      ImDrawList* draw = ImGui::GetWindowDrawList();
      // The backdrop: the main menus' teal, darker at the bottom, and scanlines.
      draw->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), size, IM_COL32(8, 38, 34, 255),
                                    IM_COL32(4, 26, 26, 255), IM_COL32(2, 10, 10, 255),
                                    IM_COL32(3, 16, 15, 255));
      const float line = std::max(2.0f, em * 0.09f);
      for (float y = 0.0f; y < size.y; y += line * 2.0f) {
        draw->AddRectFilled(ImVec2(0.0f, y), ImVec2(size.x, y + line), IM_COL32(0, 0, 0, 40));
      }
      const auto text = [&](float scale, ImVec2 pos, ImU32 color, const char* string) {
        draw->AddText(font, em * scale, pos, color, string);
      };

      const float left = size.x * 0.12f;
      // What this is (#97): the two games, then the recompilation and the platform it was built for.
#ifdef _WIN32
      constexpr const char* kSubtitle = "XBOX 360  STATIC RECOMPILATION  FOR PC";
#else
      constexpr const char* kSubtitle = "XBOX 360  STATIC RECOMPILATION  FOR LINUX";
#endif
      float y = size.y * 0.16f;
      text(2.2f, ImVec2(left, y), kWhite, "F.E.A.R.  &  F.E.A.R. Files");
      y += em * 2.4f;
      text(0.85f, ImVec2(left + em * 0.08f, y), kTeal, kSubtitle);
      y += em * 3.0f;

      const float row_width = std::max(size.x * 0.45f, em * 16.0f);
      const int titles = int(titles_.size());
      for (int i = 0; i < titles; ++i) {
        const Title& title = titles_[i];
        const float height = em * (title.subtitle.empty() ? 1.9f : 2.8f);
        const ImVec2 min(left - em * 0.7f, y), max(left + row_width, y + height);
        if (HitRow(i, min, max)) Act(i);
        const bool on = selected_ == i;
        if (on) {
          draw->AddRectFilled(min, ImVec2(min.x + em * 0.16f, max.y), kTeal);
          draw->AddRectFilledMultiColor(ImVec2(min.x + em * 0.16f, min.y), max,
                                        IM_COL32(110, 190, 172, 46), IM_COL32(110, 190, 172, 0),
                                        IM_COL32(110, 190, 172, 0), IM_COL32(110, 190, 172, 46));
        }
        std::string name = title.name;
        if (!installed_[i]) name += "  (not found)";
        text(1.6f, ImVec2(left, y + em * 0.1f), on ? kSelected : kWhite, name.c_str());
        if (!title.subtitle.empty()) {
          text(0.85f, ImVec2(left + em * 0.05f, y + em * 1.85f), on ? kTeal : kDimTeal,
               title.subtitle.c_str());
        }
        y += height + em * 0.5f;
      }
      y += em * 0.6f;
      {
        const ImVec2 min(left - em * 0.7f, y), max(left + row_width * 0.5f, y + em * 1.4f);
        if (HitRow(titles, min, max)) Act(titles);
        const bool on = selected_ == titles;
        if (on) draw->AddRectFilled(min, ImVec2(min.x + em * 0.16f, max.y), kTeal);
        text(1.05f, ImVec2(left, y + em * 0.12f), on ? kSelected : kGrey, "Quit to Desktop");
      }

      // The help line, as the game's: what the selected row does, in the highlight colour.
      std::string help;
      if (!message_.empty()) {
        help = message_;
      } else if (selected_ < titles && !installed_[selected_]) {
        help = "Not found: " + titles_[selected_].exe.string();
      } else if (selected_ < titles) {
        // "Play F.E.A.R." ends its own sentence.
        const std::string& name = titles_[selected_].name;
        help = "Play " + name + (name.ends_with('.') ? "" : ".") +
               " Switch Game in its main menu comes back here.";
      } else {
        help = "Quit to the desktop.";
      }
      // Wrapped short of the A prompt: an error (message_, a missing program's path) can be long.
      const float bottom = size.y * 0.86f;
      draw->AddText(font, em * 0.8f, ImVec2(left + em * 0.05f, bottom), kSelected, help.c_str(),
                    nullptr, size.x * 0.6f);
      // The A prompt, bottom right, and the version under the title's subtitle line.
      const char* prompt = "A  Select";
      const ImVec2 prompt_size = font->CalcTextSizeA(em * 0.9f, FLT_MAX, 0.0f, prompt);
      const ImVec2 prompt_pos(size.x * 0.88f - prompt_size.x, size.y * 0.9f);
      draw->AddCircleFilled(ImVec2(prompt_pos.x + em * 0.32f, prompt_pos.y + em * 0.48f),
                            em * 0.42f, IM_COL32(90, 170, 60, 255));
      text(0.9f, prompt_pos, kWhite, prompt);
      text(0.6f, ImVec2(left + em * 0.08f, size.y * 0.16f + em * 3.6f), kGrey,
           fear::version::Text());
    }
    ImGui::End();
    PopFont();
    ImGui::PopStyleVar(2);
  }


  void Act(int row) {
    if (closing_) return;
    if (row == int(titles_.size())) {
      Finish();
      if (host_.later && host_.quit) host_.later(host_.quit);
      return;
    }
    const Title& title = titles_[row];
    if (title.id == fear::title::kId) {
      // This program's game: carry on starting it, after this frame, with the controllers left
      // for the runtime's driver (fear_panel.h).
      Finish();
      if (host_.later) {
        host_.later([start = start_] {
          fear::panel::ReleaseControllers();
          start();
        });
      }
      return;
    }
    if (!installed_[row]) return;
    std::vector<std::string> args = title.args;
    args.insert(args.end(), {"--fear_collection", file_, "--fear_collection_start", title.id});
    if (!fear::process::StartAfterThis(title.exe, std::move(args), title.working_dir)) {
      message_ = "Couldn't start " + title.exe.string() + ".";
      return;
    }
    REXLOG_INFO("F.E.A.R. collection: starting {} ({})", title.name, title.exe.string());
    Finish();
    if (host_.later && host_.quit) host_.later(host_.quit);
  }

  void Finish() {
    closing_ = true;
    Close();
  }

  Host host_;
  std::string file_;
  std::vector<Title> titles_;
  std::vector<bool> installed_;
  std::function<void()> start_;
  std::string message_;
  bool closing_ = false;};

}  // namespace

bool Enabled() { return !REXCVAR_GET(fear_collection).empty(); }

bool StartNow(const Host& host, std::function<void()> start) {
  const std::string file = REXCVAR_GET(fear_collection);
  if (file.empty() || !REXCVAR_GET(fear_collection_start).empty() || !host.drawer) return true;
  std::vector<Title> titles = LoadTitles(file);
  if (titles.empty()) {
    REXLOG_ERROR("F.E.A.R. collection: no titles in {}; starting this game", file);
    return true;
  }
  new CollectionMenu(host, file, std::move(titles), std::move(start));  // deletes itself
  return false;
}

bool SwitchGame() {
  return fear::process::StartAfterThis(
      fear::process::OwnExecutable(),
      fear::process::WithoutFlags(fear::process::OwnArguments(), {"fear_collection_start"}),
      fs::current_path());
}

}  // namespace fear::collection
