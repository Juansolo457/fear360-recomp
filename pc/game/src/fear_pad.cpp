// F.E.A.R. PC - game controllers in the game client. See fear_pad.h.
#include "stdafx.h"

#include "fear_pad.h"

#include <stdio.h>

#include "ClientWeapon.h"
#include "ClientWeaponMgr.h"
#include "CommandIDs.h"
#include "GameClientShell.h"
#include "InterfaceMgr.h"
#include "MsgIDs.h"
#include "PlayerCamera.h"
#include "PlayerMgr.h"
#include "PlayerStats.h"
#include "TargetMgr.h"
#include "VarTrack.h"
#include "ltfileoperations.h"
#include "fearpc/layout.h"
#include "fearpc/look.h"
#include "fearpc/pad.h"

namespace {

using fearpc::PadAction;
using fearpc::PadInput;

VarTrack g_vtPadEnabled;
VarTrack g_vtPadLayout;
VarTrack g_vtPadLookRamp;
VarTrack g_vtPadLookSensitivity;
VarTrack g_vtPadSoftLock;
VarTrack g_vtPadMeasure;
VarTrack g_vtPadRumble;

// Vibration, per motor (0 the large low-frequency one, 1 the small high-frequency one).
float g_fFxMotors[2] = {};    // from rumble effects, this camera update
float g_fShake = 0.0f;        // the camera's effect shake, 0..1
float g_fKick[2] = {};        // shots and damage, fading out
float g_fSent[2] = {};        // what the pad was last told
double g_fSentAt = -1.0;      // when, real time
const CClientWeapon* g_pLastWeapon = NULL;
int32 g_nLastClip = -1;
int32 g_nLastHealth = -1;
const float kKickFade = 10.0f;         // per second: a kick is mostly gone in a tenth of a second
const float kRumbleChange = 0.05f;     // a change smaller than this waits for the refresh
const double kRumbleRefresh = 0.1;     // seconds between sends while the strength holds
const uint32 kRumbleDurationMs = 250;  // each send's motor time, so a stall can't leave it on

// PadMeasure 1: holds the right stick at each deflection in turn (no pad needed), measures how
// fast the camera actually turns, and writes pad_measure.txt to the user directory. This checks
// the whole path, the engine's smoothing and zoom included, against the 360's numbers.
// PadMeasure N (2 or more) waits N seconds into play first, past a level's opening cutscene.
struct Measure {
	bool bActive = false;
	int nStep = 0;
	double fStepStart = 0.0;
	double fYaw = 0.0;          // unwrapped camera yaw, degrees
	double fLastRawYaw = 0.0;
	double fWindowStartYaw = 0.0;
	bool bWindowOpen = false;
	FILE* pFile = NULL;
};
Measure g_Measure;
const float kMeasureDeflections[] = { 0.25f, 0.5f, 0.75f, 0.85f, 0.95f, 1.0f };
const int kMeasureSteps = sizeof(kMeasureDeflections) / sizeof(kMeasureDeflections[0]);
const double kMeasureSettle = 0.5, kMeasureWindow = 2.0;

bool g_bStarted = false;
bool g_bPlaying = false;
double g_fPlayStart = 0.0;  // when play last started, real time
fearpc::PadState g_State;      // this frame, neutral when the game isn't in front
fearpc::PadState g_LastState;  // the previous frame
// Inputs that were down when play started (the A that picked a menu item, say): ignored until
// let go, so they don't jump or fire on arrival.
bool g_bHeld[(int)PadInput::kCount] = {};
fearpc::LookModel g_Look;
uint32 g_nUseOrReload = COMMAND_ID_RELOAD;  // what X chose when it went down

// Menu keys: each direction or button that is down, and when it repeats next.
struct MenuKey {
	PadInput input;
	int nKey;
	double fNextRepeat;
	bool bDown;
};
MenuKey g_MenuKeys[] = {
	{ PadInput::kDpadUp, VK_UP, 0.0, false },
	{ PadInput::kDpadDown, VK_DOWN, 0.0, false },
	{ PadInput::kDpadLeft, VK_LEFT, 0.0, false },
	{ PadInput::kDpadRight, VK_RIGHT, 0.0, false },
	{ PadInput::kA, VK_RETURN, 0.0, false },
	{ PadInput::kStart, VK_RETURN, 0.0, false },
	{ PadInput::kB, VK_ESCAPE, 0.0, false },
};
const double kRepeatDelay = 0.4, kRepeatInterval = 0.12;
const float kMenuStick = 0.5f;

fearpc::PadLayout Layout()
{
	return g_vtPadLayout.GetFloat() >= 0.5f ? fearpc::PadLayout::kLeaner : fearpc::PadLayout::kNormal;
}

float Value(const fearpc::PadState& s, PadAction action)
{
	PadInput input = fearpc::LayoutInput(Layout(), action);
	return (input == PadInput::kCount || g_bHeld[(int)input]) ? 0.0f : s[input];
}

bool Down(const fearpc::PadState& s, PadAction action) { return Value(s, action) >= 0.5f; }

bool GameInFront()
{
	DWORD nProcess = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &nProcess);
	return nProcess == GetCurrentProcessId();
}

// A key press for the menus, delivered as CGameClientShell passes the keyboard's on.
void SendKey(int nKey)
{
	g_pInterfaceMgr->OnKeyDown(nKey, 0);
	g_pInterfaceMgr->OnKeyUp(nKey);
}

// A direction or button for the menus: the D-pad, or the left stick past halfway.
bool MenuDown(const fearpc::PadState& s, PadInput input)
{
	switch (input)
	{
		case PadInput::kDpadUp:		return s.Down(input) || s[PadInput::kLeftY] > kMenuStick;
		case PadInput::kDpadDown:	return s.Down(input) || s[PadInput::kLeftY] < -kMenuStick;
		case PadInput::kDpadLeft:	return s.Down(input) || s[PadInput::kLeftX] < -kMenuStick;
		case PadInput::kDpadRight:	return s.Down(input) || s[PadInput::kLeftX] > kMenuStick;
		default:					return s.Down(input);
	}
}

void UpdateMenuKeys(const fearpc::PadState& s)
{
	const double fNow = RealTimeTimer::Instance().GetTimerAccumulatedS();
	for (MenuKey& key : g_MenuKeys)
	{
		const bool bDown = MenuDown(s, key.input);
		if (bDown && !key.bDown)
		{
			SendKey(key.nKey);
			key.fNextRepeat = fNow + kRepeatDelay;
		}
		else if (bDown && fNow >= key.fNextRepeat && key.nKey != VK_RETURN && key.nKey != VK_ESCAPE)
		{
			SendKey(key.nKey);
			key.fNextRepeat = fNow + kRepeatInterval;
		}
		key.bDown = bDown;
	}
}

// Leaving play: whatever is down counts as already pressed, so the Start that opened the menu
// doesn't also pick its first item.
void HoldMenuKeys(const fearpc::PadState& s)
{
	for (MenuKey& key : g_MenuKeys)
		key.bDown = MenuDown(s, key.input);
}

// Entering play: the same for the game's buttons.
void HoldGameInputs(const fearpc::PadState& s)
{
	for (int i = (int)PadInput::kLeftTrigger; i < (int)PadInput::kCount; ++i)
		g_bHeld[i] = s.value[i] >= 0.5f;
}

void ReleaseGameInputs(const fearpc::PadState& s)
{
	for (int i = 0; i < (int)PadInput::kCount; ++i)
		g_bHeld[i] = g_bHeld[i] && s.value[i] >= 0.5f;
}

void Kick(float fLow, float fHigh)
{
	g_fKick[0] = LTMAX(g_fKick[0], LTMIN(fLow, 1.0f));
	g_fKick[1] = LTMAX(g_fKick[1], LTMIN(fHigh, 1.0f));
}

// Tells the pad, but only on a real change or when the last send is about to run out.
void SendRumble(float fLow, float fHigh)
{
	const double fNow = RealTimeTimer::Instance().GetTimerAccumulatedS();
	const bool bOff = fLow <= 0.01f && fHigh <= 0.01f;
	const bool bWasOff = g_fSent[0] <= 0.01f && g_fSent[1] <= 0.01f;
	if (bOff && bWasOff)
		return;
	const bool bChanged = fabsf(fLow - g_fSent[0]) >= kRumbleChange || fabsf(fHigh - g_fSent[1]) >= kRumbleChange || bOff;
	if (!bChanged && fNow - g_fSentAt < kRumbleRefresh)
		return;
	if (bOff)
		fearpc::PadRumble(0.0f, 0.0f, 0);
	else
		fearpc::PadRumble(fLow, fHigh, kRumbleDurationMs);
	g_fSent[0] = bOff ? 0.0f : fLow;
	g_fSent[1] = bOff ? 0.0f : fHigh;
	g_fSentAt = fNow;
}

void StopRumble()
{
	g_fKick[0] = g_fKick[1] = g_fFxMotors[0] = g_fFxMotors[1] = g_fShake = 0.0f;
	g_nLastHealth = -1;
	g_pLastWeapon = NULL;
	SendRumble(0.0f, 0.0f);
}

// Shots (the clip going down) and damage (health plus armour going down) start kicks; the
// motors run at the strongest of the kicks, the effects' motors and the camera shake.
void UpdateRumble(float fFrameTime)
{
	CClientWeapon* pWeapon = g_pClientWeaponMgr ? g_pClientWeaponMgr->GetCurrentClientWeapon() : NULL;
	const int32 nClip = pWeapon ? pWeapon->GetAmmoInClips() : -1;
	if (pWeapon && pWeapon == g_pLastWeapon && nClip >= 0 && nClip < g_nLastClip)
		Kick(0.3f, 0.6f);
	g_pLastWeapon = pWeapon;
	g_nLastClip = nClip;

	const int32 nHealth = g_pPlayerStats ? (int32)(g_pPlayerStats->GetHealth() + g_pPlayerStats->GetArmor()) : -1;
	if (g_nLastHealth >= 0 && nHealth >= 0 && nHealth < g_nLastHealth)
	{
		const float fDamage = (float)(g_nLastHealth - nHealth);
		Kick(0.35f + fDamage / 40.0f, fDamage / 60.0f);
	}
	g_nLastHealth = nHealth;

	const float fStrength = LTCLAMP(g_vtPadRumble.GetFloat(), 0.0f, 1.0f);
	const float fLow = LTMIN(LTMAX(LTMAX(g_fKick[0], g_fFxMotors[0]), g_fShake), 1.0f) * fStrength;
	const float fHigh = LTMIN(LTMAX(LTMAX(g_fKick[1], g_fFxMotors[1]), g_fShake * 0.5f), 1.0f) * fStrength;
	const float fFade = expf(-kKickFade * LTMAX(fFrameTime, 0.0f));
	g_fKick[0] *= fFade;
	g_fKick[1] *= fFade;
	SendRumble(fLow, fHigh);
}

// Something in front of you to use: a door, a switch, a weapon on the floor. The target
// manager says "can activate" even with nothing there, so the activation target decides.
bool CanUseTarget()
{
	CTargetMgr* pTargetMgr = g_pPlayerMgr ? g_pPlayerMgr->GetTargetMgr() : NULL;
	return pTargetMgr && pTargetMgr->CanActivateTarget() &&
		pTargetMgr->GetActivationData().m_hTarget != NULL &&
		!pTargetMgr->IsActivationType(MID_ACTIVATE_SURFACESND);
}

bool EnemyUnderCrosshair()
{
	CTargetMgr* pTargetMgr = g_pPlayerMgr ? g_pPlayerMgr->GetTargetMgr() : NULL;
	return pTargetMgr && pTargetMgr->GetEnemyTarget() != NULL;
}

fearpc::LookTuning Tuning()
{
	fearpc::LookTuning tuning;
	const int nRamp = (int)(g_vtPadLookRamp.GetFloat() + 0.5f);
	tuning.edge_ramp = (nRamp <= 0) ? fearpc::EdgeRamp::kConsole :
		(nRamp == 1) ? fearpc::EdgeRamp::kNoDelay :
		(nRamp == 2) ? fearpc::EdgeRamp::kInstant : fearpc::EdgeRamp::kOff;
	tuning.sensitivity = g_vtPadLookSensitivity.GetFloat();
	return tuning;
}

double CameraYawDegrees()
{
	const LTVector vForward = g_pPlayerMgr->GetPlayerCamera()->GetCameraRotation().Forward();
	return atan2(vForward.x, vForward.z) * 180.0 / MATH_PI;
}

void UpdateMeasure()
{
	Measure& m = g_Measure;
	const double fNow = RealTimeTimer::Instance().GetTimerAccumulatedS();
	if (!m.bActive)
	{
		const float fRequest = g_vtPadMeasure.GetFloat();
		if (fRequest < 0.5f || !g_pPlayerMgr || !g_pPlayerMgr->GetPlayerCamera())
			return;
		if (fRequest >= 2.0f && fNow - g_fPlayStart < fRequest)
			return;
		char szPath[MAX_PATH * 2];
		LTFileOperations::GetUserDirectory(szPath, LTARRAYSIZE(szPath));
		LTStrCat(szPath, "pad_measure.txt", LTARRAYSIZE(szPath));
		m = Measure();
		m.pFile = fopen(szPath, "w");
		if (m.pFile)
			fprintf(m.pFile, "# right stick deflection, measured yaw deg/s, expected deg/s (PadLookRamp %g, sensitivity %g)\n",
				g_vtPadLookRamp.GetFloat(), g_vtPadLookSensitivity.GetFloat());
		m.bActive = true;
		m.fStepStart = fNow;
		m.fLastRawYaw = CameraYawDegrees();
		g_Look.Reset();
		g_pLTClient->CPrint("PadMeasure: started, writing %s", szPath);
		return;
	}

	const double fRaw = CameraYawDegrees();
	double fDelta = fRaw - m.fLastRawYaw;
	while (fDelta > 180.0) fDelta -= 360.0;
	while (fDelta < -180.0) fDelta += 360.0;
	m.fYaw += fDelta;
	m.fLastRawYaw = fRaw;

	const double fElapsed = fNow - m.fStepStart;
	if (!m.bWindowOpen && fElapsed >= kMeasureSettle)
	{
		m.bWindowOpen = true;
		m.fWindowStartYaw = m.fYaw;
	}
	if (fElapsed < kMeasureSettle + kMeasureWindow)
		return;

	const float fDeflection = kMeasureDeflections[m.nStep];
	const double fRate = (m.fYaw - m.fWindowStartYaw) / kMeasureWindow;
	const fearpc::LookTuning tuning = Tuning();
	const float fExpected = fearpc::CurveRate(tuning, fDeflection) * tuning.sensitivity;
	if (m.pFile)
		fprintf(m.pFile, "%.2f %.2f %.2f\n", fDeflection, fRate, fExpected);
	g_pLTClient->CPrint("PadMeasure: %.2f -> %.1f deg/s (360: %.1f)", fDeflection, fRate, fExpected);

	if (++m.nStep >= kMeasureSteps)
	{
		if (m.pFile)
			fclose(m.pFile);
		m = Measure();
		g_vtPadMeasure.SetFloat(0.0f);
		g_Look.Reset();
		return;
	}
	m.fStepStart = fNow;
	m.bWindowOpen = false;
	g_Look.Reset();
}

}  // namespace

void FearPadFrame()
{
	if (!g_bStarted)
	{
		g_vtPadEnabled.Init(g_pLTClient, "PadEnabled", NULL, 1.0f);
		g_vtPadLayout.Init(g_pLTClient, "PadLayout", NULL, 0.0f);
		g_vtPadLookRamp.Init(g_pLTClient, "PadLookRamp", NULL, 3.0f);
		g_vtPadLookSensitivity.Init(g_pLTClient, "PadLookSensitivity", NULL, 1.0f);
		g_vtPadSoftLock.Init(g_pLTClient, "PadSoftLock", NULL, 1.0f);
		g_vtPadMeasure.Init(g_pLTClient, "PadMeasure", NULL, 0.0f);
		g_vtPadRumble.Init(g_pLTClient, "PadRumble", NULL, 1.0f);
		if (!fearpc::PadInit())
			g_pLTClient->CPrint("Pads: SDL could not start; controllers disabled.");
		g_bStarted = true;
	}

	g_LastState = g_State;
	const fearpc::PadState& polled = fearpc::PadUpdate();
	g_State = (g_vtPadEnabled.GetFloat() >= 0.5f && GameInFront()) ? polled : fearpc::PadState();

	const bool bWasPlaying = g_bPlaying;
	g_bPlaying = g_pInterfaceMgr && g_pInterfaceMgr->GetGameState() == GS_PLAYING;
	if (!g_bPlaying)
	{
		StopRumble();
		if (bWasPlaying)
			HoldMenuKeys(g_State);
		g_Look.Reset();
		UpdateMenuKeys(g_State);
		return;
	}
	if (!bWasPlaying)
	{
		HoldGameInputs(g_State);
		g_fPlayStart = RealTimeTimer::Instance().GetTimerAccumulatedS();
	}
	ReleaseGameInputs(g_State);
	UpdateMeasure();

	// Start opens the menu.
	if (!g_bHeld[(int)PadInput::kStart] && g_State.Down(PadInput::kStart) && !g_LastState.Down(PadInput::kStart))
		SendKey(VK_ESCAPE);

	// X uses what's in front of you, or reloads; the choice holds until it is let go, so holding
	// it on a weapon picks the weapon up.
	if (Down(g_State, PadAction::kUseOrReload) && !Down(g_LastState, PadAction::kUseOrReload))
		g_nUseOrReload = CanUseTarget() ? COMMAND_ID_ACTIVATE : COMMAND_ID_RELOAD;

	if (g_State.connected)
		UpdateRumble(RealTimeTimer::Instance().GetTimerElapsedS());
	else
		StopRumble();
}

void FearPadCameraEffects(const LTRigidTransform& tEffect, const float* pMotors)
{
	g_fFxMotors[0] = pMotors[0];
	g_fFxMotors[1] = pMotors[1];
	// The shake's size: how far the view is pushed (game units) and turned (the quaternion's
	// angle), scaled so a nearby grenade reads near full strength.
	const float fAngle = 2.0f * acosf(LTMIN(fabsf(tEffect.m_rRot.m_Quat[LTRotation::QW]), 1.0f));
	g_fShake = LTMIN(tEffect.m_vPos.Mag() / 6.0f + RAD2DEG(fAngle) / 2.0f, 1.0f);
}

bool FearPadCommandOn(uint32 nCommand)
{
	if (!g_bPlaying || !g_State.connected)
		return false;
	switch (nCommand)
	{
		case COMMAND_ID_FIRING:			return Down(g_State, PadAction::kFire);
		case COMMAND_ID_THROW_GRENADE:	return Down(g_State, PadAction::kThrowGrenade);
		case COMMAND_ID_NEXT_WEAPON:	return Down(g_State, PadAction::kNextWeapon);
		case COMMAND_ID_SLOWMO:			return Down(g_State, PadAction::kSlowMo);
		case COMMAND_ID_JUMP:			return Down(g_State, PadAction::kJump);
		case COMMAND_ID_ALT_FIRING:		return Down(g_State, PadAction::kMelee);
		case COMMAND_ID_ACTIVATE:
		case COMMAND_ID_RELOAD:			return nCommand == g_nUseOrReload && Down(g_State, PadAction::kUseOrReload);
		case COMMAND_ID_MEDKIT:			return Down(g_State, PadAction::kMedkit);
		case COMMAND_ID_NEXT_GRENADE:	return Down(g_State, PadAction::kNextGrenade);
		case COMMAND_ID_FLASHLIGHT:		return Down(g_State, PadAction::kFlashlight);
		case COMMAND_ID_LEAN_LEFT:		return Down(g_State, PadAction::kLeanLeft);
		case COMMAND_ID_LEAN_RIGHT:		return Down(g_State, PadAction::kLeanRight);
		case COMMAND_ID_DUCK:			return Down(g_State, PadAction::kCrouch);
		case COMMAND_ID_ZOOM_IN:		return Down(g_State, PadAction::kZoom);
		case COMMAND_ID_MISSION:		return Down(g_State, PadAction::kObjectives);
		default:						return false;
	}
}

float FearPadCommandValue(uint32 nCommand)
{
	if (!g_bPlaying || !g_State.connected)
		return 0.0f;
	switch (nCommand)
	{
		case COMMAND_ID_FORWARD_AXIS:	return Value(g_State, PadAction::kMoveY);
		case COMMAND_ID_STRAFE_AXIS:	return Value(g_State, PadAction::kMoveX);
		default:						return 0.0f;
	}
}

bool FearPadLook(float fFrameTime, float* pYaw, float* pPitch)
{
	*pYaw = *pPitch = 0.0f;
	if (!g_bPlaying)
		return false;

	float fStickX = Value(g_State, PadAction::kLookX), fStickY = Value(g_State, PadAction::kLookY);
	bool bSoftLock = g_vtPadSoftLock.GetFloat() >= 0.5f && EnemyUnderCrosshair();
	if (g_Measure.bActive)
	{
		fStickX = kMeasureDeflections[g_Measure.nStep];
		fStickY = 0.0f;
		bSoftLock = false;
	}
	else if (!g_State.connected)
	{
		return false;
	}

	float fYawDegrees = 0.0f, fPitchDegrees = 0.0f;
	g_Look.Update(Tuning(), fStickX, fStickY, fFrameTime, bSoftLock, &fYawDegrees, &fPitchDegrees);
	*pYaw = DEG2RAD(fYawDegrees);
	*pPitch = DEG2RAD(fPitchDegrees);
	return true;
}
