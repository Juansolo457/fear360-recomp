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

// The character the 360 game writes in its text for a button (Interface/XenonButtons,
// IconGlyph): A U+E000, B E001, X E002, Y E003, Start E004, Back E005, L3 E006, R3 E007,
// LB E008, RB E009, D-pad up/down/left/right E00A to E00D, LT E010, RT E011. The 360 has none
// for a stick's movement; ours are E00E (left stick) and E00F (right stick), which it leaves
// unused. 0 for PadInput::kCount.
char16_t ButtonGlyph(PadInput input);
// Cells in the glyph picture, one per character from kGlyphFirst: 8 across, the Xbox set in
// the first kGlyphsPerSet cells and the PlayStation set in the next.
constexpr char16_t kGlyphFirst = 0xE000;
constexpr int kGlyphColumns = 8;
constexpr int kGlyphsPerSet = 32;

}  // namespace fearpc
