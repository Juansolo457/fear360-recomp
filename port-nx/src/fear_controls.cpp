// F.E.A.R. 360 - PC Settings' Controls page. See fear_controls.h.

#include "fear_controls.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <string_view>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>

#include "fear_panel.h"

namespace fear::controls {

using namespace fear::panel;

namespace {

// The mouse rows, then one row per action, then the page's buttons.
enum MouseRow { kSensitivity, kVertical, kInvert, kSmoothing, kMouseRows };
enum Button { kResetButton, kSaveButton, kBackButton, kButtons };
constexpr int kSlots = 2;

constexpr double kMinSensitivity = 0.05, kMaxSensitivity = 10.0;
constexpr double kMinVertical = 0.25, kMaxVertical = 2.0;

struct Smoothing {
  double ms;
  const char* label;
};
constexpr Smoothing kSmoothings[] = {{0, "Off (raw)"}, {8, "Low"}, {16, "Medium"}, {32, "High"}};

double Round(double value, double step) { return std::round(value / step) * step; }

std::string Number(double value) { return fmt::format("{:g}", Round(value, 0.001)); }

bool SameKey(std::string_view a, std::string_view b) {
  const std::string x = fear::mkb::KeyLabel(a), y = fear::mkb::KeyLabel(b);
  return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](char p, char q) {
           return std::tolower(static_cast<unsigned char>(p)) ==
                  std::tolower(static_cast<unsigned char>(q));
         });
}

std::vector<std::string> Slots(std::string_view text) {
  std::vector<std::string> keys = fear::mkb::SplitKeys(text);
  if (keys.size() < kSlots) keys.resize(kSlots);
  return keys;
}

std::string Join(const std::vector<std::string>& keys) {
  std::string out;
  for (const std::string& key : keys) {
    if (key.empty()) continue;
    if (!out.empty()) out += ',';
    out += key;
  }
  return out;
}

std::string DefaultOf(const char* cvar) {
  const auto* info = rex::cvar::GetFlagInfo(cvar);
  return info ? info->default_value : std::string();
}

double DefaultNumber(const char* cvar, double fallback) {
  try {
    return std::stod(DefaultOf(cvar));
  } catch (...) {
    return fallback;
  }
}

// Any key or mouse button down, as ImGui knows them.
bool AnyHeld() {
  if (ImGui::IsAnyMouseDown()) return true;
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
    if (ImGui::IsKeyDown(ImGuiKey(key))) return true;
  }
  return false;
}

// Selecting a row with a moving mouse, as Row() does, for the cells and sliders in it.
bool HoveredByMouse() {
  const ImGuiIO& io = ImGui::GetIO();
  return ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
}

void PushSliderStyle() {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.22f, 0.22f, 0.24f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.26f, 0.28f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.75f, 0.14f, 0.10f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.90f, 0.20f, 0.14f, 1.0f));
}

void PopSliderStyle() { ImGui::PopStyleColor(5); }

}  // namespace

bool Available() { return fear::mkb::Enabled(); }

Page::Page() : actions_(fear::mkb::BindableActions()) {
  state_.sensitivity = rex::cvar::Query<double>("fear_mouse_sensitivity");
  state_.vertical = rex::cvar::Query<double>("fear_mouse_vertical");
  state_.invert = rex::cvar::Query<bool>("fear_mouse_invert");
  state_.smoothing = rex::cvar::Query<double>("fear_mouse_smoothing");
  for (const auto& action : actions_) {
    state_.keys.push_back(Slots(rex::cvar::GetFlagByName(action.setting)));
  }
  saved_ = state_;
}

Page::~Page() {
  if (capturing_) fear::mkb::StopCapture();
}

int Page::Rows() const { return kMouseRows + int(actions_.size()) + kButtons; }

bool Page::Changed() const { return !(state_ == saved_); }

std::vector<std::pair<std::string, std::string>> Page::Values() const {
  std::vector<std::pair<std::string, std::string>> values = {
      {"fear_mouse_sensitivity", Number(state_.sensitivity)},
      {"fear_mouse_vertical", Number(state_.vertical)},
      {"fear_mouse_invert", state_.invert ? "true" : "false"},
      {"fear_mouse_smoothing", Number(state_.smoothing)},
  };
  for (size_t i = 0; i < actions_.size(); ++i) {
    values.emplace_back(actions_[i].setting, Join(state_.keys[i]));
  }
  return values;
}

void Page::Step(int row, int direction) {
  message_.clear();
  switch (row) {
    case kSensitivity:
      state_.sensitivity = std::clamp(Round(state_.sensitivity + 0.05 * direction, 0.01),
                                      kMinSensitivity, kMaxSensitivity);
      break;
    case kVertical:
      state_.vertical = std::clamp(Round(state_.vertical + 0.05 * direction, 0.01), kMinVertical,
                                   kMaxVertical);
      break;
    case kInvert:
      state_.invert = !state_.invert;
      break;
    case kSmoothing: {
      // The next listed value up or down from the setting, wherever it is, wrapping around.
      const int count = int(std::size(kSmoothings));
      int index = direction > 0 ? 0 : count - 1;
      if (direction > 0) {
        for (int i = count - 1; i >= 0; --i) {
          if (kSmoothings[i].ms > state_.smoothing + 1e-6) index = i;
        }
      } else {
        for (int i = 0; i < count; ++i) {
          if (kSmoothings[i].ms < state_.smoothing - 1e-6) index = i;
        }
      }
      state_.smoothing = kSmoothings[index].ms;
      break;
    }
    default:
      break;
  }
}

void Page::StartCapture() {
  capturing_ = true;
  message_.clear();
  fear::mkb::StartCapture();
}

void Page::StopCapture() {
  capturing_ = false;
  fear::mkb::StopCapture();
  // The key or button that ended the wait is still down: nothing acts until it's let go.
  quiet_until_ = ImGui::GetTime() + 0.15;
}

void Page::Bind(size_t action, int slot, const std::string& name) {
  std::string moved_from;
  for (size_t a = 0; a < state_.keys.size(); ++a) {
    for (size_t s = 0; s < state_.keys[a].size(); ++s) {
      if (a == action && int(s) == slot) continue;
      if (!state_.keys[a][s].empty() && SameKey(state_.keys[a][s], name)) {
        state_.keys[a][s].clear();
        if (a != action) moved_from = actions_[a].label;
      }
    }
  }
  state_.keys[action][slot] = name;
  if (!moved_from.empty()) {
    message_ = fmt::format("{} moved from {} to {}.", fear::mkb::KeyLabel(name), moved_from,
                           actions_[action].label);
  }
}

void Page::Clear(size_t action, int slot) {
  message_.clear();
  state_.keys[action][slot].clear();
}

void Page::ResetToDefaults() {
  state_.sensitivity = DefaultNumber("fear_mouse_sensitivity", 1.0);
  state_.vertical = DefaultNumber("fear_mouse_vertical", 1.0);
  const std::string invert = DefaultOf("fear_mouse_invert");
  state_.invert = invert == "true" || invert == "1";
  state_.smoothing = DefaultNumber("fear_mouse_smoothing", 0.0);
  for (size_t i = 0; i < actions_.size(); ++i) {
    state_.keys[i] = Slots(DefaultOf(actions_[i].setting));
  }
  message_ = "Back to the PC's default keys and mouse settings (not saved yet).";
}

std::string Page::Note() const {
  if (!message_.empty()) return message_;
  const int actions = int(actions_.size());
  if (selected_ == kSensitivity) {
    // 0.022 degrees per count at 1 (fear_mkb.h); a full turn at 800 counts an inch.
    const double cm = 360.0 / (0.022 * state_.sensitivity) / 800.0 * 2.54;
    return fmt::format(
        "Mouse look speed. 1.00 turns 0.022 degrees per mouse count, the scale of most PC "
        "shooters, so a sensitivity from another game carries over. At 800 DPI a full turn takes "
        "{:.0f} cm of mouse movement. Drag the slider, or Left and Right.",
        cm);
  }
  if (selected_ == kVertical) {
    return "Up and down speed, as a share of the sensitivity: 100% is the same as left and right.";
  }
  if (selected_ == kInvert) return "On: moving the mouse forward looks down.";
  if (selected_ == kSmoothing) {
    return "Spreads each mouse movement over a few milliseconds, for a steadier view at a cost of "
           "a little lag. Off is the raw movement. The same at any frame rate.";
  }
  if (selected_ < kMouseRows + actions) {
    const auto& action = actions_[selected_ - kMouseRows];
    std::string note = action.note ? std::string(action.note) + " " : std::string();
    return note +
           "Enter or click: choose its key, then press a key, mouse button or the wheel. Left and "
           "Right: its first or second key. Delete: clear it.";
  }
  switch (selected_ - kMouseRows - actions) {
    case kResetButton: return "The PC's default keys and mouse settings.";
    case kSaveButton: return "Saves all of PC Settings, these controls included, and closes.";
    default: return "Back to PC Settings. Changes here are kept until you save or cancel there.";
  }
}

Page::Result Page::Draw(uint32_t input) {
  const int actions = int(actions_.size());
  const int rows = Rows();
  const int first_button = kMouseRows + actions;
  Result result = Result::kStay;

  if (capturing_) {
    const std::string name = fear::mkb::TakeCapture();
    if (!name.empty()) {
      StopCapture();
      if (name != "Escape") Bind(size_t(selected_ - kMouseRows), slot_, name);
    } else if (input & kBack) {
      StopCapture();  // the controller's B (Escape is the key above)
    }
    input = 0;
  } else if (ImGui::GetTime() < quiet_until_ || (quiet_until_ > 0.0 && AnyHeld())) {
    input = 0;
  } else {
    quiet_until_ = 0.0;
  }

  if (input & kUp) {
    selected_ = (selected_ + rows - 1) % rows;
    scroll_to_selected_ = true;
    message_.clear();
  }
  if (input & kDown) {
    selected_ = (selected_ + 1) % rows;
    scroll_to_selected_ = true;
    message_.clear();
  }
  const bool on_action = selected_ >= kMouseRows && selected_ < first_button;
  if (on_action) {
    if (input & kLeft) slot_ = 0;
    if (input & kRight) slot_ = 1;
  } else if (selected_ < kMouseRows) {
    if (input & kLeft) Step(selected_, -1);
    if (input & kRight) Step(selected_, +1);
  }
  if (input & kAccept) {
    if (on_action) {
      StartCapture();
    } else if (selected_ == kInvert || selected_ == kSmoothing) {
      Step(selected_, +1);
    } else if (selected_ >= first_button) {
      switch (selected_ - first_button) {
        case kResetButton: ResetToDefaults(); break;
        case kSaveButton: result = Result::kSave; break;
        default: result = Result::kBack; break;
      }
    }
  }
  // Delete and Backspace clear a key here; Backspace, the navigator's back, doesn't go back then
  // (Escape still does).
  if (on_action && !capturing_ && quiet_until_ == 0.0 &&
      (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
       ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
    Clear(size_t(selected_ - kMouseRows), slot_);
    input &= ~uint32_t(kBack);
  }
  if (input & kBack) result = Result::kBack;

  ImGui::TextUnformatted("CONTROLS");
  ImGui::SameLine();
  ImGui::TextDisabled("Keyboard and mouse");
  ImGui::Separator();

  if (capturing_) {
    const auto& action = actions_[selected_ - kMouseRows];
    ImGui::Dummy(ImVec2(0.0f, FontSize() * 2.0f));
    ImGui::TextWrapped("Press a key, a mouse button or the wheel for %s (%s key).", action.label,
                       slot_ == 0 ? "first" : "second");
    ImGui::Dummy(ImVec2(0.0f, FontSize() * 0.5f));
    Dim("Escape cancels. Escape itself always opens the pause menu.");
    ImGui::Dummy(ImVec2(0.0f, FontSize() * 2.0f));
    return result;
  }

  const float list_height = ImGui::GetIO().DisplaySize.y * 0.55f;
  if (ImGui::BeginChild("##controls_list", ImVec2(0.0f, list_height), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoNav)) {
    if (ImGui::BeginTable("##controls", 3, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
      ImGui::TableSetupColumn("##first", ImGuiTableColumnFlags_WidthStretch, 0.9f);
      ImGui::TableSetupColumn("##second", ImGuiTableColumnFlags_WidthStretch, 0.9f);
      auto keep_in_view = [&](int index) {
        if (scroll_to_selected_ && selected_ == index) {
          ImGui::SetScrollHereY(0.5f);
          scroll_to_selected_ = false;
        }
      };

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("MOUSE");

      // Sensitivity and vertical speed: sliders a mouse can drag, Left and Right step them.
      const char* mouse_labels[] = {"Mouse sensitivity", "Up and down speed", "Invert mouse",
                                    "Mouse smoothing"};
      for (int row = 0; row < kMouseRows; ++row) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        Row(mouse_labels[row], row, selected_);
        keep_in_view(row);
        ImGui::TableNextColumn();
        const bool here = selected_ == row;
        if (row == kSensitivity || row == kVertical) {
          ImGui::PushID(row);
          PushSliderStyle();
          ImGui::SetNextItemWidth(-FLT_MIN);
          bool changed;
          if (row == kSensitivity) {
            float value = float(state_.sensitivity);
            changed = ImGui::SliderFloat("##value", &value, float(kMinSensitivity),
                                         float(kMaxSensitivity), "%.2f",
                                         ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            if (changed) state_.sensitivity = Round(value, 0.01);
          } else {
            float percent = float(state_.vertical * 100.0);
            changed = ImGui::SliderFloat("##value", &percent, float(kMinVertical * 100.0),
                                         float(kMaxVertical * 100.0), "%.0f%%",
                                         ImGuiSliderFlags_AlwaysClamp);
            if (changed) state_.vertical = Round(percent, 5.0) / 100.0;
          }
          if (changed) message_.clear();
          if (HoveredByMouse() || ImGui::IsItemActive()) selected_ = row;
          PopSliderStyle();
          ImGui::PopID();
          ImGui::TableNextColumn();
        } else {
          std::string value;
          if (row == kInvert) {
            value = state_.invert ? "On" : "Off";
          } else {
            value = fmt::format("{:g} ms", state_.smoothing);
            for (const Smoothing& s : kSmoothings) {
              if (std::abs(s.ms - state_.smoothing) < 1e-6) value = s.label;
            }
          }
          if (here) value = "<  " + value + "  >";
          ImGui::TextUnformatted(value.c_str());
          ImGui::TableNextColumn();
        }
      }

      // The keys, under the PC's group names.
      const char* group = nullptr;
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
      for (int i = 0; i < actions; ++i) {
        const auto& action = actions_[i];
        const int index = kMouseRows + i;
        if (!group || std::string_view(group) != action.group) {
          group = action.group;
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Dummy(ImVec2(0.0f, FontSize() * 0.2f));
          std::string title = group;
          for (char& c : title) c = char(std::toupper(static_cast<unsigned char>(c)));
          ImGui::TextDisabled("%s", title.c_str());
          if (i == 0) {
            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, FontSize() * 0.2f));
            ImGui::TextDisabled("Key");
            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, FontSize() * 0.2f));
            ImGui::TextDisabled("Second key");
          }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PopStyleColor(3);  // the row's own highlight
        if (Row(action.label, index, selected_)) StartCapture();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        keep_in_view(index);
        for (int s = 0; s < kSlots; ++s) {
          ImGui::TableNextColumn();
          const std::string& key = state_.keys[i][s];
          std::string text = key.empty() ? "-" : fear::mkb::KeyLabel(key);
          if (selected_ == index && slot_ == s) text = "[ " + text + " ]";
          ImGui::PushID(index * kSlots + s + 100000);
          if (ImGui::Selectable(text.c_str(), false)) {
            selected_ = index;
            slot_ = s;
            StartCapture();
          }
          if (HoveredByMouse()) {
            selected_ = index;
            slot_ = s;
          }
          ImGui::PopID();
        }
      }
      ImGui::PopStyleColor(3);
      ImGui::EndTable();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  const char* buttons[] = {"Reset to defaults", "Save", "Back"};
  for (int b = 0; b < kButtons; ++b) {
    if (Row(buttons[b], first_button + b, selected_)) {
      switch (b) {
        case kResetButton: ResetToDefaults(); break;
        case kSaveButton: result = Result::kSave; break;
        default: result = Result::kBack; break;
      }
    }
  }
  ImGui::Separator();
  Dim(Note().c_str());
  return result;
}

}  // namespace fear::controls
