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

}  // namespace fearpc
