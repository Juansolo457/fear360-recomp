// F.E.A.R. PC - game controllers in the game client (FEAR_PC_PAD).
//
// The pad is a device the engine doesn't know: CBindMgr asks this for command states and axis
// values next to its own bindings, CPlayerMgr asks it for the turn, and CGameClientShell calls
// FearPadFrame once a frame. Outside play (menus, splash screens, loading) the pad sends the
// menu keys instead: D-pad or left stick for the arrows, A or Start for Enter, B for Escape.
// In play, Start opens the menu (Escape).
//
// Console variables (also settable as +Name value on the command line):
//   PadEnabled          1   0 ignores pads
//   PadLayout           0   0 the 360's Normal layout, 1 its Lean layout (pc/core layout.h)
//   PadLookRamp         3   0 as the 360 shipped, 1 no delay, 2 instant, 3 off (pc/core look.h)
//   PadLookSensitivity  1   scales the 360's turn rates
//   PadSoftLock         1   slow turning with an enemy under the crosshair, as the 360 does
#pragma once

#include "ltbasedefs.h"

void FearPadFrame();
bool FearPadCommandOn(uint32 nCommand);
float FearPadCommandValue(uint32 nCommand);
// The turn for this frame in radians (yaw positive right, pitch positive down); false when no
// pad is in use.
bool FearPadLook(float fFrameTime, float* pYaw, float* pPitch);
