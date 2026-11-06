// F.E.A.R. PC - the Xbox 360 game's controller layouts.
//
// From the 360 game's database (Loki.gamedb, Interface/XenonControlsConfig: each action names an
// Interface/XenonButtons record). Default is the 360's "Normal" preset:
//
//   left stick  move          right stick  look         L3  crouch       R3  zoom
//   RT  fire                  LT  throw grenade         RB  next weapon  LB  slow-mo
//   A   jump                  B   melee                 X   use / reload Y   medkit
//   D-pad up  next grenade    down  flashlight          left / right  lean
//   Back  objectives          Start  pause menu
//
// The 360 leaves "Activate" unbound and its X both uses and reloads; the PC has two commands,
// so kUseOrReload picks one when pressed (pc/game/src/fear_pad.cpp).
#pragma once

#include <cstddef>

#include "fearpc/pad.h"

namespace fearpc {

enum class PadAction : unsigned char {
  kMoveX,  // axes: the input's value
  kMoveY,
  kLookX,
  kLookY,
  kFire,  // buttons
  kThrowGrenade,
  kNextWeapon,
  kSlowMo,
  kJump,
  kMelee,
  kUseOrReload,
  kMedkit,
  kNextGrenade,
  kFlashlight,
  kLeanLeft,
  kLeanRight,
  kCrouch,
  kZoom,
  kObjectives,
  kMenu,
  kCount,
};

struct PadBinding {
  PadAction action;
  PadInput input;
};

// kLeaner is the 360's "Lean" preset (XenonControlsConfig/Leaner): leaning on the shoulder
// buttons, medkit on D-pad up, next grenade and next weapon on D-pad left and right, slow-mo on Y.
enum class PadLayout : unsigned char { kNormal, kLeaner };

// The bindings of one layout; *count receives their number.
const PadBinding* LayoutBindings(PadLayout layout, size_t* count);

// The input an action is on in a layout, or PadInput::kCount if none.
PadInput LayoutInput(PadLayout layout, PadAction action);

}  // namespace fearpc
