// F.E.A.R. PC - the Xbox 360 game's controller layouts. See fearpc/layout.h.
#include "fearpc/layout.h"

namespace fearpc {

namespace {

using A = PadAction;
using I = PadInput;

constexpr PadBinding kNormal[] = {
    {A::kMoveX, I::kLeftX},           {A::kMoveY, I::kLeftY},
    {A::kLookX, I::kRightX},          {A::kLookY, I::kRightY},
    {A::kFire, I::kRightTrigger},     {A::kThrowGrenade, I::kLeftTrigger},
    {A::kNextWeapon, I::kRightShoulder}, {A::kSlowMo, I::kLeftShoulder},
    {A::kJump, I::kA},                {A::kMelee, I::kB},
    {A::kUseOrReload, I::kX},         {A::kMedkit, I::kY},
    {A::kNextGrenade, I::kDpadUp},    {A::kFlashlight, I::kDpadDown},
    {A::kLeanLeft, I::kDpadLeft},     {A::kLeanRight, I::kDpadRight},
    {A::kCrouch, I::kLeftStick},      {A::kZoom, I::kRightStick},
    {A::kObjectives, I::kBack},       {A::kMenu, I::kStart},
};

constexpr PadBinding kLeaner[] = {
    {A::kMoveX, I::kLeftX},           {A::kMoveY, I::kLeftY},
    {A::kLookX, I::kRightX},          {A::kLookY, I::kRightY},
    {A::kFire, I::kRightTrigger},     {A::kThrowGrenade, I::kLeftTrigger},
    {A::kNextWeapon, I::kDpadRight},  {A::kSlowMo, I::kY},
    {A::kJump, I::kA},                {A::kMelee, I::kB},
    {A::kUseOrReload, I::kX},         {A::kMedkit, I::kDpadUp},
    {A::kNextGrenade, I::kDpadLeft},  {A::kFlashlight, I::kDpadDown},
    {A::kLeanLeft, I::kLeftShoulder}, {A::kLeanRight, I::kRightShoulder},
    {A::kCrouch, I::kLeftStick},      {A::kZoom, I::kRightStick},
    {A::kObjectives, I::kBack},       {A::kMenu, I::kStart},
};

}  // namespace

const PadBinding* LayoutBindings(PadLayout layout, size_t* count) {
  if (layout == PadLayout::kLeaner) {
    *count = sizeof(kLeaner) / sizeof(kLeaner[0]);
    return kLeaner;
  }
  *count = sizeof(kNormal) / sizeof(kNormal[0]);
  return kNormal;
}

PadInput LayoutInput(PadLayout layout, PadAction action) {
  size_t count = 0;
  const PadBinding* bindings = LayoutBindings(layout, &count);
  for (size_t i = 0; i < count; ++i) {
    if (bindings[i].action == action) return bindings[i].input;
  }
  return PadInput::kCount;
}

char16_t ButtonGlyph(PadInput input) {
  switch (input) {
    case I::kA: return 0xE000;
    case I::kB: return 0xE001;
    case I::kX: return 0xE002;
    case I::kY: return 0xE003;
    case I::kStart: return 0xE004;
    case I::kBack: return 0xE005;
    case I::kLeftStick: return 0xE006;
    case I::kRightStick: return 0xE007;
    case I::kLeftShoulder: return 0xE008;
    case I::kRightShoulder: return 0xE009;
    case I::kDpadUp: return 0xE00A;
    case I::kDpadDown: return 0xE00B;
    case I::kDpadLeft: return 0xE00C;
    case I::kDpadRight: return 0xE00D;
    case I::kLeftX:
    case I::kLeftY: return 0xE00E;
    case I::kRightX:
    case I::kRightY: return 0xE00F;
    case I::kLeftTrigger: return 0xE010;
    case I::kRightTrigger: return 0xE011;
    default: return 0;
  }
}

}  // namespace fearpc
