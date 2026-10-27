// F.E.A.R. 360 - the collection menu. See fear_collection.h.

#include "fear_collection.h"

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

    if (BeginPanel("##fear_collection", 30.0f)) {
      PushFont(1.3f);
      ImGui::TextUnformatted("F.E.A.R. COLLECTION");
      PopFont();
      ImGui::SameLine();
      ImGui::TextDisabled("%s", fear::version::Text());
      ImGui::Separator();
      for (int i = 0; i < int(titles_.size()); ++i) {
        const Title& title = titles_[i];
        std::string label = title.name;
        if (!installed_[i]) label += "  (not found)";
        if (Row(label.c_str(), i, selected_)) Act(i);
        if (!title.subtitle.empty()) Dim(title.subtitle.c_str());
      }
      ImGui::Separator();
      if (Row("Quit", rows - 1, selected_)) Act(rows - 1);
      ImGui::Separator();
      if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
      } else if (selected_ < int(titles_.size()) && !installed_[selected_]) {
        Dim(("Not found: " + titles_[selected_].exe.string()).c_str());
      } else {
        Dim("Switch Game in a game's main menu comes back here.");
      }
    }
    EndPanel();
  }

 private:
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
  bool closing_ = false;
};

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
