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
//   PadLookRamp         0   0 as the 360 shipped, 1 no delay, 2 instant, 3 off (pc/core look.h)
//   PadLookSensitivity  1   scales the 360's turn rates
//   PadSoftLock         1   slow turning with an enemy under the crosshair, as the 360 does
//   PadRumble           1   vibration strength, 0 to 1 (0 off)
//
// Vibration: the PC game computes motor strengths from rumble effects but sends them nowhere,
// and its effect files have none, so the pad also rumbles for your shots, damage you take and
// the camera shakes the game plays (explosions, heavy impacts). The pad is only told when the
// strength changes, and refreshed at most every kRumbleRefresh while it runs.
#pragma once

#include "ltbasedefs.h"

class LTRigidTransform;

// The controller settings, as the Controller screen shows them (fear_pad_screen.cpp). Saved to
// fearpc_pad.txt in the user directory and read back at startup.
struct FearPadSettings
{
	bool	bEnabled;
	uint8	nLayout;		// 0 Normal, 1 Lean
	uint8	nLookRamp;		// PadLookRamp: 0 as shipped, 1 no delay, 2 instant, 3 off
	int		nSensitivity;	// percent of the 360's turn rates
	bool	bSoftLock;
	int		nRumble;		// percent
};
FearPadSettings FearPadGetSettings();
void FearPadSetSettings(const FearPadSettings& settings);

void FearPadFrame();
// Once a camera update (CPlayerCamera): the effect system's camera offset, whose size is how
// hard the view is shaking, and its two motor strengths.
void FearPadCameraEffects(const LTRigidTransform& tEffect, const float* pMotors);
bool FearPadCommandOn(uint32 nCommand);
float FearPadCommandValue(uint32 nCommand);
// The turn for this frame in radians (yaw positive right, pitch positive down); false when no
// pad is in use.
bool FearPadLook(float fFrameTime, float* pYaw, float* pPitch);
