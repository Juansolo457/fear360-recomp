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
#include <deque>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/device_assignment.h>
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
                    "What plays (#59, PC Settings > Input): true, the keyboard and mouse, PC style "
                    "(mouse look and the PC's keys; controllers are left out of play); false, the "
                    "controllers. In the menus the keyboard is a pad and the mouse points either way");
REXCVAR_DEFINE_DOUBLE(fear_mouse_sensitivity, 1.0, "F.E.A.R.",
                      "Mouse look speed: 1 turns 0.022 degrees per mouse count, the usual PC "
                      "shooter default (at 800 dpi, 35 degrees an inch)");
REXCVAR_DEFINE_DOUBLE(fear_mouse_vertical, 1.0, "F.E.A.R.",
                      "Mouse look's up and down speed, a share of fear_mouse_sensitivity (1: the "
                      "same as left and right)");
REXCVAR_DEFINE_BOOL(fear_mouse_invert, false, "F.E.A.R.", "Invert the mouse's up and down");
REXCVAR_DEFINE_DOUBLE(fear_mouse_smoothing, 0.0, "F.E.A.R.",
                      "Mouse smoothing: the milliseconds each movement is spread over (0: off, "
                      "raw movement)");
REXCVAR_DEFINE_BOOL(fear_mkb_log, false, "F.E.A.R.",
                    "Testing: log each keyboard command as it goes on and the mouse look each second");

// The PC's default keys (the 360's own profile defaults). Key names, comma separated: A-Z, 0-9,
// F1-F24, Num0-Num9, Space, Enter, Escape, Backspace, Tab, Shift, Ctrl, Alt, Up, Down, Left,
// Right, Home, End, Insert, Delete, PageUp, PageDown, CapsLock, Grave, Minus, Equals, LBracket,
// RBracket, Backslash, Semicolon, Apostrophe, Comma, Period, Slash, Mouse1-Mouse5, WheelUp,
// WheelDown (the full list: KeyNames below). Empty: none.
REXCVAR_DEFINE_STRING(fear_key_forward, "W", "F.E.A.R./Keys", "Move forward");
REXCVAR_DEFINE_STRING(fear_key_backward, "S", "F.E.A.R./Keys", "Move backward");
REXCVAR_DEFINE_STRING(fear_key_strafe_left, "A", "F.E.A.R./Keys", "Strafe left");
REXCVAR_DEFINE_STRING(fear_key_strafe_right, "D", "F.E.A.R./Keys", "Strafe right");
REXCVAR_DEFINE_STRING(fear_key_jump, "Space", "F.E.A.R./Keys", "Jump");
REXCVAR_DEFINE_STRING(fear_key_crouch, "C", "F.E.A.R./Keys", "Crouch");
REXCVAR_DEFINE_STRING(fear_key_crouch_toggle, "", "F.E.A.R./Keys", "Crouch, toggled");
REXCVAR_DEFINE_STRING(fear_key_lean_left, "Q", "F.E.A.R./Keys", "Lean left");
REXCVAR_DEFINE_STRING(fear_key_lean_right, "E", "F.E.A.R./Keys", "Lean right");
REXCVAR_DEFINE_STRING(fear_key_walk, "", "F.E.A.R./Keys", "Walk (the PC's Run key, held)");
REXCVAR_DEFINE_STRING(fear_key_turn_left, "Left", "F.E.A.R./Keys", "Turn left");
REXCVAR_DEFINE_STRING(fear_key_turn_right, "Right", "F.E.A.R./Keys", "Turn right");
REXCVAR_DEFINE_STRING(fear_key_look_up, "Up", "F.E.A.R./Keys", "Look up");
REXCVAR_DEFINE_STRING(fear_key_look_down, "Down", "F.E.A.R./Keys", "Look down");
REXCVAR_DEFINE_STRING(fear_key_center_view, "End", "F.E.A.R./Keys", "Center the view");
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
REXCVAR_DEFINE_STRING(fear_key_weapon_4, "", "F.E.A.R./Keys", "Weapon slot 4");
REXCVAR_DEFINE_STRING(fear_key_weapon_5, "", "F.E.A.R./Keys", "Weapon slot 5");
REXCVAR_DEFINE_STRING(fear_key_weapon_6, "", "F.E.A.R./Keys", "Weapon slot 6");
REXCVAR_DEFINE_STRING(fear_key_weapon_7, "", "F.E.A.R./Keys", "Weapon slot 7");
REXCVAR_DEFINE_STRING(fear_key_weapon_8, "", "F.E.A.R./Keys", "Weapon slot 8");
REXCVAR_DEFINE_STRING(fear_key_weapon_9, "", "F.E.A.R./Keys", "Weapon slot 9");
REXCVAR_DEFINE_STRING(fear_key_objectives, "M", "F.E.A.R./Keys", "Objectives");
REXCVAR_DEFINE_STRING(fear_key_status, "Tab", "F.E.A.R./Keys", "Status");
REXCVAR_DEFINE_STRING(fear_key_menu, "Escape", "F.E.A.R./Keys", "Pause menu");

namespace {

using rex::X_RESULT;  // and X_STATUS: the X_ERROR and X_STATUS macros name them
using rex::X_STATUS;
using namespace rex::input;  // the X_INPUT structures and pad buttons
using rex::ui::VirtualKey;
using fear::mkb::MenuMouse;

// The game's command numbers (fear_mkb.h).
enum Command : uint32_t {
  kForward = 0x00,
  kBackward = 0x01,
  kStrafeLeft = 0x03,
  kStrafeRight = 0x04,
  kLookDown = 0x07,
  kLookUp = 0x08,
  kTurnRight = 0x09,
  kTurnLeft = 0x0A,
  kPitch = 0x0B,
  kYaw = 0x0C,
  kDuck = 0x0E,
  kJump = 0x0F,
  kRun = 0x10,
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
  kCenterView = 0x52,
  kActivate = 0x57,
  kReload = 0x58,
  kWeapon1 = 0x5B,  // to kWeapon1 + 8: CInterfaceMgr::OnCommandOn takes nine slots
  kCrouchToggle = 0x66,
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
  const char* label;  // on the Controls page; nullptr: not on it
  const char* group;
  const char* note;
  std::string parsed_from;  // the setting's text the codes were read from
  std::vector<uint16_t> codes;
};

#define FEAR_MKB_ACTION(name, command, label, group, note) \
  Action { &FLAGS_fear_key_##name##_storage_, "fear_key_" #name, command, label, group, note, {}, {} }

constexpr const char* kMovement = "Movement";
constexpr const char* kWeapons = "Weapons";
constexpr const char* kActions = "Actions";
constexpr const char* kInterface = "Interface";

// In the Controls page's order, by the PC's Configure Controls groups.
std::vector<Action>& Table() {
  static std::vector<Action> actions = {
      FEAR_MKB_ACTION(forward, kForward, "Move forward", kMovement, nullptr),
      FEAR_MKB_ACTION(backward, kBackward, "Move backward", kMovement, nullptr),
      FEAR_MKB_ACTION(strafe_left, kStrafeLeft, "Strafe left", kMovement, nullptr),
      FEAR_MKB_ACTION(strafe_right, kStrafeRight, "Strafe right", kMovement, nullptr),
      FEAR_MKB_ACTION(jump, kJump, "Jump", kMovement, nullptr),
      FEAR_MKB_ACTION(crouch, kDuck, "Crouch", kMovement, "Held."),
      FEAR_MKB_ACTION(crouch_toggle, kCrouchToggle, "Crouch (toggle)", kMovement,
                      "Press to crouch, again to stand."),
      FEAR_MKB_ACTION(lean_left, kLeanLeft, "Lean left", kMovement, nullptr),
      FEAR_MKB_ACTION(lean_right, kLeanRight, "Lean right", kMovement, nullptr),
      FEAR_MKB_ACTION(walk, kRun, "Walk", kMovement,
                      "Held: the other pace (the PC's Run key). F.E.A.R. runs by default."),
      FEAR_MKB_ACTION(turn_left, kTurnLeft, "Turn left", kMovement, nullptr),
      FEAR_MKB_ACTION(turn_right, kTurnRight, "Turn right", kMovement, nullptr),
      FEAR_MKB_ACTION(look_up, kLookUp, "Look up", kMovement, nullptr),
      FEAR_MKB_ACTION(look_down, kLookDown, "Look down", kMovement, nullptr),
      FEAR_MKB_ACTION(center_view, kCenterView, "Center view", kMovement, nullptr),
      FEAR_MKB_ACTION(fire, kFire, "Fire", kWeapons, nullptr),
      FEAR_MKB_ACTION(melee, kMelee, "Melee", kWeapons, "The PC's Alt-Fire."),
      FEAR_MKB_ACTION(zoom, kZoom, "Zoom", kWeapons, nullptr),
      FEAR_MKB_ACTION(reload, kReload, "Reload", kWeapons, nullptr),
      FEAR_MKB_ACTION(next_weapon, kNextWeapon, "Next weapon", kWeapons, nullptr),
      FEAR_MKB_ACTION(previous_weapon, kPreviousWeapon, "Previous weapon", kWeapons, nullptr),
      FEAR_MKB_ACTION(last_weapon, kLastWeapon, "Last weapon", kWeapons, nullptr),
      FEAR_MKB_ACTION(weapon_1, kWeapon1, "Weapon 1", kWeapons, nullptr),
      FEAR_MKB_ACTION(weapon_2, kWeapon1 + 1, "Weapon 2", kWeapons, nullptr),
      FEAR_MKB_ACTION(weapon_3, kWeapon1 + 2, "Weapon 3", kWeapons, nullptr),
      FEAR_MKB_ACTION(weapon_4, kWeapon1 + 3, "Weapon 4", kWeapons,
                      "Slots past 3 need more weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(weapon_5, kWeapon1 + 4, "Weapon 5", kWeapons,
                      "Slots past 3 need more weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(weapon_6, kWeapon1 + 5, "Weapon 6", kWeapons,
                      "Slots past 5 need 10 weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(weapon_7, kWeapon1 + 6, "Weapon 7", kWeapons,
                      "Slots past 5 need 10 weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(weapon_8, kWeapon1 + 7, "Weapon 8", kWeapons,
                      "Slots past 5 need 10 weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(weapon_9, kWeapon1 + 8, "Weapon 9", kWeapons,
                      "Slots past 5 need 10 weapon slots (PC Settings)."),
      FEAR_MKB_ACTION(use, kActivate, "Use", kActions,
                      "Doors, switches; held, picks up a weapon. The 360's X, so it reloads too."),
      FEAR_MKB_ACTION(throw_grenade, kThrowGrenade, "Throw grenade", kActions, nullptr),
      FEAR_MKB_ACTION(next_grenade, kNextGrenade, "Next grenade type", kActions, nullptr),
      FEAR_MKB_ACTION(slow_mo, kSlowMo, "Slow-mo", kActions, "Reflexes."),
      FEAR_MKB_ACTION(flashlight, kFlashlight, "Flashlight", kActions, nullptr),
      FEAR_MKB_ACTION(medkit, kMedkit, "Use medkit", kActions, nullptr),
      FEAR_MKB_ACTION(objectives, kObjectives, "Objectives", kInterface, nullptr),
      FEAR_MKB_ACTION(status, kStatus, "Status", kInterface, nullptr),
      // Escape, which opens the pause menu, isn't rebound (the PC keeps it too).
      FEAR_MKB_ACTION(menu, kMenu, nullptr, kInterface, nullptr),
  };
  return actions;
}

#undef FEAR_MKB_ACTION

// The settings' text, read by the game's thread (FearMkbCommands) and set by the UI thread
// (fear::mkb::SetFlags).
std::mutex g_keys_mutex;

// A key name: what the settings say, what the Controls page shows, and the codes it stands for
// (a side-neutral name stands for both sides; the window reports Shift, Ctrl and Alt without
// their side). Names are matched without case; the first name with a code is the one written.
struct KeyName {
  std::string name;
  std::string label;
  std::vector<uint16_t> codes;
};

const std::vector<KeyName>& KeyNames() {
  static const std::vector<KeyName> names = [] {
    auto vk = [](VirtualKey key) { return uint16_t(key); };
    std::vector<KeyName> out;
    for (char c = 'A'; c <= 'Z'; ++c) {
      out.push_back({std::string(1, c), std::string(1, c), {uint16_t(vk(VirtualKey::kA) + (c - 'A'))}});
    }
    for (char c = '0'; c <= '9'; ++c) {
      out.push_back({std::string(1, c), std::string(1, c), {uint16_t(vk(VirtualKey::k0) + (c - '0'))}});
    }
    for (int n = 1; n <= 24; ++n) {
      const std::string name = "F" + std::to_string(n);
      out.push_back({name, name, {uint16_t(vk(VirtualKey::kF1) + n - 1)}});
    }
    for (int n = 0; n <= 9; ++n) {
      out.push_back({"Num" + std::to_string(n), "Num " + std::to_string(n),
                     {uint16_t(vk(VirtualKey::kNumpad0) + n)}});
    }
    const std::vector<KeyName> named = {
        {"Space", "Space", {vk(VirtualKey::kSpace)}},
        {"Enter", "Enter", {vk(VirtualKey::kReturn)}},
        {"Return", "Enter", {vk(VirtualKey::kReturn)}},
        {"Escape", "Escape", {vk(VirtualKey::kEscape)}},
        {"Esc", "Escape", {vk(VirtualKey::kEscape)}},
        {"Backspace", "Backspace", {vk(VirtualKey::kBack)}},
        {"Tab", "Tab", {vk(VirtualKey::kTab)}},
        {"Shift", "Shift", {vk(VirtualKey::kShift), vk(VirtualKey::kLShift), vk(VirtualKey::kRShift)}},
        {"LShift", "Left Shift", {vk(VirtualKey::kLShift)}},
        {"RShift", "Right Shift", {vk(VirtualKey::kRShift)}},
        {"Ctrl", "Ctrl", {vk(VirtualKey::kControl), vk(VirtualKey::kLControl), vk(VirtualKey::kRControl)}},
        {"Control", "Ctrl", {vk(VirtualKey::kControl), vk(VirtualKey::kLControl), vk(VirtualKey::kRControl)}},
        {"LCtrl", "Left Ctrl", {vk(VirtualKey::kLControl)}},
        {"RCtrl", "Right Ctrl", {vk(VirtualKey::kRControl)}},
        {"Alt", "Alt", {vk(VirtualKey::kMenu), vk(VirtualKey::kLMenu), vk(VirtualKey::kRMenu)}},
        {"LAlt", "Left Alt", {vk(VirtualKey::kLMenu)}},
        {"RAlt", "Right Alt", {vk(VirtualKey::kRMenu)}},
        {"Up", "Up", {vk(VirtualKey::kUp)}},
        {"Down", "Down", {vk(VirtualKey::kDown)}},
        {"Left", "Left", {vk(VirtualKey::kLeft)}},
        {"Right", "Right", {vk(VirtualKey::kRight)}},
        {"Home", "Home", {vk(VirtualKey::kHome)}},
        {"End", "End", {vk(VirtualKey::kEnd)}},
        {"Insert", "Insert", {vk(VirtualKey::kInsert)}},
        {"Delete", "Delete", {vk(VirtualKey::kDelete)}},
        {"PageUp", "Page Up", {vk(VirtualKey::kPrior)}},
        {"PageDown", "Page Down", {vk(VirtualKey::kNext)}},
        {"CapsLock", "Caps Lock", {vk(VirtualKey::kCapital)}},
        {"Pause", "Pause", {vk(VirtualKey::kPause)}},
        {"ScrollLock", "Scroll Lock", {vk(VirtualKey::kScroll)}},
        {"NumLock", "Num Lock", {vk(VirtualKey::kNumLock)}},
        {"Grave", "`", {vk(VirtualKey::kOem3)}},
        {"Minus", "-", {vk(VirtualKey::kOemMinus)}},
        {"Equals", "=", {vk(VirtualKey::kOemPlus)}},
        {"LBracket", "[", {vk(VirtualKey::kOem4)}},
        {"RBracket", "]", {vk(VirtualKey::kOem6)}},
        {"Backslash", "\\", {vk(VirtualKey::kOem5)}},
        {"Semicolon", ";", {vk(VirtualKey::kOem1)}},
        {"Apostrophe", "'", {vk(VirtualKey::kOem7)}},
        {"Comma", ",", {vk(VirtualKey::kOemComma)}},
        {"Period", ".", {vk(VirtualKey::kOemPeriod)}},
        {"Slash", "/", {vk(VirtualKey::kOem2)}},
        {"NumMultiply", "Num *", {vk(VirtualKey::kMultiply)}},
        {"NumPlus", "Num +", {vk(VirtualKey::kAdd)}},
        {"NumMinus", "Num -", {vk(VirtualKey::kSubtract)}},
        {"NumPeriod", "Num .", {vk(VirtualKey::kDecimal)}},
        {"NumDivide", "Num /", {vk(VirtualKey::kDivide)}},
        {"Mouse1", "Mouse Left", {vk(VirtualKey::kLButton)}},
        {"Mouse2", "Mouse Right", {vk(VirtualKey::kRButton)}},
        {"Mouse3", "Mouse Middle", {vk(VirtualKey::kMButton)}},
        {"Mouse4", "Mouse 4", {vk(VirtualKey::kXButton1)}},
        {"Mouse5", "Mouse 5", {vk(VirtualKey::kXButton2)}},
        {"WheelUp", "Wheel Up", {kWheelUp}},
        {"WheelDown", "Wheel Down", {kWheelDown}},
    };
    out.insert(out.end(), named.begin(), named.end());
    return out;
  }();
  return names;
}

bool SameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

const KeyName* FindName(std::string_view name) {
  for (const KeyName& entry : KeyNames()) {
    if (SameName(entry.name, name)) return &entry;
  }
  return nullptr;
}

// The name written for a code (a key pressed while rebinding); empty if it has none.
std::string NameOf(uint16_t code) {
  for (const KeyName& entry : KeyNames()) {
    if (std::find(entry.codes.begin(), entry.codes.end(), code) != entry.codes.end()) {
      return entry.name;
    }
  }
  return {};
}

std::vector<std::string_view> Split(std::string_view text) {
  std::vector<std::string_view> names;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(',', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view name = text.substr(start, end - start);
    while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
    if (!name.empty()) names.push_back(name);
    start = end + 1;
  }
  return names;
}

std::vector<uint16_t> Parse(const std::string& text, const char* setting) {
  std::vector<uint16_t> codes;
  for (std::string_view name : Split(text)) {
    const KeyName* found = FindName(name);
    if (!found) {
      REXLOG_WARN("F.E.A.R. keys: {} has an unknown key name '{}'", setting, name);
      continue;
    }
    codes.insert(codes.end(), found->codes.begin(), found->codes.end());
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
std::atomic<int64_t> g_turn_ticks{0};              // CPlayerMgr's last turn update, Clock ticks
double g_frame_yaw = 0.0, g_frame_pitch = 0.0;     // this turn update's mouse movement, radians

constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x46454152);  // FEAR

std::atomic<bool> g_plays{kMkbDefault};  // fear_mkb as last applied (Install, then ModeChanged)

// The player is in control: the binding manager is enabled (it is in parts of the front end too,
// the intro movies) and the player's turn update has run lately (only in a level). Below Driver.
bool InPlay();

// The keyboard as a pad in the game's menus: the key, the button it holds and the button's
// keystroke.
struct MenuKey {
  VirtualKey key;
  uint16_t button;
  VirtualKey pad;
};
constexpr MenuKey kMenuKeys[] = {
    {VirtualKey::kUp, X_INPUT_GAMEPAD_DPAD_UP, VirtualKey::kXInputPadDpadUp},
    {VirtualKey::kW, X_INPUT_GAMEPAD_DPAD_UP, VirtualKey::kXInputPadDpadUp},
    {VirtualKey::kDown, X_INPUT_GAMEPAD_DPAD_DOWN, VirtualKey::kXInputPadDpadDown},
    {VirtualKey::kS, X_INPUT_GAMEPAD_DPAD_DOWN, VirtualKey::kXInputPadDpadDown},
    {VirtualKey::kLeft, X_INPUT_GAMEPAD_DPAD_LEFT, VirtualKey::kXInputPadDpadLeft},
    {VirtualKey::kA, X_INPUT_GAMEPAD_DPAD_LEFT, VirtualKey::kXInputPadDpadLeft},
    {VirtualKey::kRight, X_INPUT_GAMEPAD_DPAD_RIGHT, VirtualKey::kXInputPadDpadRight},
    {VirtualKey::kD, X_INPUT_GAMEPAD_DPAD_RIGHT, VirtualKey::kXInputPadDpadRight},
    {VirtualKey::kReturn, X_INPUT_GAMEPAD_A, VirtualKey::kXInputPadA},
    {VirtualKey::kSpace, X_INPUT_GAMEPAD_A, VirtualKey::kXInputPadA},
    {VirtualKey::kEscape, X_INPUT_GAMEPAD_B, VirtualKey::kXInputPadB},
    {VirtualKey::kBack, X_INPUT_GAMEPAD_B, VirtualKey::kXInputPadB},
    {VirtualKey::kQ, X_INPUT_GAMEPAD_LEFT_SHOULDER, VirtualKey::kXInputPadLShoulder},
    {VirtualKey::kPrior, X_INPUT_GAMEPAD_LEFT_SHOULDER, VirtualKey::kXInputPadLShoulder},
    {VirtualKey::kE, X_INPUT_GAMEPAD_RIGHT_SHOULDER, VirtualKey::kXInputPadRShoulder},
    {VirtualKey::kNext, X_INPUT_GAMEPAD_RIGHT_SHOULDER, VirtualKey::kXInputPadRShoulder},
};

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
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "F.E.A.R. keyboard and mouse";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
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
  // then), so the controllers work there as before, whichever plays.
  X_RESULT GetDeviceState(rex::input::DeviceId id, X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    const bool playing = Playing(), plays = REXCVAR_GET(fear_mkb);
    const bool active = is_active(), focus = has_focus_.load();
    UpdateCapture(playing && plays && active && focus);
    // Why the mouse isn't captured in play (with the keyboard and mouse what plays), logged as it
    // changes.
    const int blocked = !playing || !plays ? 0 : !focus ? 1 : !active ? 2 : 0;
    if (logged_blocked_.exchange(blocked) != blocked && blocked) {
      REXLOG_INFO("F.E.A.R. mouse: in play but not captured: {}",
                  blocked == 1 ? "the window doesn't have the focus" : "the game has no input now");
    }
    std::lock_guard lock(mutex_);
    SyncMode(playing);
    uint16_t buttons = 0;
    if (!playing && is_active() && has_focus_.load()) {
      for (const MenuKey& k : kMenuKeys) {
        if (Held(uint16_t(k.key))) buttons |= k.button;
      }
      if (pressed_button_ && Clock::now() < pressed_until_) buttons |= pressed_button_;
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
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // The same keys as pad keystrokes, for the menus that read those (XUI scenes, which the game's
  // XUI manager feeds from XInputGetKeystroke): queued as the keys go down, repeat and come up,
  // while the game has the input (SetKey). One not read within a quarter second is dropped.
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              X_INPUT_KEYSTROKE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    const Clock::time_point oldest = Clock::now() - std::chrono::milliseconds(250);
    while (!keystrokes_.empty() && keystrokes_.front().second < oldest) keystrokes_.pop_front();
    if (keystrokes_.empty()) return X_ERROR_EMPTY;
    if (out) *out = keystrokes_.front().first;
    keystrokes_.pop_front();
    return X_ERROR_SUCCESS;
  }

  void OnWindowAvailable(rex::ui::Window* window) override {
    if (!window || window_) return;
    window_ = window;
    window->AddInputListener(this, 0);
    window->AddListener(this);
  }

  // The commands whose keys are held, for the binding manager's update (guest thread).
  void Commands(uint8_t* states, uint32_t count) {
    const bool playing = Playing();
    std::lock_guard lock(mutex_);
    SyncMode(playing);
    if (!playing) return;
    for (Action& action : Table()) {
      {
        std::lock_guard keys_lock(g_keys_mutex);
        const std::string& text = action.keys();
        if (text != action.parsed_from) {
          action.codes = Parse(text, action.setting);
          action.parsed_from = text;
        }
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
      // The 360 binds no button to Activate: its X, Reload, uses things and, held, picks up a
      // weapon ("Press and hold X to pick up"). So Use is Reload too, as X is.
      if (on && action.command == kActivate && kReload < count) states[kReload] = 1;
      if (REXCVAR_GET(fear_mkb_log) && on != logged_on_[action.command & 0xFF]) {
        logged_on_[action.command & 0xFF] = on;
        REXLOG_INFO("F.E.A.R. keys: {} {}", action.setting, on ? "on" : "off");
      }
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

  // Rebinding on the Controls page (UI thread): the next press, a key code or a wheel direction.
  void StartRebind() {
    std::lock_guard lock(mutex_);
    rebinding_ = true;
    rebound_ = 0;
  }
  void StopRebind() {
    std::lock_guard lock(mutex_);
    rebinding_ = false;
    rebound_ = 0;
  }
  uint16_t TakeRebind() {
    std::lock_guard lock(mutex_);
    const uint16_t code = rebound_;
    if (code) {
      rebinding_ = false;
      rebound_ = 0;
    }
    return code;
  }

  // The mouse in the menus since the last call (guest thread), dropping what's older than a
  // quarter second: clicks made while no menu screen was taking them (loading, in play) don't
  // land on the next screen. The last move is kept, so the pointer still highlights its item.
  std::vector<MenuMouse> TakeMenuMouse() {
    std::lock_guard lock(mutex_);
    std::vector<MenuMouse> out;
    const Clock::time_point oldest = Clock::now() - std::chrono::milliseconds(250);
    size_t last_move = menu_mouse_.size();
    for (size_t i = 0; i < menu_mouse_.size(); ++i) {
      if (menu_mouse_[i].first.kind == MenuMouse::Kind::kMove) last_move = i;
    }
    for (size_t i = 0; i < menu_mouse_.size(); ++i) {
      if (menu_mouse_[i].second >= oldest || i == last_move) out.push_back(menu_mouse_[i].first);
    }
    menu_mouse_.clear();
    return out;
  }

  // A pad button pressed for the menus from the game's thread (a click on its prompt): held for a
  // tenth of a second, long enough for the menus' frame-by-frame reads, with its keystrokes.
  void PressPad(uint16_t button, VirtualKey pad) {
    std::lock_guard lock(mutex_);
    pressed_button_ = button;
    pressed_until_ = Clock::now() + std::chrono::milliseconds(100);
    for (uint16_t flags : {uint16_t(X_INPUT_KEYSTROKE_KEYDOWN), uint16_t(X_INPUT_KEYSTROKE_KEYUP)}) {
      if (keystrokes_.size() >= kKeystrokesKept) break;
      X_INPUT_KEYSTROKE ks = {};
      ks.virtual_key = uint16_t(pad);
      ks.flags = flags;
      keystrokes_.emplace_back(ks, Clock::now());
    }
  }

  // The controllers play now (PC Settings' Input row): the mouse is let go at once, since the
  // game's polls no longer reach the capture's update.
  void Release() { UpdateCapture(false); }

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
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    SetKey(ButtonKey(e.button()), true);
    if (e.button() == rex::ui::MouseEvent::Button::kLeft) PushMenuMouse(MenuMouse::Kind::kLeftDown, e);
    if (e.button() == rex::ui::MouseEvent::Button::kRight) PushMenuMouse(MenuMouse::Kind::kRightDown, e);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    SetKey(ButtonKey(e.button()), false);
    if (e.button() == rex::ui::MouseEvent::Button::kLeft) PushMenuMouse(MenuMouse::Kind::kLeftUp, e);
    if (e.button() == rex::ui::MouseEvent::Button::kRight) PushMenuMouse(MenuMouse::Kind::kRightUp, e);
  }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!captured_) {
      last_x_ = e.x();
      last_y_ = e.y();
      PushMenuMouse(MenuMouse::Kind::kMove, e);
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
    {
      std::lock_guard lock(mutex_);
      if (rebinding_ && !rebound_ && e.scroll_y()) {
        rebound_ = e.scroll_y() > 0 ? kWheelUp : kWheelDown;
        return;
      }
    }
    if (!captured_) {
      if (e.scroll_y()) PushMenuMouse(MenuMouse::Kind::kWheel, e, e.scroll_y());
      return;
    }
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
    if (has_focus_.exchange(false)) REXLOG_INFO("F.E.A.R. keyboard and mouse: the window lost the focus");
    {
      std::lock_guard lock(mutex_);
      down_.fill(false);
      mouse_dx_ = mouse_dy_ = 0.0;
    }
    ApplyCapture(false);
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    if (!has_focus_.exchange(true)) REXLOG_INFO("F.E.A.R. keyboard and mouse: the window has the focus");
  }

 private:
  // The player is in control (InPlay).
  static bool Playing() { return InPlay(); }

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
    // For the menus' pad keystrokes: the menus, while the game has the input (with a panel of ours
    // open, its Enter would reach the scene under it). A key up still follows a key down sent.
    const bool menus = !Playing() && is_active();
    std::lock_guard lock(mutex_);
    // A new press, not the key's repeat or one held when rebinding began (Enter that chose it).
    if (down && !down_[key] && rebinding_ && !rebound_) rebound_ = key;
    QueueKeystroke(key, down, menus);
    down_[key] = down;
    if (!down) held_over_[key] = false;
  }

  // The pad keystroke of a menu key going down (again, while held: a repeat) or up, in the menus
  // and without a rebind waiting. A key up only follows a key down sent. Caller holds mutex_.
  void QueueKeystroke(uint16_t key, bool down, bool menus) {
    for (const MenuKey& k : kMenuKeys) {
      if (uint16_t(k.key) != key) continue;
      const bool sent = keystroke_sent_[key];
      if (down && (!menus || rebinding_)) return;
      if (!down && !sent) return;
      if (keystrokes_.size() >= kKeystrokesKept) return;
      X_INPUT_KEYSTROKE ks = {};
      ks.virtual_key = uint16_t(k.pad);
      ks.flags = uint16_t(!down        ? X_INPUT_KEYSTROKE_KEYUP
                          : down_[key] ? X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT
                                       : X_INPUT_KEYSTROKE_KEYDOWN);
      ks.user_index = 0;  // the input system stamps the player this device drives
      keystrokes_.emplace_back(ks, Clock::now());
      keystroke_sent_[key] = down;
      return;
    }
  }

  // The mouse in the menus (UI thread), for TakeMenuMouse: a move replaces a move just before it.
  void PushMenuMouse(MenuMouse::Kind kind, const rex::ui::MouseEvent& e, int32_t wheel = 0) {
    if (captured_ || !window_ || !has_focus_) return;
    const MenuMouse m = {kind, e.x(), e.y(), window_->GetActualPhysicalWidth(),
                         window_->GetActualPhysicalHeight(), wheel};
    std::lock_guard lock(mutex_);
    if (kind == MenuMouse::Kind::kMove && !menu_mouse_.empty() &&
        menu_mouse_.back().first.kind == MenuMouse::Kind::kMove) {
      menu_mouse_.back() = {m, Clock::now()};
    } else if (menu_mouse_.size() < kMenuMouseKept) {
      menu_mouse_.emplace_back(m, Clock::now());
    }
  }

  // A key held across a switch between play and the menus counts again only once let go, so
  // Escape that opened the pause menu doesn't also back out of it. Caller holds mutex_.
  void SyncMode(bool playing) {
    if (playing == mode_playing_) return;
    mode_playing_ = playing;
    REXLOG_INFO("F.E.A.R. keyboard and mouse: {}",
                playing ? "in play (keys are commands)" : "in the menus (keys are a pad)");
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
    REXLOG_INFO("F.E.A.R. mouse: {}", capture ? "captured for mouse look" : "released");
    if (capture) {
      cursor_before_ = window_->GetCursorVisibility();
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      window_->CaptureMouse();
      relative_ = window_->SetRelativeMouseMode(true);
      if (!relative_) REXLOG_WARN("F.E.A.R. mouse: no pointer lock, keeping the cursor centred");
      std::lock_guard lock(mutex_);
      mouse_dx_ = mouse_dy_ = 0.0;
      wheel_ = 0;
      menu_mouse_.clear();
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
  std::array<bool, 256> logged_on_{};
  uint32_t packet_ = 0;
  bool rebinding_ = false;
  uint16_t rebound_ = 0;
  static constexpr size_t kMenuMouseKept = 64;
  std::vector<std::pair<MenuMouse, Clock::time_point>> menu_mouse_;
  static constexpr size_t kKeystrokesKept = 16;
  // Each with when it was queued: one not read within a quarter second (nothing was reading
  // keystrokes then) is dropped, so it can't land on the next scene that does.
  std::deque<std::pair<X_INPUT_KEYSTROKE, Clock::time_point>> keystrokes_;
  std::array<bool, 256> keystroke_sent_{};  // the key's last keystroke was a key down
  uint16_t pressed_button_ = 0;             // PressPad's, until pressed_until_
  Clock::time_point pressed_until_{};

  std::atomic<bool> has_focus_{true};
  std::atomic<bool> capture_wanted_{false};
  std::atomic<int> logged_blocked_{0};  // GetDeviceState's last logged reason (0: none)
  // UI thread only.
  bool captured_ = false;
  bool relative_ = false;
  int32_t last_x_ = 0, last_y_ = 0;
  rex::ui::Window::CursorVisibility cursor_before_ = rex::ui::Window::CursorVisibility::kVisible;
};

// Mouse smoothing (fear_mouse_smoothing, a time constant in milliseconds), for the turn update
// (guest thread). Each update applies the share of the movement not yet applied that the time
// since the last one calls for, and the rest carries over: no movement is lost, and the feel is
// the same at any frame rate. Off, or after a pause in the updates, everything applies at once.
void Smooth(double& yaw, double& pitch) {
  static double pending_yaw = 0.0, pending_pitch = 0.0;
  static Clock::time_point last{};
  const Clock::time_point now = Clock::now();
  const double seconds = last == Clock::time_point{} ? 0.0
                                                     : std::chrono::duration<double>(now - last).count();
  last = now;
  const double tau = REXCVAR_GET(fear_mouse_smoothing) / 1000.0;
  const double share = tau > 0.0 && seconds > 0.0 && seconds < 0.25 ? 1.0 - std::exp(-seconds / tau)
                                                                    : 1.0;
  pending_yaw += yaw;
  pending_pitch += pitch;
  yaw = pending_yaw * share;
  pitch = pending_pitch * share;
  pending_yaw -= yaw;
  pending_pitch -= pitch;
}

bool InPlay() {
  auto recent = [](const std::atomic<int64_t>& ticks) {
    const int64_t last = ticks.load(std::memory_order_relaxed);
    return last && Clock::now().time_since_epoch() - Clock::duration(last) <
                       std::chrono::milliseconds(250);
  };
  return g_gameplay.load(std::memory_order_relaxed) && recent(g_update_ticks) &&
         recent(g_turn_ticks);
}

// Which devices the game reads, over the SDK's assignment of the controllers (one per player
// slot, or every one to player 1 with fear_shared_controllers): in play with the keyboard and
// mouse what plays (fear_mkb), only their device, so the controllers are left out; otherwise, in
// the menus and with the controllers playing, everything, as before. Decided at each poll, so
// PC Settings' Input row applies at once; the panels read the pads and the keys themselves.
class Assignment final : public rex::input::DeviceAssignment {
 public:
  explicit Assignment(std::unique_ptr<rex::input::DeviceAssignment> pads) : pads_(std::move(pads)) {}

  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
    pads_->OnDevicesChanged(devices);
  }

  void DevicesForUser(uint32_t user_index, std::vector<rex::input::DeviceId>& out) const override {
    pads_->DevicesForUser(user_index, out);
    if (REXCVAR_GET(fear_mkb) && InPlay()) {
      std::erase_if(out, [](rex::input::DeviceId id) { return id != kDevice; });
    }
  }

 private:
  std::unique_ptr<rex::input::DeviceAssignment> pads_;
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
  g_plays = REXCVAR_GET(fear_mkb);
  REXLOG_INFO("F.E.A.R. input: {} play; the keyboard and mouse work in the menus either way",
              g_plays ? "the keyboard and mouse" : "the controllers");
}

bool Enabled() { return !fear::title::kFiles && g_driver; }

std::unique_ptr<rex::input::DeviceAssignment> Assign(
    std::unique_ptr<rex::input::DeviceAssignment> pads) {
  if constexpr (fear::title::kFiles) return pads;  // no keyboard and mouse there yet
  return std::make_unique<Assignment>(std::move(pads));
}

void ModeChanged() {
  const bool plays = REXCVAR_GET(fear_mkb);
  if (g_plays.exchange(plays) == plays) return;
  REXLOG_INFO("F.E.A.R. input: {} play now", plays ? "the keyboard and mouse" : "the controllers");
  if (g_driver && !plays) g_driver->Release();
}

std::vector<ActionInfo> BindableActions() {
  std::vector<ActionInfo> out;
  for (const Action& action : Table()) {
    if (action.label) out.push_back({action.setting, action.label, action.group, action.note});
  }
  return out;
}

std::vector<std::string> SplitKeys(std::string_view text) {
  std::vector<std::string> out;
  for (std::string_view name : Split(text)) out.emplace_back(name);
  return out;
}

std::string KeyLabel(std::string_view name) {
  const KeyName* found = FindName(name);
  return found ? found->label : std::string(name);
}

void StartCapture() {
  if (g_driver) g_driver->StartRebind();
}

std::string TakeCapture() {
  if (!g_driver) return {};
  const uint16_t code = g_driver->TakeRebind();
  if (!code) return {};
  std::string name = NameOf(code);
  if (name.empty()) g_driver->StartRebind();  // a key with no name (Windows, Menu): keep waiting
  return name;
}

void StopCapture() {
  if (g_driver) g_driver->StopRebind();
}

void SetFlags(const std::vector<std::pair<std::string, std::string>>& values) {
  std::lock_guard lock(g_keys_mutex);
  for (const auto& [name, value] : values) rex::cvar::SetFlagByName(name, value);
}

std::vector<MenuMouse> TakeMenuMouse() {
  if (!Enabled()) return {};
  return g_driver->TakeMenuMouse();
}

void PressMenuPad(MenuPad pad) {
  if (!Enabled()) return;
  if (pad == MenuPad::kB) {
    g_driver->PressPad(X_INPUT_GAMEPAD_B, VirtualKey::kXInputPadB);
  } else {
    g_driver->PressPad(X_INPUT_GAMEPAD_A, VirtualKey::kXInputPadA);
  }
}

std::string HintKey(std::string_view command) {
  if (!Enabled() || !g_plays.load()) return {};
  auto lower = [](std::string_view text) {
    std::string out(text);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
  };
  // The game's command names (the 360 controller configurations' and the hint strings'), to our
  // settings. The two movement axes and the two look axes name their keys together.
  struct Name {
    const char* command;
    const char* settings[4];
  };
  static constexpr Name kNames[] = {
      {"activate", {"fear_key_use"}},
      {"reload", {"fear_key_reload"}},
      {"fire", {"fear_key_fire"}},
      {"alt-fire", {"fear_key_melee"}},
      {"toggle melee", {"fear_key_melee"}},
      {"zoomin", {"fear_key_zoom"}},
      {"jump", {"fear_key_jump"}},
      {"duck", {"fear_key_crouch"}},
      {"leanleft", {"fear_key_lean_left"}},
      {"leanright", {"fear_key_lean_right"}},
      {"flashlight", {"fear_key_flashlight"}},
      {"medkit", {"fear_key_medkit"}},
      {"slowmo", {"fear_key_slow_mo"}},
      {"throwgrenade", {"fear_key_throw_grenade"}},
      {"nextgrenade", {"fear_key_next_grenade"}},
      {"nextweapon", {"fear_key_next_weapon"}},
      {"lastweapon", {"fear_key_last_weapon"}},
      {"mission", {"fear_key_objectives"}},
      {"controls", {"fear_key_objectives"}},
      {"forwardaxis", {"fear_key_forward", "fear_key_backward"}},
      {"strafeaxis", {"fear_key_strafe_left", "fear_key_strafe_right"}},
  };
  const std::string name = lower(command);
  if (name == "ingamemenu") return "Escape";
  if (name == "yaw" || name == "pitch") return "Mouse";
  for (const Name& entry : kNames) {
    if (name != entry.command) continue;
    std::string out;
    std::lock_guard lock(g_keys_mutex);
    for (const char* setting : entry.settings) {
      if (!setting) break;
      const std::vector<std::string> keys = SplitKeys(rex::cvar::GetFlagByName(setting));
      if (keys.empty()) continue;
      if (!out.empty()) out += "/";
      out += KeyLabel(keys.front());
    }
    return out;
  }
  return {};
}

}  // namespace fear::mkb

#if !defined(FEAR_TITLE_FILES)
// The hint text's |Command| expanders (four copies, fear_manifest.toml): at their insert call
// (sub_8236F290: the text at r5, r6 characters) the command's name is the wide string at r1+276
// (MSVC layout: 8 characters in place, size +16, capacity +20) and r5 the button's glyph. With the
// keyboard and mouse playing, the bound key's name goes in instead (fear_mkb.h).
void FearHintKey(PPCRegister& r1, PPCRegister& r5, PPCRegister& r6) {
  uint8_t* base = rex::system::kernel_memory()->virtual_membase();
  const uint32_t name_at = r1.u32 + 276;
  const uint32_t size = LoadWord(base, name_at + 16);
  const uint32_t capacity = LoadWord(base, name_at + 20);
  if (size == 0 || size > 64) return;
  const uint32_t chars = capacity >= 8 ? LoadWord(base, name_at) : name_at;
  std::string command;
  for (uint32_t i = 0; i < size; ++i) {
    const uint16_t c = uint16_t(base[chars + i * 2] << 8 | base[chars + i * 2 + 1]);
    command += c < 0x80 ? char(c) : '?';
  }
  const std::string key = fear::mkb::HintKey(command);
  static std::mutex logged_mutex;
  static std::vector<std::string> logged;
  {
    std::lock_guard lock(logged_mutex);
    if (std::find(logged.begin(), logged.end(), command) == logged.end()) {
      logged.push_back(command);
      REXLOG_INFO("F.E.A.R. hints: |{}| shows {}", command, key.empty() ? "the button" : key);
    }
  }
  if (key.empty()) return;
  // One buffer: the insert copies it at once, and the hints are built on the game's thread.
  static uint32_t buffer = 0;
  constexpr uint32_t kChars = 48;
  if (!buffer) buffer = rex::system::kernel_memory()->SystemHeapAlloc(kChars * 2);
  if (!buffer) return;
  const size_t count = std::min<size_t>(key.size(), kChars - 1);
  for (size_t i = 0; i < count; ++i) {
    base[buffer + i * 2] = 0;
    base[buffer + i * 2 + 1] = uint8_t(key[i]);
  }
  base[buffer + count * 2] = base[buffer + count * 2 + 1] = 0;
  r5.u64 = buffer;
  r6.u64 = count;
}
#endif

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
  g_turn_ticks.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  g_frame_yaw = g_frame_pitch = 0.0;
  if (g_driver && REXCVAR_GET(fear_mkb)) {
    double dx = 0.0, dy = 0.0;
    g_driver->TakeMouse(dx, dy);
    const double scale = REXCVAR_GET(fear_mouse_sensitivity) * kRadiansPerCount;
    g_frame_yaw = dx * scale;
    g_frame_pitch = dy * scale * REXCVAR_GET(fear_mouse_vertical) *
                    (REXCVAR_GET(fear_mouse_invert) ? -1.0 : 1.0);
    Smooth(g_frame_yaw, g_frame_pitch);
    if (REXCVAR_GET(fear_mkb_log)) {
      static double sum_x = 0.0, sum_y = 0.0;
      static int frames = 0;
      static Clock::time_point since = Clock::now();
      sum_x += dx;
      sum_y += dy;
      ++frames;
      if (Clock::now() - since >= std::chrono::seconds(1)) {
        REXLOG_INFO("F.E.A.R. mouse: {} turn updates, {:.0f} x {:.0f} counts", frames, sum_x, sum_y);
        sum_x = sum_y = 0.0;
        frames = 0;
        since = Clock::now();
      }
    }
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
