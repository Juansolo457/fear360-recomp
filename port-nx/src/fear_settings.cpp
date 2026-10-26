// F.E.A.R. 360 - the PC Settings panel and the quit confirmation. See fear_settings.h.

#include "fear_settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>

#include "display_vblank.h"
#include "fear_panel.h"
#include "fear_process.h"
#include "fear_title.h"

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
  settings.push_back({"Display",
                      "fullscreen",
                      {{"true", "Fullscreen"}, {"false", "Window"}},
                      true,
                      "Alt+Enter switches too."});
  settings.push_back({"Frame rate",
                      "fear_frame_rate_limit",
                      {{"30", FrameRateLabel(30) + " (the 360's)"},
                       {"60", FrameRateLabel(60)},
                       {"90", FrameRateLabel(90)},
                       {"120", FrameRateLabel(120)}},
                      false,
                      "Each frame is shown for a whole number of your display's refreshes, the "
                      "fewest that keep it at or under the limit."});
  settings.push_back({"Anti-aliasing",
                      "fear_d3d12_msaa",
                      {{"true", "The game's MSAA (as shipped)"}, {"false", "Off"}},
                      false,
                      "The game's own 2x or 4x multisampling. Off costs the GPU much less: the "
                      "Steam Deck runs faster without it."});
  settings.push_back({"Texture filtering",
                      "fear_d3d12_anisotropy",
                      {{"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
                      false,
                      "Anisotropic filtering: sharper floors and walls at an angle."});
  settings.push_back({"Texture sharpness",
                      "fear_mip_bias",
                      {{"0", "As shipped"}, {"-0.5", "Sharper"}, {"-1", "Sharpest"}},
                      false,
                      "Sharper distant textures, like EchoPatch's mipmap bias on PC. The "
                      "sharpest setting can shimmer on fine patterns."});
  settings.push_back({"Right stick at its edge",
                      "fear_look_ramp",
                      {{"off", "No boost"},
                       {"console", "As on the 360"},
                       {"nodelay", "Boost at once, ramping"},
                       {"instant", "Full boost at once"}},
                      false,
                      "On the 360, holding the stick at its edge speeds turning up after 0.3 s, "
                      "ramping to 360 degrees a second; no boost keeps turning in step with the "
                      "stick."});
  settings.push_back({"Aim assist",
                      "fear_aim_softlock",
                      {{"true", "On"}, {"false", "Off"}},
                      false,
                      "Turning slows when your aim crosses an enemy (the 360's soft lock)."});
  settings.push_back({"Field of view",
                      "fear_fov",
                      {{"65", "65"},
                       {"70", "70 (the 360's and the PC's)"},
                       {"75", "75"},
                       {"80", "80"},
                       {"85", "85"},
                       {"90", "90"},
                       {"95", "95"}},
                      false,
                      "Vertical, in degrees. Above 70 the gun moves forward to keep its place on "
                      "screen, as EchoPatch does on PC."});
  settings.push_back({"Bloom",
                      "fear_bloom_intensity",
                      {{"0", "Off (the PC's)"}, {"-1", "On (the 360's)"}},
                      false,
                      "The 360 version adds a screen bloom the PC version doesn't have. Near "
                      "bright lights it spreads a grey haze over the scene."});
  settings.push_back({"Enemy AI",
                      "fear_ai",
                      {{"sharp", "Sharp"}, {"pc", "As on PC"}, {"360", "As on 360"}},
                      false,
                      "Sharp: enemies fight like Very Hard's at every difficulty (aim, grenades, "
                      "dodging); how hard they hit still follows the difficulty. As on PC: the "
                      "PC's difficulties. As on 360: its easier Normal."});
  settings.push_back({"Blood and armour marks",
                      "fear_body_marks",
                      {{"true", "On (as on PC)"}, {"false", "Off (as on 360)"}},
                      false,
                      "Bullet hits leave blood and armour marks on enemies. The 360 version "
                      "starts with them off."});
  // The database options (look_tuning.h): F.E.A.R. only until F.E.A.R. Files' database, inside its
  // catalogue, can be changed too.
  if (!fear::title::kFiles) {
    settings.push_back({"Weapon slots",
                        "fear_weapon_slots",
                        {{"3", "3 (as shipped)"}, {"4", "4"}, {"5", "5"}, {"10", "10 (every weapon)"}},
                        false,
                        "How many weapons you can carry at once."});
    settings.push_back({"Flashlight",
                        "fear_infinite_flashlight",
                        {{"false", "Battery (as shipped)"}, {"true", "Never runs out"}},
                        false,
                        "Never runs out also removes the battery bar from the HUD."});
    settings.push_back({"Intro videos",
                        "fear_intro_videos",
                        {{"all", "All (as shipped)"},
                         {"monolith", "Monolith logo only"},
                         {"none", "Skip"}},
                        false,
                        "The rating card and the Sierra, Day 1 and Monolith logos at startup."});
  }
  settings.push_back({"Cutscene black bars",
                      "fear_letterbox",
                      {{"true", "On (as shipped)"}, {"false", "Off"}},
                      false,
                      "The letterbox bars at the top and bottom of the screen in cutscenes."});
  settings.push_back({"Model detail",
                      "fear_full_detail_models",
                      {{"false", "As shipped"}, {"true", "Full at any distance"}},
                      false,
                      "Full: characters and objects keep their most detailed models far away, "
                      "like EchoPatch's no model LOD bias. Costs some GPU."});
  settings.push_back({"Bodies and debris",
                      "fear_bodies_stay",
                      {{"false", "Fade (as shipped)"}, {"true", "Stay"}},
                      false,
                      fear::title::kFiles
                          ? "Stay: bodies no longer fade away once there are more than 10, and "
                            "bullet holes, blood splats, debris and shell casings stay."
                          : "Stay: bodies no longer fade away once there are more than 10, and "
                            "bullet holes, blood splats, debris, shell casings and broken glass "
                            "stay. A long fight leaves a lot behind."});
  settings.push_back({"Checkpoint saves",
                      "fear_fast_checkpoints",
                      {{"true", "Fast"}, {"false", "As shipped"}},
                      false,
                      "Fast skips the video memory clean-up the game does at each checkpoint, as "
                      "EchoPatch's faster saves do on PC."});
  settings.push_back({"High frame rate fixes",
                      "fear_high_fps_fixes",
                      {{"true", "On (EchoPatch's)"}, {"false", "Off (as shipped)"}},
                      true,
                      "Above 30 fps the game slides on slopes, misses jump kicks, loses speed on "
                      "landing and leaving water, and its water and slow-mo meter misbehave. On "
                      "fixes them as EchoPatch does on PC."});
  settings.push_back({"Animation timing",
                      "fear_anim_exact_time",
                      {{"true", "Exact"}, {"false", "As shipped"}},
                      true,
                      "The engine drops a fraction of a millisecond from every animation step; "
                      "exact keeps it, so effects play at the right speed at any frame rate."});

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

bool WriteSettings(const std::vector<std::pair<std::string, std::string>>& values) {
  std::error_code error;
  fs::create_directories(g_path.parent_path(), error);
  std::ofstream out(g_path, std::ios::trunc);
  if (!out) {
    REXLOG_ERROR("F.E.A.R. settings: can't write {}", g_path.string());
    return false;
  }
  out << "# F.E.A.R. PC Settings, written by the panel in the main menu.\n"
         "# Loaded at startup; a flag on the command line still wins for that run.\n";
  for (const auto& [name, value] : values) {
    const auto* info = rex::cvar::GetFlagInfo(name);
    const bool text = info && info->type == rex::cvar::FlagType::String;
    out << name << " = " << (text ? TomlString(value) : value) << "\n";
  }
  return bool(out);
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
    const int rows = int(settings_.size());
    const int actions = 3;  // Save, Save and restart, Cancel
    const uint32_t input = closing_ ? 0 : navigator_.Poll();
    if (input & kUp) selected_ = (selected_ + rows + actions - 1) % (rows + actions);
    if (input & kDown) selected_ = (selected_ + 1) % (rows + actions);
    if (selected_ < rows) {
      if (input & kLeft) Step(selected_, -1);
      if (input & (kRight | kAccept)) Step(selected_, +1);
    } else if (input & kAccept) {
      Act(selected_ - rows);
    }
    if (input & kBack) Act(2);

    if (BeginPanel("##fear_settings", 30.0f)) {
      ImGui::TextUnformatted("PC SETTINGS");
      ImGui::Separator();
      if (ImGui::BeginTable("##settings", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        for (int i = 0; i < rows; ++i) {
          const Setting& setting = settings_[i];
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          if (Row(setting.label.c_str(), i, selected_)) {
            Step(i, ImGui::GetIO().KeyShift ? -1 : +1);
          }
          ImGui::TableNextColumn();
          std::string value = setting.options[chosen_[i]].label;
          if (selected_ == i && setting.options.size() > 1) value = "<  " + value + "  >";
          ImGui::TextUnformatted(value.c_str());
          if (chosen_[i] != running_[i] && !setting.live) {
            ImGui::SameLine();
            ImGui::TextDisabled("(on restart)");
          }
        }
        ImGui::EndTable();
      }
      ImGui::Separator();
      const char* labels[] = {"Save", "Save and restart", "Cancel"};
      for (int a = 0; a < actions; ++a) {
        if (Row(labels[a], rows + a, selected_)) Act(a);
      }
      ImGui::Separator();
      if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
      } else if (selected_ < rows) {
        const Setting& setting = settings_[selected_];
        std::string note = setting.note;
        if (rex::cvar::GetFlagSource(setting.cvar) == rex::cvar::Source::kCommandLine) {
          note += " This run: set by a command-line flag, which wins over the saved value; "
                  "Save and restart leaves it out.";
        }
        if (setting.cvar == "fear_frame_rate_limit" && TimerPaced()) {
          note += " This run is paced by a timer (a launch flag), not by this setting. Save and "
                  "restart with another frame rate goes back to the display's pacing.";
        }
        Dim(note.c_str());
      } else {
        Dim(selected_ == rows + 1
                ? "Saves, closes the game and starts it again with these settings."
                : "Changes marked (on restart) apply the next time the game starts. "
                  "Saved in your user data folder (settings.toml).");
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
    if (!WriteSettings(values)) {
      message_ = "Couldn't write " + g_path.string() + ".";
      return false;
    }
    REXLOG_INFO("F.E.A.R. settings: saved to {}", g_path.string());
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
    if (action == 0 && Save()) {
      Finish();
    } else if (action == 1 && Save()) {
      if (Relaunch(settings_, FrameRateChanged())) {
        Finish();
        if (host_.later && host_.quit) host_.later(host_.quit);
      } else {
        message_ = "Saved, but the game couldn't restart itself: quit and start it again.";
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

  static bool TimerPaced() { return CvarValue("fear_vblank_source") == "timer"; }

  std::vector<Setting> settings_;
  std::vector<size_t> running_;  // the option in effect, by row
  std::vector<size_t> chosen_;
  std::string message_;
  bool closing_ = false;
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

}  // namespace

namespace fear::settings {

void Load(const std::filesystem::path& user_data_root) {
  fear::process::WaitForPreviousRun();
  g_path = user_data_root / "settings.toml";
  // (fear-nx's renderer started a first run at the display's multiple of 720p; the D3D12
  // renderer has no resolution scale yet: docs/NATIVE_RENDERER.md.)
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
