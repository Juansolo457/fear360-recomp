// F.E.A.R. 360 - keyboard and mouse, PC style (#59). See fear_mkb.h.

#include "fear_mkb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "fear_title.h"

#if defined(_WIN32)
inline constexpr bool kMkbDefault = true;
#else
inline constexpr bool kMkbDefault = false;  // the Steam Deck: a trackpad can send mouse movement
#endif

REXCVAR_DEFINE_BOOL(fear_mkb, kMkbDefault, "F.E.A.R.",
                    "Keyboard and mouse, PC style (#59): mouse look and the PC's keys in play, the "
                    "keyboard as a pad in the menus");
REXCVAR_DEFINE_DOUBLE(fear_mouse_sensitivity, 1.0, "F.E.A.R.",
                      "Mouse look speed: 1 turns 0.022 degrees per mouse count, the usual PC "
                      "shooter default (at 800 dpi, 35 degrees an inch)");
REXCVAR_DEFINE_BOOL(fear_mouse_invert, false, "F.E.A.R.", "Invert the mouse's up and down");

// The PC's default keys (the 360's own profile defaults). Key names, comma separated: A-Z, 0-9,
// F1-F12, Space, Enter, Escape, Backspace, Tab, Shift, Ctrl, Alt (or LShift, RCtrl, ...), Up,
// Down, Left, Right, Home, End, Insert, Delete, PageUp, PageDown, CapsLock, Grave, Mouse1-Mouse5,
// WheelUp, WheelDown. Empty: none.
REXCVAR_DEFINE_STRING(fear_key_forward, "W", "F.E.A.R./Keys", "Move forward");
REXCVAR_DEFINE_STRING(fear_key_backward, "S", "F.E.A.R./Keys", "Move backward");
REXCVAR_DEFINE_STRING(fear_key_strafe_left, "A", "F.E.A.R./Keys", "Strafe left");
REXCVAR_DEFINE_STRING(fear_key_strafe_right, "D", "F.E.A.R./Keys", "Strafe right");
REXCVAR_DEFINE_STRING(fear_key_jump, "Space", "F.E.A.R./Keys", "Jump");
REXCVAR_DEFINE_STRING(fear_key_crouch, "C", "F.E.A.R./Keys", "Crouch");
REXCVAR_DEFINE_STRING(fear_key_lean_left, "Q", "F.E.A.R./Keys", "Lean left");
REXCVAR_DEFINE_STRING(fear_key_lean_right, "E", "F.E.A.R./Keys", "Lean right");
REXCVAR_DEFINE_STRING(fear_key_fire, "Mouse1", "F.E.A.R./Keys", "Fire");
REXCVAR_DEFINE_STRING(fear_key_melee, "Mouse2", "F.E.A.R./Keys", "Melee (the PC's Alt-Fire)");
REXCVAR_DEFINE_STRING(fear_key_zoom, "Shift,Mouse3", "F.E.A.R./Keys", "Zoom");
REXCVAR_DEFINE_STRING(fear_key_reload, "R", "F.E.A.R./Keys", "Reload");
REXCVAR_DEFINE_STRING(fear_key_use, "F", "F.E.A.R./Keys", "Use (doors, switches, pick up)");
REXCVAR_DEFINE_STRING(fear_key_slow_mo, "Ctrl", "F.E.A.R./Keys", "Slow-mo (reflexes)");
REXCVAR_DEFINE_STRING(fear_key_flashlight, "X", "F.E.A.R./Keys", "Flashlight");
REXCVAR_DEFINE_STRING(fear_key_medkit, "Z", "F.E.A.R./Keys", "Use a medkit");
REXCVAR_DEFINE_STRING(fear_key_throw_grenade, "G", "F.E.A.R./Keys", "Throw a grenade");
REXCVAR_DEFINE_STRING(fear_key_next_grenade, "4", "F.E.A.R./Keys", "Next grenade type");
REXCVAR_DEFINE_STRING(fear_key_next_weapon, "WheelDown", "F.E.A.R./Keys", "Next weapon");
REXCVAR_DEFINE_STRING(fear_key_previous_weapon, "WheelUp", "F.E.A.R./Keys", "Previous weapon");
REXCVAR_DEFINE_STRING(fear_key_last_weapon, "", "F.E.A.R./Keys", "Last weapon");
REXCVAR_DEFINE_STRING(fear_key_weapon_1, "1", "F.E.A.R./Keys", "Weapon slot 1");
REXCVAR_DEFINE_STRING(fear_key_weapon_2, "2", "F.E.A.R./Keys", "Weapon slot 2");
REXCVAR_DEFINE_STRING(fear_key_weapon_3, "3", "F.E.A.R./Keys", "Weapon slot 3");
REXCVAR_DEFINE_STRING(fear_key_objectives, "M", "F.E.A.R./Keys", "Objectives");
REXCVAR_DEFINE_STRING(fear_key_status, "Tab", "F.E.A.R./Keys", "Status");
REXCVAR_DEFINE_STRING(fear_key_menu, "Escape", "F.E.A.R./Keys", "Pause menu");

namespace {

using rex::ui::VirtualKey;

// The game's command numbers (fear_mkb.h).
enum Command : uint32_t {
  kForward = 0x00,
  kBackward = 0x01,
  kStrafeLeft = 0x03,
  kStrafeRight = 0x04,
  kPitch = 0x0B,
  kYaw = 0x0C,
  kDuck = 0x0E,
  kJump = 0x0F,
  kFire = 0x11,
  kLeanLeft = 0x14,
  kLeanRight = 0x15,
  kMedkit = 0x46,
  kZoom = 0x47,
  kNextGrenade = 0x49,
  kPreviousWeapon = 0x4C,
  kNextWeapon = 0x4D,
  kObjectives = 0x4E,
  kStatus = 0x4F,
  kThrowGrenade = 0x51,
  kActivate = 0x57,
  kReload = 0x58,
  kWeapon1 = 0x5B,
  kWeapon2 = 0x5C,
  kWeapon3 = 0x5D,
  kLastWeapon = 0x67,
  kSlowMo = 0x6A,
  kFlashlight = 0x72,
  kMenu = 0x79,
  kMelee = 0x7A,
};

// CBindMgr: the frame's command states (a byte vector's begin and end) and the enabled flag.
constexpr uint32_t kStatesOffset = 0x14;
constexpr uint32_t kEnabledOffset = 0x40;

// Codes past the 256 virtual keys for the wheel's two directions.
constexpr uint16_t kWheelUp = 0x100;
constexpr uint16_t kWheelDown = 0x101;

constexpr double kRadiansPerCount = 0.022 * std::numbers::pi / 180.0;

struct Action {
  std::string& (*keys)();  // the fear_key_* setting
  const char* setting;
  uint32_t command;
  std::string parsed_from;  // the setting's text the codes were read from
  std::vector<uint16_t> codes;
};

#define FEAR_MKB_ACTION(name, command) \
  Action { &FLAGS_fear_key_##name##_storage_, "fear_key_" #name, command, {}, {} }

std::vector<Action>& Actions() {
  static std::vector<Action> actions = {
      FEAR_MKB_ACTION(forward, kForward),
      FEAR_MKB_ACTION(backward, kBackward),
      FEAR_MKB_ACTION(strafe_left, kStrafeLeft),
      FEAR_MKB_ACTION(strafe_right, kStrafeRight),
      FEAR_MKB_ACTION(jump, kJump),
      FEAR_MKB_ACTION(crouch, kDuck),
      FEAR_MKB_ACTION(lean_left, kLeanLeft),
      FEAR_MKB_ACTION(lean_right, kLeanRight),
      FEAR_MKB_ACTION(fire, kFire),
      FEAR_MKB_ACTION(melee, kMelee),
      FEAR_MKB_ACTION(zoom, kZoom),
      FEAR_MKB_ACTION(reload, kReload),
      FEAR_MKB_ACTION(use, kActivate),
      FEAR_MKB_ACTION(slow_mo, kSlowMo),
      FEAR_MKB_ACTION(flashlight, kFlashlight),
      FEAR_MKB_ACTION(medkit, kMedkit),
      FEAR_MKB_ACTION(throw_grenade, kThrowGrenade),
      FEAR_MKB_ACTION(next_grenade, kNextGrenade),
      FEAR_MKB_ACTION(next_weapon, kNextWeapon),
      FEAR_MKB_ACTION(previous_weapon, kPreviousWeapon),
      FEAR_MKB_ACTION(last_weapon, kLastWeapon),
      FEAR_MKB_ACTION(weapon_1, kWeapon1),
      FEAR_MKB_ACTION(weapon_2, kWeapon2),
      FEAR_MKB_ACTION(weapon_3, kWeapon3),
      FEAR_MKB_ACTION(objectives, kObjectives),
      FEAR_MKB_ACTION(status, kStatus),
      FEAR_MKB_ACTION(menu, kMenu),
  };
  return actions;
}

#undef FEAR_MKB_ACTION

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// The codes one key name stands for (a side-neutral name stands for both sides).
std::vector<uint16_t> Codes(std::string_view name) {
  auto vk = [](VirtualKey key) { return uint16_t(key); };
  const std::string key = Lower(name);
  if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
    return {uint16_t(uint16_t(VirtualKey::kA) + (key[0] - 'a'))};
  }
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
    return {uint16_t(uint16_t(VirtualKey::k0) + (key[0] - '0'))};
  }
  if (key.size() >= 2 && key[0] == 'f' && std::isdigit(static_cast<unsigned char>(key[1]))) {
    const int number = std::atoi(key.c_str() + 1);
    if (number >= 1 && number <= 12) return {uint16_t(uint16_t(VirtualKey::kF1) + number - 1)};
  }
  struct Named {
    const char* name;
    std::vector<uint16_t> codes;
  };
  static const std::vector<Named> named = {
      {"space", {vk(VirtualKey::kSpace)}},
      {"enter", {vk(VirtualKey::kReturn)}},
      {"return", {vk(VirtualKey::kReturn)}},
      {"escape", {vk(VirtualKey::kEscape)}},
      {"esc", {vk(VirtualKey::kEscape)}},
      {"backspace", {vk(VirtualKey::kBack)}},
      {"tab", {vk(VirtualKey::kTab)}},
      {"shift", {vk(VirtualKey::kShift), vk(VirtualKey::kLShift), vk(VirtualKey::kRShift)}},
      {"lshift", {vk(VirtualKey::kLShift)}},
      {"rshift", {vk(VirtualKey::kRShift)}},
      {"ctrl", {vk(VirtualKey::kControl), vk(VirtualKey::kLControl), vk(VirtualKey::kRControl)}},
      {"control", {vk(VirtualKey::kControl), vk(VirtualKey::kLControl), vk(VirtualKey::kRControl)}},
      {"lctrl", {vk(VirtualKey::kLControl)}},
      {"rctrl", {vk(VirtualKey::kRControl)}},
      {"alt", {vk(VirtualKey::kMenu), vk(VirtualKey::kLMenu), vk(VirtualKey::kRMenu)}},
      {"lalt", {vk(VirtualKey::kLMenu)}},
      {"ralt", {vk(VirtualKey::kRMenu)}},
      {"up", {vk(VirtualKey::kUp)}},
      {"down", {vk(VirtualKey::kDown)}},
      {"left", {vk(VirtualKey::kLeft)}},
      {"right", {vk(VirtualKey::kRight)}},
      {"home", {vk(VirtualKey::kHome)}},
      {"end", {vk(VirtualKey::kEnd)}},
      {"insert", {vk(VirtualKey::kInsert)}},
      {"delete", {vk(VirtualKey::kDelete)}},
      {"pageup", {vk(VirtualKey::kPrior)}},
      {"pagedown", {vk(VirtualKey::kNext)}},
      {"capslock", {vk(VirtualKey::kCapital)}},
      {"grave", {vk(VirtualKey::kOem3)}},
      {"mouse1", {vk(VirtualKey::kLButton)}},
      {"mouse2", {vk(VirtualKey::kRButton)}},
      {"mouse3", {vk(VirtualKey::kMButton)}},
      {"mouse4", {vk(VirtualKey::kXButton1)}},
      {"mouse5", {vk(VirtualKey::kXButton2)}},
      {"wheelup", {kWheelUp}},
      {"wheeldown", {kWheelDown}},
  };
  for (const Named& entry : named) {
    if (key == entry.name) return entry.codes;
  }
  return {};
}

std::vector<uint16_t> Parse(const std::string& text, const char* setting) {
  std::vector<uint16_t> codes;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(',', start);
    if (end == std::string::npos) end = text.size();
    std::string_view name(text.data() + start, end - start);
    while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
    if (!name.empty()) {
      const std::vector<uint16_t> found = Codes(name);
      if (found.empty()) REXLOG_WARN("F.E.A.R. keys: {} has an unknown key name '{}'", setting, name);
      codes.insert(codes.end(), found.begin(), found.end());
    }
    start = end + 1;
  }
  return codes;
}

uint32_t LoadWord(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return std::byteswap(value);
}

using Clock = std::chrono::steady_clock;

class Driver;
Driver* g_driver = nullptr;

// The game's side, set by the hooks.
std::atomic<bool> g_gameplay{false};               // the binding manager is enabled
std::atomic<int64_t> g_update_ticks{0};            // its last update, Clock ticks
double g_frame_yaw = 0.0, g_frame_pitch = 0.0;     // this turn update's mouse movement, radians

constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x46454152);  // FEAR

// The keyboard and mouse: a window listener that keeps the keys and the mouse's movement, and a
// synthetic pad for the game's menus.
class Driver final : public rex::input::InputDriver,
                     public rex::ui::WindowInputListener,
                     public rex::ui::WindowListener {
 public:
  Driver() : InputDriver(nullptr, 0) {}
  ~Driver() override {
    if (g_driver == this) g_driver = nullptr;
  }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (!REXCVAR_GET(fear_mkb)) return;
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "F.E.A.R. keyboard and mouse";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice || !REXCVAR_GET(fear_mkb)) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->type = 0x01;
      out->sub_type = 0x01;
      out->gamepad.buttons = 0xFFFF;
      out->gamepad.left_trigger = 0xFF;
      out->gamepad.right_trigger = 0xFF;
    }
    return X_ERROR_SUCCESS;
  }

  // The pad the game's menus read; nothing while the player is in control (the keys are commands
  // then), so a controller works as before.
  X_RESULT GetDeviceState(rex::input::DeviceId id, X_INPUT_STATE* out) override {
    if (id != kDevice || !REXCVAR_GET(fear_mkb)) return X_ERROR_DEVICE_NOT_CONNECTED;
    const bool playing = Playing();
    UpdateCapture(playing && is_active() && has_focus_.load());
    std::lock_guard lock(mutex_);
    SyncMode(playing);
    uint16_t buttons = 0;
    if (!playing && is_active() && has_focus_.load()) {
      auto held = [&](std::initializer_list<VirtualKey> keys) {
        return std::any_of(keys.begin(), keys.end(), [&](VirtualKey key) { return Held(uint16_t(key)); });
      };
      if (held({VirtualKey::kUp, VirtualKey::kW})) buttons |= X_INPUT_GAMEPAD_DPAD_UP;
      if (held({VirtualKey::kDown, VirtualKey::kS})) buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
      if (held({VirtualKey::kLeft, VirtualKey::kA})) buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
      if (held({VirtualKey::kRight, VirtualKey::kD})) buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
      if (held({VirtualKey::kReturn, VirtualKey::kSpace})) buttons |= X_INPUT_GAMEPAD_A;
      if (held({VirtualKey::kEscape, VirtualKey::kBack})) buttons |= X_INPUT_GAMEPAD_B;
      if (held({VirtualKey::kQ, VirtualKey::kPrior})) buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
      if (held({VirtualKey::kE, VirtualKey::kNext})) buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
    }
    if (out) {
      std::memset(out, 0, sizeof(*out));
      if (buttons != last_buttons_) ++packet_;
      last_buttons_ = buttons;
      out->packet_number = packet_;
      out->gamepad.buttons = buttons;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(rex::input::DeviceId id, X_INPUT_VIBRATION*) override {
    return id == kDevice && REXCVAR_GET(fear_mkb) ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    return id == kDevice && REXCVAR_GET(fear_mkb) ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  void OnWindowAvailable(rex::ui::Window* window) override {
    if (!window || window_) return;
    window_ = window;
    window->AddInputListener(this, 0);
    window->AddListener(this);
  }

  // The commands whose keys are held, for the binding manager's update (guest thread).
  void Commands(uint8_t* states, uint32_t count) {
    std::lock_guard lock(mutex_);
    SyncMode(true);
    for (Action& action : Actions()) {
      const std::string& text = action.keys();
      if (text != action.parsed_from) {
        action.codes = Parse(text, action.setting);
        action.parsed_from = text;
      }
      if (action.command >= count) continue;
      bool on = false;
      for (uint16_t code : action.codes) {
        if (code == kWheelUp || code == kWheelDown) {
          on = on || Notch(code - kWheelUp, action.command);
        } else {
          on = on || Held(code);
        }
      }
      if (on) states[action.command] = 1;
    }
    // A notch is one frame on, then at least one off, so the next notch is a new press.
    for (int i = 0; i < 2; ++i) {
      if (notch_on_[i]) {
        notch_on_[i] = false;
        notch_off_frame_[i] = true;
      } else {
        notch_off_frame_[i] = false;
      }
    }
  }

  // The mouse movement since the last call, in counts (guest thread).
  void TakeMouse(double& dx, double& dy) {
    std::lock_guard lock(mutex_);
    dx = mouse_dx_;
    dy = mouse_dy_;
    mouse_dx_ = mouse_dy_ = 0.0;
  }

  // WindowInputListener (UI thread).
  void OnKeyDown(rex::ui::KeyEvent& e) override { SetKey(uint16_t(e.virtual_key()), true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { SetKey(uint16_t(e.virtual_key()), false); }
  void OnMouseDown(rex::ui::MouseEvent& e) override { SetKey(ButtonKey(e.button()), true); }
  void OnMouseUp(rex::ui::MouseEvent& e) override { SetKey(ButtonKey(e.button()), false); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!captured_) {
      last_x_ = e.x();
      last_y_ = e.y();
      return;
    }
    {
      std::lock_guard lock(mutex_);
      if (relative_) {
        mouse_dx_ += e.dx();
        mouse_dy_ += e.dy();
      } else {
        mouse_dx_ += e.x() - last_x_;
        mouse_dy_ += e.y() - last_y_;
      }
    }
    last_x_ = e.x();
    last_y_ = e.y();
    if (!relative_ && window_) {
      // Without a pointer lock, keep the cursor off the window's edges.
      int32_t x = 0, y = 0;
      if (window_->WarpMouseToCenter(x, y)) {
        last_x_ = x;
        last_y_ = y;
      }
    }
  }
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    if (!captured_) return;
    std::lock_guard lock(mutex_);
    wheel_ += e.scroll_y();
    while (wheel_ >= int32_t(rex::ui::MouseEvent::kScrollPerDetent)) {
      wheel_ -= rex::ui::MouseEvent::kScrollPerDetent;
      ++notches_[0];
    }
    while (wheel_ <= -int32_t(rex::ui::MouseEvent::kScrollPerDetent)) {
      wheel_ += rex::ui::MouseEvent::kScrollPerDetent;
      ++notches_[1];
    }
  }

  // WindowListener (UI thread).
  void OnClosing(rex::ui::UIEvent&) override {
    ApplyCapture(false);
    if (window_) {
      window_->RemoveInputListener(this);
      window_->RemoveListener(this);
      window_ = nullptr;
    }
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    has_focus_ = false;
    {
      std::lock_guard lock(mutex_);
      down_.fill(false);
      mouse_dx_ = mouse_dy_ = 0.0;
    }
    ApplyCapture(false);
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override { has_focus_ = true; }

 private:
  // The player is in control: the binding manager is enabled and has run lately.
  static bool Playing() {
    const int64_t last = g_update_ticks.load(std::memory_order_relaxed);
    const auto since = Clock::now().time_since_epoch() - Clock::duration(last);
    return g_gameplay.load(std::memory_order_relaxed) && last &&
           since < std::chrono::milliseconds(500);
  }

  static uint16_t ButtonKey(rex::ui::MouseEvent::Button button) {
    switch (button) {
      case rex::ui::MouseEvent::Button::kLeft: return uint16_t(VirtualKey::kLButton);
      case rex::ui::MouseEvent::Button::kRight: return uint16_t(VirtualKey::kRButton);
      case rex::ui::MouseEvent::Button::kMiddle: return uint16_t(VirtualKey::kMButton);
      case rex::ui::MouseEvent::Button::kX1: return uint16_t(VirtualKey::kXButton1);
      case rex::ui::MouseEvent::Button::kX2: return uint16_t(VirtualKey::kXButton2);
      default: return 0;
    }
  }

  void SetKey(uint16_t key, bool down) {
    if (!key || key >= 256) return;
    if (down && !has_focus_) return;
    std::lock_guard lock(mutex_);
    down_[key] = down;
    if (!down) held_over_[key] = false;
  }

  // A key held across a switch between play and the menus counts again only once let go, so
  // Escape that opened the pause menu doesn't also back out of it. Caller holds mutex_.
  void SyncMode(bool playing) {
    if (playing == mode_playing_) return;
    mode_playing_ = playing;
    held_over_ = down_;
    notches_[0] = notches_[1] = 0;
  }

  bool Held(uint16_t key) const { return key < 256 && down_[key] && !held_over_[key]; }

  // Whether the wheel direction's command is on this frame. Caller holds mutex_.
  bool Notch(int direction, uint32_t) {
    if (notch_on_[direction]) return true;
    if (notch_off_frame_[direction] || !notches_[direction]) return false;
    --notches_[direction];
    notch_on_[direction] = true;
    return true;
  }

  // Captures the mouse (hidden, relative motion) while the player is in control.
  void UpdateCapture(bool want) {
    if (want == capture_wanted_.load(std::memory_order_relaxed) || !window_) return;
    capture_wanted_ = want;
    window_->app_context().CallInUIThreadDeferred([this] { ApplyCapture(capture_wanted_.load()); });
  }

  void ApplyCapture(bool capture) {
    if (!window_ || capture == captured_) return;
    captured_ = capture;
    if (capture) {
      cursor_before_ = window_->GetCursorVisibility();
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      window_->CaptureMouse();
      relative_ = window_->SetRelativeMouseMode(true);
      if (!relative_) REXLOG_WARN("F.E.A.R. mouse: no pointer lock, keeping the cursor centred");
      std::lock_guard lock(mutex_);
      mouse_dx_ = mouse_dy_ = 0.0;
      wheel_ = 0;
    } else {
      window_->SetRelativeMouseMode(false);
      relative_ = false;
      window_->ReleaseMouse();
      window_->SetCursorVisibility(cursor_before_);
    }
  }

  rex::ui::Window* window_ = nullptr;

  std::mutex mutex_;
  std::array<bool, 256> down_{};
  std::array<bool, 256> held_over_{};
  bool mode_playing_ = false;
  double mouse_dx_ = 0.0, mouse_dy_ = 0.0;
  int32_t wheel_ = 0;
  int notches_[2] = {};  // wheel up, down
  bool notch_on_[2] = {};
  bool notch_off_frame_[2] = {};
  uint16_t last_buttons_ = 0;
  uint32_t packet_ = 0;

  std::atomic<bool> has_focus_{true};
  std::atomic<bool> capture_wanted_{false};
  // UI thread only.
  bool captured_ = false;
  bool relative_ = false;
  int32_t last_x_ = 0, last_y_ = 0;
  rex::ui::Window::CursorVisibility cursor_before_ = rex::ui::Window::CursorVisibility::kVisible;
};

}  // namespace

namespace fear::mkb {

void Install(rex::input::InputSystem& input, rex::ui::Window* window) {
  if constexpr (fear::title::kFiles) return;  // its own addresses: later (fear_mkb.h)
  auto driver = std::make_unique<Driver>();
  g_driver = driver.get();
  input.AddDriver(std::move(driver));
  // A driver added after the window reaches the input system gets it here.
  if (window) g_driver->OnWindowAvailable(window);
  REXLOG_INFO("F.E.A.R. keyboard and mouse: {}", REXCVAR_GET(fear_mkb) ? "on" : "off");
}

}  // namespace fear::mkb

void FearMkbCommands(PPCRegister& r30) {
  if constexpr (fear::title::kFiles) return;
  uint8_t* base = rex::system::kernel_memory()->virtual_membase();
  const uint32_t bindings = r30.u32;
  g_gameplay.store(base[bindings + kEnabledOffset] != 0, std::memory_order_relaxed);
  g_update_ticks.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  if (!g_driver || !REXCVAR_GET(fear_mkb)) return;
  const uint32_t begin = LoadWord(base, bindings + kStatesOffset);
  const uint32_t end = LoadWord(base, bindings + kStatesOffset + 4);
  if (!begin || end <= begin) return;
  g_driver->Commands(base + begin, end - begin);
}

#if !defined(FEAR_TITLE_FILES)
// CPlayerMgr's turn update: this frame's mouse movement, for its two reads below.
REX_EXTERN(__imp__sub_8237DC88);
REX_HOOK_RAW(sub_8237DC88) {
  g_frame_yaw = g_frame_pitch = 0.0;
  if (g_driver && REXCVAR_GET(fear_mkb)) {
    double dx = 0.0, dy = 0.0;
    g_driver->TakeMouse(dx, dy);
    const double scale = REXCVAR_GET(fear_mouse_sensitivity) * kRadiansPerCount;
    g_frame_yaw = dx * scale;
    g_frame_pitch = dy * scale * (REXCVAR_GET(fear_mouse_invert) ? -1.0 : 1.0);
  }
  __imp__sub_8237DC88(ctx, base);
}

// CBindMgr's largest-magnitude command value: the mouse adds to Yaw and Pitch.
REX_EXTERN(__imp__sub_82336938);
REX_HOOK_RAW(sub_82336938) {
  const uint32_t command = ctx.r4.u32;
  __imp__sub_82336938(ctx, base);
  if (command == kYaw) {
    ctx.f1.f64 += g_frame_yaw;
  } else if (command == kPitch) {
    ctx.f1.f64 += g_frame_pitch;
  }
}
#endif
