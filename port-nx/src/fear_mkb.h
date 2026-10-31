// F.E.A.R. 360 - keyboard and mouse, PC style (#59).
//
// The 360 game kept the PC's input code. Its binding manager (the PC's CBindMgr: sub_823367C8,
// sub_82336880, sub_82336938 answer a command's largest, smallest and largest-magnitude value over
// its bindings, 60-byte records; sub_82336FA8 is the per-frame update) still takes the PC's
// commands, and CPlayerMgr's turn update (sub_8237DC88) still reads the mouse's two, Yaw (0x0C)
// and Pitch (0x0B), next to the stick's YawAccel (0x17) and PitchAccel (0x16), and applies the
// PC's mouse dampening (MouseAimDampen, MouseSloMoDampen, ...) and MouseInvertY to them. Even the
// profile defaults (Game/ProfileDatabase/Defaults.gamedb) are the PC's keyboard and mouse layout.
// What the 360 lacks is the device: its ILTInput (vtable 0x82005F70) knows four pads of 24
// controls each, so the profile's Keyboard and Mouse bindings never resolve.
//
// So this supplies the device at the binding manager's level:
//   - the mouse: the turn update's two Yaw and Pitch reads (largest-magnitude value) also get the
//     mouse movement since the last turn update, in radians (fear_mouse_sensitivity 1 = 0.022
//     degrees per count, the PC shooter default);
//   - the keys: after the update has built this frame's command states (one byte per command,
//     at the manager's +0x14) and before it compares them with the last frame's to send the
//     game its command on and off events (0x82337300, a [[midasm_hook]]), the commands whose keys
//     are held are switched on. The game then raises the events and answers "is this command on"
//     itself, exactly as for its own bindings. The mouse wheel turns its command on for one frame
//     per notch.
// The keys are the PC's defaults from that profile, each a setting (fear_key_*: key names, comma
// separated, e.g. "W,Up"). The command numbers come from the game's tables (the 360 controller
// configurations' names at 0x830ABD40, the PC names and IDs registered in sub_823F9910) and
// CInterfaceMgr::OnCommandOn's switch (sub_82364950: weapons 0x4C-0x4D, slots from 0x5B).
// Recompilation Settings' Controls page (fear_controls.h) rebinds them and sets the mouse's speed,
// vertical speed, invert and smoothing, the options PC F.E.A.R.'s Controls screens have.
//
// In the game's own menus, the keyboard is a pad instead: arrows and WASD the D-pad, Enter and
// Space A, Escape and Backspace B, Q and E the shoulders, both as the pad's state (the PC's menu
// screens) and as its keystrokes (the XUI scenes, #110). The mouse points and clicks there through
// the PC's own menu code (TakeMenuMouse, fear_menu.h). Which one applies follows the binding
// manager's enabled flag (+0x40): on while the player is in control. The mouse is captured
// (hidden, relative motion) only then, with the keyboard and mouse what plays, and while no panel
// is open.
//
// One or the other plays (fear_mkb, Recompilation Settings > Input, applied at once): with the
// keyboard and mouse, the controllers are left out of play (Assign: the game's player gets only
// this device then); with the controllers, the mouse isn't captured and the keys aren't commands.
// In the menus both always work, so the setting can be reached with whatever is in hand. The Steam
// Deck build starts with the controllers, since there a trackpad can send mouse movement. F.E.A.R.
// only for now: F.E.A.R. Files has its own addresses.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/ppc/context.h>

namespace rex::input {
class DeviceAssignment;
class InputSystem;
}
namespace rex::ui {
class Window;
}

namespace fear::mkb {

// Adds the keyboard and mouse to the runtime's input. Call before the input system's active
// callback is set (it reaches only the drivers it already has).
void Install(rex::input::InputSystem& input, rex::ui::Window* window);

// Whether the keyboard and mouse are there (F.E.A.R.; not F.E.A.R. Files yet). Whether they play
// is fear_mkb.
bool Enabled();

// The game's devices (rex::input::DeviceAssignment), over `pads`, the SDK's assignment of the
// controllers: in play with the keyboard and mouse what plays, only their device; otherwise what
// `pads` gives. F.E.A.R. Files: `pads` as it is.
std::unique_ptr<rex::input::DeviceAssignment> Assign(
    std::unique_ptr<rex::input::DeviceAssignment> pads);

// fear_mkb was set (Recompilation Settings' Input row): logged, and with the controllers now what
// plays, the mouse is let go.
void ModeChanged();

// For Recompilation Settings' Controls page (fear_controls.h).

// An action keys can be bound to, in the page's order.
struct ActionInfo {
  const char* setting;  // its fear_key_* setting
  const char* label;
  const char* group;    // Movement, Weapons, Actions, Interface
  const char* note;     // or nullptr
};
std::vector<ActionInfo> BindableActions();

// The key names in a fear_key_* setting's text, in order, as written.
std::vector<std::string> SplitKeys(std::string_view text);
// A key name as the page shows it ("Mouse1": "Mouse Left"); unknown names as written.
std::string KeyLabel(std::string_view name);

// Rebinding, on the UI thread: after StartCapture, the next key, mouse button or wheel notch
// pressed, as a key name ("Escape" for Escape), once; empty until then.
void StartCapture();
std::string TakeCapture();
void StopCapture();

// Sets keyboard and mouse settings (cvar, value) between two reads by the game's thread.
void SetFlags(const std::vector<std::pair<std::string, std::string>>& values);

// The mouse in the game's menus, while it isn't captured for mouse look: what it did since the
// last call (at most a quarter second back), for fear_menu.h's hook on the game's thread. Empty
// while the keyboard and mouse are off.
struct MenuMouse {
  enum class Kind : uint8_t { kMove, kLeftDown, kLeftUp, kRightDown, kRightUp, kWheel };
  Kind kind;
  int32_t x, y;            // in the window, physical pixels
  uint32_t width, height;  // the window's size then, physical pixels
  int32_t wheel;           // kWheel: up positive, 120 a notch
};
std::vector<MenuMouse> TakeMenuMouse();

// A click on a menu screen's button prompt ("B Back", "A Select", #110): that pad button, pressed
// for the menus as a controller would (its state for a moment, and its keystrokes). Game thread.
enum class MenuPad : uint8_t { kA, kB };
void PressMenuPad(MenuPad pad);

// The hint text's |Command| (the game's command name, "Reload", "FlashLight") as its bound keys'
// names ("R"), with the keyboard and mouse playing; empty otherwise, and the game's button glyph
// stays. The 360 put a controller glyph there (CXProfile method 138); the PC put the key's name.
std::string HintKey(std::string_view command);

}  // namespace fear::mkb

// [[midasm_hook]] in CBindMgr's update, between building the frame's command states and sending
// the changes (fear_manifest.toml).
void FearMkbCommands(PPCRegister& r30);

// [[midasm_hook]] at the insert call of each |Command| expander in the hint text (HintKey).
void FearHintKey(PPCRegister& r1, PPCRegister& r5, PPCRegister& r6);
