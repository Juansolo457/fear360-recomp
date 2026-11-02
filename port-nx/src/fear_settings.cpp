// F.E.A.R. 360 - the Recompilation Settings panel and the quit confirmation. See fear_settings.h.

#include "fear_settings.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>

#include "display_vblank.h"
#include "fear_controls.h"
#include "fear_mkb.h"
#include "fear_panel.h"
#include "fear_process.h"
#include "fear_title.h"
#include "fear_version.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace fear::panel;

fs::path g_path;  // <user data>/settings.toml

// --- Settings ---

struct Option {
  std::string value;
  std::string label;
};

struct Setting {
  std::string label;
  std::string cvar;
  std::vector<Option> options;
  bool live = false;  // applies at once; otherwise on the next start
  std::string note;
};

std::string CvarValue(const std::string& name) { return rex::cvar::GetFlagByName(name); }

// Values as the cvar prints them against an option's: numbers by value, the rest as text.
bool SameValue(const std::string& a, const std::string& b) {
  try {
    size_t used_a = 0, used_b = 0;
    const double x = std::stod(a, &used_a);
    const double y = std::stod(b, &used_b);
    if (used_a == a.size() && used_b == b.size()) return std::abs(x - y) < 1e-6;
  } catch (...) {
  }
  return a == b;
}

std::string FrameRateLabel(double limit) {
  const auto& plan = fear::display_vblank::GetPlan();
  if (!plan.active || plan.display_hz <= 0) return fmt::format("Up to {:g} fps", limit);
  const double refreshes = std::max(1.0, std::ceil(plan.display_hz / limit - 1e-6));
  const double fps = plan.display_hz / refreshes;
  if (std::abs(fps - limit) < 0.05) return fmt::format("{:g} fps", fps);
  return fmt::format("Up to {:g}: {:.4g} fps at {:.0f} Hz", limit, fps, plan.display_hz);
}

std::vector<Setting> BuildSettings() {
  std::vector<Setting> settings;
  if (!fear::title::kFiles) {
    // What plays (fear_mkb.h); the menus and the panels take both devices either way.
    settings.push_back({"Input",
                        "fear_mkb",
                        {{"true", "Keyboard and mouse"}, {"false", "Controller"}},
                        false,
                        "Set the controls used in the game. Both work in the menus. Changing "
                        "this restarts the game."});
  }
  settings.push_back({"Display mode",
                      "fullscreen",
                      {{"true", "Fullscreen"}, {"false", "Windowed"}},
                      true,
                      "Set fullscreen or windowed display. Alt+Enter also switches."});
#if defined(_WIN32)
  // The D3D12 renderer's (the Vulkan renderer, Linux's, draws at the 360's 1280x720).
  settings.push_back({"Resolution",
                      "fear_d3d12_resolution_scale",
                      {{"0", "Match display"},
                       {"1", "1280x720 (Xbox 360)"},
                       {"2", "2560x1440"},
                       {"3", "3840x2160"}},
                      false,
                      "Set the resolution the game is drawn at. Lower values can increase "
                      "performance."});
#endif
  settings.push_back({"Frame rate limit",
                      "fear_frame_rate_limit",
                      {{"30", FrameRateLabel(30) + " (Xbox 360)"},
                       {"60", FrameRateLabel(60)},
                       {"90", FrameRateLabel(90)},
                       {"120", FrameRateLabel(120)}},
                      false,
                      "Set the maximum frame rate. Frames are paced to the display's refresh "
                      "rate."});
  settings.push_back({"Anti-aliasing",
                      "fear_d3d12_msaa",
                      {{"true", "On"}, {"false", "Off"}},
                      false,
                      "Toggle full screen anti-aliasing. Turning it off can increase "
                      "performance, most of all on the Steam Deck."});
  settings.push_back({"Anisotropic filtering",
                      "fear_d3d12_anisotropy",
                      {{"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
                      false,
                      "Set the level of anisotropic filtering. Higher values sharpen textures "
                      "seen at an angle."});
  settings.push_back({"Texture sharpness",
                      "fear_mip_bias",
                      {{"0", "Normal"}, {"-0.5", "Sharper"}, {"-1", "Sharpest"}},
                      false,
                      "Set the sharpness of distant textures. Sharpest can shimmer on fine "
                      "patterns."});
  // The texture pack (docs/TEXTURES.md), in both renderers.
  const std::string pack = CvarValue("fear_texture_pack");
  if (pack.empty()) {
    settings.push_back({"PC textures",
                        "fear_texture_pack_enabled",
                        {{"true", "Not installed"}},
                        false,
                        "Use the PC version's textures and sharper text. Build them from your own "
                        "copy of the PC game with tools/texture_pack.py."});
  } else {
    settings.push_back({"PC textures",
                        "fear_texture_pack_enabled",
                        {{"true", "On"}, {"false", "Off"}},
                        false,
                        "Toggle the PC version's textures and sharper text, built from your own "
                        "copy: " +
                            pack});
  }
  settings.push_back({"Look acceleration",
                      "fear_look_ramp",
                      {{"off", "Off"},
                       {"console", "Xbox 360"},
                       {"nodelay", "Immediate"},
                       {"instant", "Full speed"}},
                      false,
                      "Set how turning speeds up with the right stick held at its edge. Off "
                      "keeps turning in step with the stick."});
  settings.push_back({"Aim assist",
                      "fear_aim_softlock",
                      {{"true", "On"}, {"false", "Off"}},
                      false,
                      "Toggle aim assist. Turning slows when your aim crosses an enemy."});
  settings.push_back({"Field of view",
                      "fear_fov",
                      {{"65", "65"},
                       {"70", "70 (default)"},
                       {"75", "75"},
                       {"80", "80"},
                       {"85", "85"},
                       {"90", "90"},
                       {"95", "95"},
                       {"100", "100"}},
                      false,
                      "Set the vertical field of view. Above 70 the weapon moves forward to "
                      "stay in view."});
  settings.push_back({"Bloom",
                      "fear_bloom_intensity",
                      {{"0", "Off (PC)"}, {"-1", "On (Xbox 360)"}},
                      false,
                      "Toggle the Xbox 360 version's bloom. Bright lights spread a haze over the "
                      "scene."});
  settings.push_back({"Enemy AI",
                      "fear_ai",
                      {{"sharp", "Sharp"}, {"pc", "PC"}, {"360", "Xbox 360"}},
                      false,
                      "Set enemy tactics. Sharp uses Very Hard tactics at every difficulty; "
                      "damage still follows the difficulty."});
  settings.push_back({"Blood and armor marks",
                      "fear_body_marks",
                      {{"true", "On (PC)"}, {"false", "Off (Xbox 360)"}},
                      false,
                      "Toggle blood and armor marks from bullet hits on enemies."});
  // The database options (look_tuning.h): F.E.A.R. only until F.E.A.R. Files' database, inside its
  // catalogue, can be changed too.
  if (!fear::title::kFiles) {
    settings.push_back({"Weapon slots",
                        "fear_weapon_slots",
                        {{"3", "3 (default)"}, {"4", "4"}, {"5", "5"}, {"10", "10 (all weapons)"}},
                        false,
                        "Set the maximum number of weapons you may carry."});
    settings.push_back({"John Wick mode",
                        "fear_john_wick",
                        {{"false", "Off"}, {"true", "On"}},
                        false,
                        "Toggle infinite ammo, dual pistols, faster movement and a SlowMo meter "
                        "that never runs out."});
    settings.push_back({"Automatic shotgun",
                        "fear_auto_shotgun",
                        {{"false", "Off"}, {"true", "On"}},
                        false,
                        "Toggle the automatic shotgun left unused on the Xbox 360 disc. It "
                        "replaces the VK-12 wherever one is found."});
    settings.push_back({"Flashlight battery",
                        "fear_infinite_flashlight",
                        {{"false", "Normal"}, {"true", "Unlimited"}},
                        false,
                        "Set the flashlight's battery life. Unlimited also hides the battery "
                        "meter."});
    settings.push_back({"Intro videos",
                        "fear_intro_videos",
                        {{"all", "All"}, {"monolith", "Monolith only"}, {"none", "Skip"}},
                        false,
                        "Set which logo videos play at startup."});
  }
  settings.push_back({"Letterbox",
                      "fear_letterbox",
                      {{"true", "On"}, {"false", "Off"}},
                      false,
                      "Toggle the black bars at the top and bottom of the screen during "
                      "cutscenes."});
  settings.push_back({"Model detail",
                      "fear_full_detail_models",
                      {{"false", "Normal"}, {"true", "Maximum"}},
                      false,
                      "Set the detail of distant characters and objects. Lower values can "
                      "increase performance."});
  settings.push_back({"Bodies and debris",
                      "fear_bodies_stay",
                      {{"false", "Fade"}, {"true", "Stay"}},
                      false,
                      fear::title::kFiles
                          ? "Toggle whether bodies, bullet holes, blood, debris and shell "
                            "casings stay instead of fading away."
                          : "Toggle whether bodies, bullet holes, blood, debris, shell casings "
                            "and broken glass stay instead of fading away."});
  settings.push_back({"Interval select",
                      "fear_all_intervals",
                      {{"false", "Reached"}, {"true", "All intervals"}},
                      true,
                      "Set which intervals can be chosen from Campaign > Interval."});
  settings.push_back({"Checkpoint saves",
                      "fear_fast_checkpoints",
                      {{"true", "Fast"}, {"false", "Normal"}},
                      false,
                      "Set checkpoint save speed. Fast skips the video memory cleanup at each "
                      "checkpoint."});
  settings.push_back({"High frame rate fixes",
                      "fear_high_fps_fixes",
                      {{"true", "On"}, {"false", "Off"}},
                      true,
                      "Toggle fixes for play above 30 fps: sliding on slopes, jump kicks, "
                      "landing speed, water and the SlowMo meter."});
  settings.push_back({"Version display",
                      "fear_version_badge",
                      {{"launch", "At startup"}, {"always", "Always"}, {"off", "Off"}},
                      false,
                      "Set when the build version is shown in the corner of the screen."});
  settings.push_back({"Animation timing",
                      "fear_anim_exact_time",
                      {{"true", "Exact"}, {"false", "Normal"}},
                      true,
                      "Set animation timing. Exact keeps effects at the right speed at any "
                      "frame rate."});

  // A value set some other way (a launcher flag, the settings file) that no option has.
  for (Setting& setting : settings) {
    const std::string value = CvarValue(setting.cvar);
    const bool listed = std::any_of(setting.options.begin(), setting.options.end(),
                                    [&](const Option& o) { return SameValue(o.value, value); });
    if (!listed && !value.empty()) setting.options.push_back({value, value + " (custom)"});
  }
  return settings;
}

size_t CurrentOption(const Setting& setting) {
  const std::string value = CvarValue(setting.cvar);
  for (size_t i = 0; i < setting.options.size(); ++i) {
    if (SameValue(setting.options[i].value, value)) return i;
  }
  return 0;
}

std::string TomlString(const std::string& text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out + "\"";
}

// The setting a settings.toml line sets ("name = value"), or empty.
std::string SettingName(std::string_view line) {
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  size_t end = 0;
  while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
    ++end;
  }
  std::string_view rest = line.substr(end);
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
  return end && !rest.empty() && rest.front() == '=' ? std::string(line.substr(0, end)) : std::string();
}

// Writes these settings, keeping the file's lines for any others (the Controls page's while it
// wasn't opened, a line added by hand).
bool WriteSettings(const std::vector<std::pair<std::string, std::string>>& values) {
  std::vector<std::string> kept;
  {
    std::ifstream in(g_path);
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const std::string name = SettingName(line);
      if (name.empty()) continue;  // comments and blanks: the header is written again
      const bool written = std::any_of(values.begin(), values.end(),
                                       [&](const auto& value) { return value.first == name; });
      if (!written) kept.push_back(line);
    }
  }
  std::error_code error;
  fs::create_directories(g_path.parent_path(), error);
  // Written whole to a file beside it, then renamed over it: a crash mid-write leaves the old
  // file, not a truncated one (docs/REVIEW.md).
  fs::path temp = g_path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) {
      REXLOG_ERROR("F.E.A.R. settings: can't write {}", temp.string());
      return false;
    }
    out << "# F.E.A.R. Recompilation Settings, written by the panel in the main menu.\n"
           "# Loaded at startup; a flag on the command line still wins for that run.\n";
    for (const std::string& line : kept) out << line << "\n";
    for (const auto& [name, value] : values) {
      const auto* info = rex::cvar::GetFlagInfo(name);
      const bool text = info && info->type == rex::cvar::FlagType::String;
      out << name << " = " << (text ? TomlString(value) : value) << "\n";
    }
    out.close();
    if (!out) {
      REXLOG_ERROR("F.E.A.R. settings: couldn't finish writing {}", temp.string());
      return false;
    }
  }
  fs::rename(temp, g_path, error);
  if (error) {
    REXLOG_ERROR("F.E.A.R. settings: can't replace {} ({})", g_path.string(), error.message());
    fs::remove(temp, error);
    return false;
  }
  return true;
}

// Starts the game again with this run's arguments, minus the flags for the settings (so the
// saved ones apply), its log renamed, after this run has exited. With display_pacing, also
// without a timer's pacing (tools/launch.ps1 -Timer: fear_vblank_source timer and
// video_mode_refresh_rate), which the frame rate setting doesn't control: it caps the display's
// vblank pacing. Kept, a frame rate changed here came back at the timer's (2026-10-25: 30 again
// after choosing 120).
bool Relaunch(const std::vector<Setting>& settings, bool display_pacing) {
  std::vector<std::string> managed = {"fear_texture_pack"};
  for (const Setting& setting : settings) managed.push_back(setting.cvar);
  if (display_pacing) {
    managed.push_back("fear_vblank_source");
    managed.push_back("video_mode_refresh_rate");
  }
  return fear::process::StartAfterThis(
      fear::process::OwnExecutable(),
      fear::process::WithoutFlags(fear::process::OwnArguments(), managed), fs::current_path());
}

class HostPanel : public fear::panel::Panel {
 protected:
  explicit HostPanel(const fear::settings::Host& host) : Panel(host.drawer), host_(host) {}

  fear::settings::Host host_;
};

class SettingsPanel final : public HostPanel {
 public:
  explicit SettingsPanel(const fear::settings::Host& host)
      : HostPanel(host), settings_(BuildSettings()) {
    for (const Setting& setting : settings_) {
      running_.push_back(CurrentOption(setting));
    }
    chosen_ = running_;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const uint32_t polled = closing_ ? 0 : navigator_.Poll();
    if (controls_open_) {
      DrawControls(polled);
      return;
    }
    // The Controls page's row first when it's offered, then the settings, then the actions. The
    // rows fill two columns (#124), left then right, in that order: Up and Down walk them all,
    // Left and Right change the selected value. The actions sit side by side under them.
    const int first = controls_offered_ ? 1 : 0;
    const int rows = int(settings_.size());
    const int items = first + rows;
    const int left = (items + 1) / 2;
    const int actions = 3;  // Save, Save and restart, Cancel
    const int total = items + actions;
    const uint32_t input = polled;
    if (selected_ >= items) {
      // On the actions: Left and Right move along them, Up goes back to the right column's end.
      if (input & kLeft) selected_ = items + (selected_ - items + actions - 1) % actions;
      if (input & kRight) selected_ = items + (selected_ - items + 1) % actions;
      if (input & kUp) selected_ = items - 1;
      if (input & kDown) selected_ = 0;
      if (input & kAccept) Act(selected_ - items);
    } else {
      if (input & kUp) selected_ = (selected_ + total - 1) % total;
      if (input & kDown) selected_ = (selected_ + 1) % total;
      if (selected_ < first) {
        if (input & (kRight | kAccept)) OpenControls();
      } else if (selected_ < items) {
        if (input & kLeft) Step(selected_ - first, -1);
        if (input & (kRight | kAccept)) Step(selected_ - first, +1);
      }
    }
    if (input & kBack) Act(0);  // leaving saves; Cancel is the way out without

    if (BeginPanel("##fear_settings", 56.0f, fit_scale_)) {
      ImGui::TextUnformatted("RECOMPILATION SETTINGS");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", fear::version::Text());
      ImGui::Separator();
      // One row of the list: the Controls page's, or a setting's.
      auto item = [&](int index) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (index < first) {
          if (Row("Controls", 0, selected_)) OpenControls();
          ImGui::TableNextColumn();
          std::string value = controls_ && controls_->Changed() ? "Changed (not saved)"
                                                                : "Keyboard and mouse";
          if (selected_ == 0) value += "  >";
          ImGui::TextUnformatted(value.c_str());
          return;
        }
        const int i = index - first;
        const Setting& setting = settings_[i];
        if (Row(setting.label.c_str(), index, selected_)) {
          Step(i, ImGui::GetIO().KeyShift ? -1 : +1);
        }
        ImGui::TableNextColumn();
        std::string value = setting.options[chosen_[i]].label;
        if (selected_ == index && setting.options.size() > 1) value = "<  " + value + "  >";
        ImGui::TextUnformatted(value.c_str());
        if (chosen_[i] != running_[i] && !setting.live) {
          ImGui::SameLine();
          ImGui::TextDisabled("(restart)");
        }
      };
      if (ImGui::BeginTable("##columns", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int column = 0; column < 2; ++column) {
          ImGui::TableNextColumn();
          if (ImGui::BeginTable(column ? "##right" : "##left", 2,
                                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.15f);
            for (int index = column ? left : 0; index < (column ? items : left); ++index) {
              item(index);
            }
            ImGui::EndTable();
          }
        }
        ImGui::EndTable();
      }
      ImGui::Separator();
      const char* labels[] = {"Save", "Save and restart", "Cancel"};
      const float width = (ImGui::GetContentRegionAvail().x -
                           ImGui::GetStyle().ItemSpacing.x * float(actions - 1)) /
                          float(actions);
      for (int a = 0; a < actions; ++a) {
        if (a) ImGui::SameLine();
        if (Row(labels[a], items + a, selected_, width)) Act(a);
      }
      ImGui::Separator();
      // The note gets a fixed five lines, so the panel keeps its size from row to row (and
      // covers the menu behind it whatever the note's length).
      ImGui::BeginChild("##note", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 5.0f),
                        ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
      if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
      } else if (selected_ < first) {
        Dim("Set keyboard and mouse configuration options.");
      } else if (selected_ < items) {
        const Setting& setting = settings_[selected_ - first];
        std::string note = setting.note;
        if (rex::cvar::GetFlagSource(setting.cvar) == rex::cvar::Source::kCommandLine) {
          note += " Currently set by a command-line option, which overrides the saved value.";
        }
        if (setting.cvar == "fear_frame_rate_limit" && TimerPaced()) {
          note += " Currently paced by a timer (a launch option). Save and restart to pace to "
                  "the display.";
        }
        Dim(note.c_str());
      } else {
        Dim(selected_ == items + 1 ? "Save changes and restart the game."
            : selected_ == items + 2
                ? "Return to the main menu without saving. Back also saves changes."
                : "Save changes. Settings marked (restart) apply the next time the game "
                  "starts.");
      }
      ImGui::EndChild();
      // Still taller than the screen (a long note, a small display): a smaller font next frame.
      const float limit = ImGui::GetIO().DisplaySize.y * 0.96f;
      const float height = ImGui::GetWindowHeight();
      if (height > limit) {
        fit_scale_ = std::max(0.5f, fit_scale_ * limit / height);
      }
    }
    EndPanel();
  }

 private:
  void Step(int row, int direction) {
    const int count = int(settings_[row].options.size());
    chosen_[row] = size_t((int(chosen_[row]) + direction + count) % count);
    message_.clear();
  }

  bool Save() {
    std::vector<std::pair<std::string, std::string>> values;
    for (size_t i = 0; i < settings_.size(); ++i) {
      values.emplace_back(settings_[i].cvar, settings_[i].options[chosen_[i]].value);
    }
    const std::string pack = CvarValue("fear_texture_pack");
    if (!pack.empty()) values.emplace_back("fear_texture_pack", pack);
    // The Controls page's, when it was opened (its settings are left as they are otherwise).
    const auto controls = controls_ ? controls_->Values()
                                    : std::vector<std::pair<std::string, std::string>>();
    values.insert(values.end(), controls.begin(), controls.end());
    if (!WriteSettings(values)) {
      message_ = "Unable to save settings to " + g_path.string() + ".";
      return false;
    }
    REXLOG_INFO("F.E.A.R. settings: saved to {}", g_path.string());
    // The keys and the mouse now: the game reads them live.
    if (!controls.empty()) fear::mkb::SetFlags(controls);
    // Window mode and animation timing now, on the UI thread after this frame.
    std::vector<std::pair<std::string, std::string>> live;
    for (size_t i = 0; i < settings_.size(); ++i) {
      if (settings_[i].live && chosen_[i] != running_[i]) live.push_back(values[i]);
    }
    if (!live.empty() && host_.later) {
      host_.later([live] {
        for (const auto& [name, value] : live) rex::cvar::SetFlagByName(name, value);
      });
    }
    return true;
  }

  void Act(int action) {
    if (closing_) return;
    // A new Input restarts the game: the game picks its button pictures when it starts.
    if (action == 0 && InputChanged()) action = 1;
    if (action == 0 && Save()) {
      Finish();
    } else if (action == 1 && Save()) {
      if (Relaunch(settings_, FrameRateChanged())) {
        Finish();
        if (host_.later && host_.quit) host_.later(host_.quit);
      } else {
        message_ = "Settings saved. The game could not restart: quit and start it again.";
      }
    } else if (action == 2) {
      Finish();
    }
  }

  void Finish() {
    closing_ = true;
    Close();
  }

  bool FrameRateChanged() const {
    for (size_t i = 0; i < settings_.size(); ++i) {
      if (settings_[i].cvar == "fear_frame_rate_limit") return chosen_[i] != running_[i];
    }
    return false;
  }

  bool InputChanged() const {
    for (size_t i = 0; i < settings_.size(); ++i) {
      if (settings_[i].cvar == "fear_mkb") return chosen_[i] != running_[i];
    }
    return false;
  }

  static bool TimerPaced() { return CvarValue("fear_vblank_source") == "timer"; }

  void OpenControls() {
    if (!controls_) controls_ = std::make_unique<fear::controls::Page>();
    controls_open_ = true;
    message_.clear();
  }

  // The Controls page (fear_controls.h) in this panel, a little wider for its two key columns.
  void DrawControls(uint32_t input) {
    using Result = fear::controls::Page::Result;
    Result result = Result::kStay;
    if (BeginPanel("##fear_settings", 34.0f)) result = controls_->Draw(input);
    EndPanel();
    if (result == Result::kBack) {
      controls_open_ = false;
      selected_ = 0;
    } else if (result == Result::kSave) {
      Act(0);
      if (!closing_) controls_open_ = false;  // couldn't save: the message is on the main page
    }
  }

  std::vector<Setting> settings_;
  std::vector<size_t> running_;  // the option in effect, by row
  std::vector<size_t> chosen_;
  std::string message_;
  bool closing_ = false;
  const bool controls_offered_ = fear::controls::Available();
  std::unique_ptr<fear::controls::Page> controls_;  // made when first opened
  bool controls_open_ = false;
  float fit_scale_ = 1.0f;  // the font's, shrunk when the panel came out taller than the screen
};

class QuitPanel final : public HostPanel {
 public:
  explicit QuitPanel(const fear::settings::Host& host) : HostPanel(host) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const uint32_t input = closing_ ? 0 : navigator_.Poll();
    if (input & (kUp | kDown | kLeft | kRight)) selected_ = 1 - selected_;
    if (input & kAccept) Act(selected_);
    if (input & kBack) Act(1);
    if (BeginPanel("##fear_quit", 16.0f)) {
      ImGui::TextUnformatted("QUIT TO DESKTOP?");
      ImGui::Separator();
      if (Row("Quit", 0, selected_)) Act(0);
      if (Row("Cancel", 1, selected_)) Act(1);
    }
    EndPanel();
  }

 private:
  void Act(int action) {
    if (closing_) return;
    closing_ = true;
    if (action == 0 && host_.later && host_.quit) host_.later(host_.quit);
    Close();
  }

  bool closing_ = false;
};

// A Steam Deck: Steam sets SteamDeck=1 for its games, and the board is Valve's.
bool OnSteamDeck() {
#if defined(_WIN32)
  return false;
#else
  if (const char* deck = std::getenv("SteamDeck"); deck && deck[0] == '1') return true;
  std::ifstream vendor("/sys/devices/virtual/dmi/id/board_vendor");
  std::string name;
  return std::getline(vendor, name) && name.starts_with("Valve");
#endif
}

// First start (#117): keyboard and mouse or a controller, saved as Recompilation Settings' Input
// row (fear_mkb), then `then` (the collection menu, or the game).
class InputPanel final : public HostPanel {
 public:
  InputPanel(const fear::settings::Host& host, std::function<void()> then)
      : HostPanel(host), then_(std::move(then)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    const uint32_t input = closing_ ? 0 : navigator_.Poll();
    if (input & (kUp | kDown | kLeft | kRight)) selected_ = 1 - selected_;
    if (input & kAccept) Act(selected_);
    if (BeginPanel("##fear_input", 24.0f)) {
      ImGui::TextUnformatted("SELECT CONTROLS");
      ImGui::Separator();
      if (Row("Keyboard and mouse", 0, selected_)) Act(0);
      if (Row("Controller", 1, selected_)) Act(1);
      ImGui::Separator();
      Dim("Both work in the menus. Change this later in Recompilation Settings.");
    }
    EndPanel();
  }

 private:
  void Act(int choice) {
    if (closing_) return;
    closing_ = true;
    const std::string value = choice == 0 ? "true" : "false";
    if (!WriteSettings({{"fear_mkb", value}})) {
      REXLOG_ERROR("F.E.A.R. settings: couldn't save the input choice");
    }
    rex::cvar::SetFlagByName("fear_mkb", value);
    REXLOG_INFO("F.E.A.R. settings: first start, {} chosen",
                choice == 0 ? "the keyboard and mouse" : "a controller");
    Close();
    if (host_.later && then_) host_.later(then_);
  }

  std::function<void()> then_;
  bool closing_ = false;
};

}  // namespace

namespace fear::settings {

bool AskInput(const Host& host, std::function<void()> then) {
  if (fear::title::kFiles || !host.drawer || fear::panel::AnyOpen()) return false;
  // The Steam Deck starts with its controls, unasked (Recompilation Settings > Input still
  // offers the keyboard and mouse); a Linux desktop is asked, as Windows is.
  if (OnSteamDeck()) return false;
  // Asked once: until settings.toml (or the command line) says which.
  if (rex::cvar::GetFlagSource("fear_mkb") != rex::cvar::Source::kDefault) return false;
  new InputPanel(host, std::move(then));  // deletes itself when closed
  return true;
}

void Load(const std::filesystem::path& user_data_root) {
  fear::process::WaitForPreviousRun();
  g_path = user_data_root / "settings.toml";
  // (The D3D12 renderer's resolution, fear_d3d12_resolution_scale, defaults to the display's.)
  rex::cvar::LoadConfig(g_path);
}

void OpenSettings(const Host& host) {
  if (fear::panel::AnyOpen() || !host.drawer) return;
  new SettingsPanel(host);  // deletes itself when closed (ImGuiDialog)
}

void OpenQuit(const Host& host) {
  if (fear::panel::AnyOpen() || !host.drawer) return;
  new QuitPanel(host);
}

}  // namespace fear::settings
