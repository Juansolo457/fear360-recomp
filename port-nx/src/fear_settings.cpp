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
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "display_vblank.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <xinput.h>
#endif

REXCVAR_DEFINE_UINT32(fear_restart_wait_pid, 0, "F.E.A.R.",
                      "Set by PC Settings' Save and restart: wait for this process (the run "
                      "that restarted) to exit before starting")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

namespace fs = std::filesystem;

fs::path g_path;  // <user data>/settings.toml
ImFont* g_font = nullptr;
std::atomic<bool> g_open{false};          // a panel is open
std::atomic<bool> g_wait_release{false};  // one closed: the game waits for the buttons to be let go

// --- Controller ---

#if defined(_WIN32)
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn XInputGet() {
  static const XInputGetStateFn function = [] {
    HMODULE module = LoadLibraryW(L"xinput1_4.dll");
    if (!module) module = LoadLibraryW(L"xinput9_1_0.dll");
    return module ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"))
                  : nullptr;
  }();
  return function;
}
#endif

struct PadState {
  uint16_t buttons = 0;  // XINPUT_GAMEPAD_*
  int16_t lx = 0, ly = 0;
  bool triggers = false;
};

// Every connected controller merged (all of them drive player 1). Asking XInput about an empty
// slot is slow, so those are asked again once a second.
PadState ReadPads() {
  PadState out;
#if defined(_WIN32)
  static std::atomic<uint32_t> connected{0xF};
  static std::atomic<int64_t> next_scan{0};
  const auto get = XInputGet();
  if (!get) return out;
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  const bool scan = now >= next_scan.load();
  if (scan) {
    next_scan = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::seconds(1)).count();
  }
  for (DWORD i = 0; i < 4; ++i) {
    if (!(connected.load() & (1u << i)) && !scan) continue;
    XINPUT_STATE state{};
    if (get(i, &state) != ERROR_SUCCESS) {
      connected &= ~(1u << i);
      continue;
    }
    connected |= 1u << i;
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    out.buttons |= pad.wButtons;
    if (std::abs(pad.sThumbLX) > std::abs(out.lx)) out.lx = pad.sThumbLX;
    if (std::abs(pad.sThumbLY) > std::abs(out.ly)) out.ly = pad.sThumbLY;
    out.triggers |= pad.bLeftTrigger > 30 || pad.bRightTrigger > 30;
  }
#endif
  return out;
}

// The keys the panels answer to; the game maps some of them to buttons.
bool PanelKeysHeld() {
#if defined(_WIN32)
  for (int key : {VK_RETURN, VK_SPACE, VK_ESCAPE, VK_BACK, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}) {
    if (GetAsyncKeyState(key) & 0x8000) return true;
  }
#endif
  return false;
}

enum Input : uint32_t {
  kUp = 1,
  kDown = 2,
  kLeft = 4,
  kRight = 8,
  kAccept = 16,
  kBack = 32,
};
constexpr uint32_t kDirections = kUp | kDown | kLeft | kRight;

// What was pressed since the last frame: controller, keyboard. Held directions repeat. What is
// already held when the panel opens (the press that opened it) counts only once let go.
class Navigator {
 public:
  uint32_t Poll() {
    const double t = ImGui::GetTime();
    if (opened_ < 0) opened_ = t;
    const PadState pad = ReadPads();
    uint32_t now = 0;
#if defined(_WIN32)
    if ((pad.buttons & XINPUT_GAMEPAD_DPAD_UP) || pad.ly > 16000) now |= kUp;
    if ((pad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.ly < -16000) now |= kDown;
    if ((pad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) || pad.lx < -16000) now |= kLeft;
    if ((pad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || pad.lx > 16000) now |= kRight;
    if (pad.buttons & XINPUT_GAMEPAD_A) now |= kAccept;
    if (pad.buttons & XINPUT_GAMEPAD_B) now |= kBack;
#endif
    uint32_t pressed = now & ~held_;
    const uint32_t directions = now & kDirections;
    if (pressed & kDirections) {
      next_repeat_ = t + 0.4;
    } else if (directions && (held_ & directions) == directions && t >= next_repeat_) {
      pressed |= directions;
      next_repeat_ = t + 0.11;
    }
    held_ = now;

    // ImGui gets the key that opened the panel too, queued from before it existed.
    if (t - opened_ > 0.25) {
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) pressed |= kUp;
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) pressed |= kDown;
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) pressed |= kLeft;
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) pressed |= kRight;
      if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
          ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        pressed |= kAccept;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        pressed |= kBack;
      }
    }
    return pressed;
  }

 private:
  uint32_t held_ = ~0u;
  double next_repeat_ = 1e30;
  double opened_ = -1;
};

// --- Look ---

float PanelFontSize(const ImGuiIO& io) {
  return std::max(18.0f, 64.0f * io.DisplaySize.y / 2160.0f);
}

// A centered, untitled panel in the panel font; false if it isn't drawn this frame.
bool BeginPanel(const char* id, float width_em) {
  const ImGuiIO& io = ImGui::GetIO();
  const float size = PanelFontSize(io);
  ImGui::PushFont(g_font, size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(size * 1.0f, size * 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(size * 0.4f, size * 0.15f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 0.94f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.55f, 0.58f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.62f, 0.10f, 0.08f, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.62f, 0.10f, 0.08f, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.75f, 0.14f, 0.10f, 0.95f));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(size * width_em, io.DisplaySize.x * 0.95f), 0.0f),
                           ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  return ImGui::Begin(id, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
}

void EndPanel() {
  ImGui::End();
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(4);
  ImGui::PopFont();
}

void Dim(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.64f, 1.0f));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// A full-width row; true when clicked. Hovering with a moving mouse selects it.
bool Row(const char* label, int index, int& selected) {
  ImGui::PushID(index);
  const bool clicked = ImGui::Selectable(label, selected == index,
                                         ImGuiSelectableFlags_SpanAllColumns |
                                             ImGuiSelectableFlags_AllowOverlap);
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    selected = index;
  }
  if (clicked) selected = index;
  ImGui::PopID();
  return clicked;
}

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
  settings.push_back({"Resolution",
                      "nx_native_resolution_scale",
                      {{"1", "1280 x 720 (the 360's)"},
                       {"2", "2560 x 1440"},
                       {"3", "3840 x 2160 (4K)"},
                       {"4", "5120 x 2880"}},
                      false,
                      "The game draws at this size, then fits it to your screen. Its own MSAA "
                      "still applies on top."});
  settings.push_back({"Display",
                      "fullscreen",
                      {{"true", "Fullscreen"}, {"false", "Window"}},
                      true,
                      "Alt+Enter switches too."});
  settings.push_back({"Frame rate",
                      "fear_frame_rate_limit",
                      {{"30", FrameRateLabel(30) + " (the 360's)"},
                       {"60", FrameRateLabel(60)},
                       {"90", FrameRateLabel(90)}},
                      false,
                      "Each frame is shown for a whole number of your display's refreshes, the "
                      "fewest that keep it at or under the limit. Above about 90 the 360's "
                      "effects break (bullet tracers)."});
  settings.push_back({"Texture filtering",
                      "nx_native_max_anisotropy",
                      {{"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
                      false,
                      "Anisotropic filtering: sharper floors and walls at an angle."});
  const std::string pack = CvarValue("fear_texture_pack");
  if (pack.empty()) {
    settings.push_back({"PC textures",
                        "fear_texture_pack_enabled",
                        {{"true", "Not built"}},
                        false,
                        "Built on your PC from your own copy of the PC game: "
                        "tools/texture_pack.py (docs/TEXTURES.md)."});
  } else {
    settings.push_back({"PC textures",
                        "fear_texture_pack_enabled",
                        {{"true", "On"}, {"false", "Off (the 360's)"}},
                        false,
                        "The PC version's textures, from the pack built from your own copy: " +
                            pack});
  }
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

#if defined(_WIN32)
std::wstring QuoteArgument(const std::wstring& argument) {
  if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
    return argument;
  }
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, L'\\');
  return out + L"\"";
}
#endif

// Starts the game again with this run's arguments, minus the flags for the settings (so the
// saved ones apply), its log renamed, and told to wait until this run has exited.
bool Relaunch(const std::vector<Setting>& settings) {
#if defined(_WIN32)
  std::vector<std::string> managed = {"fear_texture_pack", "fear_restart_wait_pid"};
  for (const Setting& setting : settings) managed.push_back(setting.cvar);

  wchar_t executable[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
  int count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) return false;
  std::wstring command = QuoteArgument(executable);
  for (int i = 1; i < count; ++i) {
    const std::wstring argument = arguments[i];
    if (argument.rfind(L"--", 0) == 0) {
      std::wstring name = argument.substr(2);
      const size_t equals = name.find(L'=');
      const bool inline_value = equals != std::wstring::npos;
      if (inline_value) name.resize(equals);
      const bool negated = name.rfind(L"no-", 0) == 0;
      if (negated) name = name.substr(3);
      const std::string flag(name.begin(), name.end());  // flag names are ASCII
      const auto* info = rex::cvar::GetFlagInfo(flag);
      const bool takes_value = info && info->type != rex::cvar::FlagType::Boolean &&
                               !inline_value && !negated && i + 1 < count;
      if (std::find(managed.begin(), managed.end(), flag) != managed.end()) {
        if (takes_value) ++i;
        continue;
      }
      if (flag == "log_file" && takes_value) {
        fs::path log(arguments[++i]);
        std::wstring stem = log.stem().wstring();
        if (stem.size() < 8 || stem.compare(stem.size() - 8, 8, L"-restart") != 0) {
          stem += L"-restart";
        }
        log.replace_filename(stem + log.extension().wstring());
        command += L" " + argument + L" " + QuoteArgument(log.wstring());
        continue;
      }
    }
    command += L" " + QuoteArgument(argument);
  }
  LocalFree(arguments);
  command += L" --fear_restart_wait_pid " + std::to_wstring(GetCurrentProcessId());

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &process)) {
    REXLOG_ERROR("F.E.A.R. settings: restart failed ({})", GetLastError());
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
#else
  (void)settings;
  return false;
#endif
}

class Panel : public rex::ui::ImGuiDialog {
 protected:
  explicit Panel(const fear::settings::Host& host) : ImGuiDialog(host.drawer), host_(host) {
    g_open = true;
  }
  ~Panel() override {
    g_wait_release = true;
    g_open = false;
  }

  fear::settings::Host host_;
  Navigator navigator_;
  int selected_ = 0;
};

class SettingsPanel final : public Panel {
 public:
  explicit SettingsPanel(const fear::settings::Host& host)
      : Panel(host), settings_(BuildSettings()) {
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
      if (Relaunch(settings_)) {
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

  std::vector<Setting> settings_;
  std::vector<size_t> running_;  // the option in effect, by row
  std::vector<size_t> chosen_;
  std::string message_;
  bool closing_ = false;
};

class QuitPanel final : public Panel {
 public:
  explicit QuitPanel(const fear::settings::Host& host) : Panel(host) {}

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

void WaitForPreviousRun() {
#if defined(_WIN32)
  const uint32_t pid = REXCVAR_GET(fear_restart_wait_pid);
  if (!pid) return;
  if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
    WaitForSingleObject(process, 30000);
    CloseHandle(process);
  }
#endif
}

// First run: draw at the multiple of 720p that covers the display (3 on 4K, 2 on 1080p and
// 1440p).
uint32_t FirstRunScale() {
#if defined(_WIN32)
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsHeight) {
    return std::clamp(uint32_t(std::ceil(mode.dmPelsHeight / 720.0)), 1u, 3u);
  }
#endif
  return 1;
}

}  // namespace

namespace fear::settings {

void Load(const std::filesystem::path& user_data_root) {
  WaitForPreviousRun();
  g_path = user_data_root / "settings.toml";
  std::error_code error;
  if (!fs::exists(g_path, error)) {
    WriteSettings({{"nx_native_resolution_scale", std::to_string(FirstRunScale())}});
  }
  rex::cvar::LoadConfig(g_path);
}

void AddFonts(ImFontAtlas* atlas) {
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0};
  ImFontConfig config;
  config.OversampleH = config.OversampleV = 2;
  for (const char* path : {"C:\\Windows\\Fonts\\bahnschrift.ttf",
                           "C:\\Windows\\Fonts\\segoeui.ttf"}) {
    std::error_code error;
    if (fs::exists(path, error)) {
      g_font = atlas->AddFontFromFileTTF(path, 64.0f, &config, ranges);
      if (g_font) return;
    }
  }
}

bool GameGetsInput() {
  if (g_open) return false;
  if (!g_wait_release) return true;
  const PadState pad = ReadPads();
  if (pad.buttons || pad.triggers || PanelKeysHeld()) return false;
  g_wait_release = false;
  return true;
}

void OpenSettings(const Host& host) {
  if (g_open || !host.drawer) return;
  new SettingsPanel(host);  // deletes itself when closed (ImGuiDialog)
}

void OpenQuit(const Host& host) {
  if (g_open || !host.drawer) return;
  new QuitPanel(host);
}

}  // namespace fear::settings
