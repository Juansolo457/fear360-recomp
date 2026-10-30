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
// The keys are the PC's defaults from that profile, each a setting (fear_key_*: key names,
// comma separated, e.g. "W,Up"). The command numbers come from the game's tables (the 360
// controller configurations' names at 0x830ABD40, the PC names built in sub_823F9910) and
// CInterfaceMgr::OnCommandOn's switch (sub_82364950: weapons 0x4C-0x4D, slots from 0x5B).
//
// In the game's own menus (XUI, which only reads a pad), the keyboard is a pad instead: arrows
// and WASD the D-pad, Enter and Space A, Escape and Backspace B, Q and E the shoulders. Which one
// applies follows the binding manager's enabled flag (+0x40): on while the player is in control.
// The mouse is captured (hidden, relative motion) only then and while no panel is open.
//
// Both always work together with a controller; the Steam Deck build leaves this off
// (fear_mkb), since there a trackpad can send mouse movement. F.E.A.R. only for now: F.E.A.R.
// Files has its own addresses.

#pragma once

#include <rex/ppc/context.h>

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class Window;
}

namespace fear::mkb {

// Adds the keyboard and mouse to the runtime's input. Call before the input system's active
// callback is set (it reaches only the drivers it already has).
void Install(rex::input::InputSystem& input, rex::ui::Window* window);

}  // namespace fear::mkb

// [[midasm_hook]] in CBindMgr's update, between building the frame's command states and sending
// the changes (fear_manifest.toml).
void FearMkbCommands(PPCRegister& r30);
