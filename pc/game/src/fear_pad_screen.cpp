// F.E.A.R. PC - the Controller screen (FEAR_PC_PAD): Options > Controls > Joystick.
//
// With the game controller read through SDL (fear_pad.h), the PC's DirectInput joystick page
// has nothing left to set, so its screen (CScreenJoystick, with its place in the menus and its
// layout record in the game database) shows the 360 controller's settings instead. The original
// implementation in ScreenJoystick.cpp is compiled out under FEAR_PC_PAD.
#include "stdafx.h"

#include "ScreenJoystick.h"
#include "ScreenMgr.h"
#include "fear_pad.h"

namespace
{
	// The values the controls edit; read when the screen opens, applied and saved when it closes.
	bool g_bEnabled = true;
	bool g_bSoftLock = true;
	uint8 g_nLayout = 0;
	uint8 g_nLook = 0;		// index into kLook
	int g_nSensitivity = 100;
	int g_nRumble = 100;

	// Look acceleration, named as in the recomp's settings, and the PadLookRamp of each.
	struct LookChoice
	{
		const wchar_t* szLabel;
		uint8 nRamp;
	};
	const LookChoice kLook[] = {
		{ L"Off", 3 },
		{ L"Xbox 360", 0 },
		{ L"Immediate", 1 },
		{ L"Full speed", 2 },
	};

	const wchar_t* PercentText(int nPos, void* /*pUserData*/)
	{
		static wchar_t szText[16];
		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d%%", nPos);
		return szText;
	}
}

CScreenJoystick::CScreenJoystick() :
	m_nJoystickDevice(0),
	m_nFirstAxis(0)
{
}

CScreenJoystick::~CScreenJoystick()
{
}

bool CScreenJoystick::Build()
{
	CreateTitle(L"Controller");

	// The label column fits "Look acceleration" in the menu font; this screen's layout record
	// has no column widths of its own.
	int nGap = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 0), 300);
	int nWidth = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 1), 260);
	const int nHeight = g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenFontSize);
	const LTVector2n vRow(nGap + nWidth, nHeight);

	CLTGUIToggle_create tcs;
	tcs.rnBaseRect.m_vMin.Init();
	tcs.rnBaseRect.m_vMax = vRow;
	tcs.nHeaderWidth = nGap;

	CLTGUICycleCtrl_create ccs;
	ccs.rnBaseRect.m_vMin.Init();
	ccs.rnBaseRect.m_vMax = vRow;
	ccs.nHeaderWidth = nGap;

	CLTGUISlider_create scs;
	scs.rnBaseRect.m_vMin.Init();
	scs.rnBaseRect.m_vMax = vRow;
	scs.nBarOffset = nGap;
	scs.pnTextCallback = PercentText;
	scs.nDisplayWidth = 80;

	tcs.pbValue = &g_bEnabled;
	CLTGUIToggle* pToggle = AddToggle(L"Controller", tcs);
	pToggle->SetOnString(L"On");
	pToggle->SetOffString(L"Off");

	ccs.pnValue = &g_nLayout;
	CLTGUICycleCtrl* pCycle = AddCycle(L"Button layout", ccs);
	pCycle->AddString(L"Normal");
	pCycle->AddString(L"Lean");

	scs.pnValue = &g_nSensitivity;
	scs.nMin = 25;
	scs.nMax = 200;
	scs.nIncrement = 5;
	AddSlider(L"Look sensitivity", scs);

	ccs.pnValue = &g_nLook;
	pCycle = AddCycle(L"Look acceleration", ccs);
	for (const LookChoice& choice : kLook)
		pCycle->AddString(choice.szLabel);

	tcs.pbValue = &g_bSoftLock;
	pToggle = AddToggle(L"Aim assist", tcs);
	pToggle->SetOnString(L"On");
	pToggle->SetOffString(L"Off");

	scs.pnValue = &g_nRumble;
	scs.nMin = 0;
	scs.nMax = 100;
	scs.nIncrement = 10;
	AddSlider(L"Vibration", scs);

	if (!CBaseScreen::Build())
		return false;
	UseBack(true, true);
	return true;
}

void CScreenJoystick::OnFocus(bool bFocus)
{
	if (bFocus)
	{
		const FearPadSettings s = FearPadGetSettings();
		g_bEnabled = s.bEnabled;
		g_nLayout = s.nLayout;
		g_nSensitivity = s.nSensitivity;
		g_bSoftLock = s.bSoftLock;
		g_nRumble = s.nRumble;
		g_nLook = 0;
		for (uint8 i = 0; i < LTARRAYSIZE(kLook); ++i)
			if (kLook[i].nRamp == s.nLookRamp)
				g_nLook = i;
		UpdateData(false);
	}
	else
	{
		UpdateData();
		FearPadSettings s;
		s.bEnabled = g_bEnabled;
		s.nLayout = g_nLayout;
		s.nLookRamp = kLook[g_nLook < LTARRAYSIZE(kLook) ? g_nLook : 0].nRamp;
		s.nSensitivity = g_nSensitivity;
		s.bSoftLock = g_bSoftLock;
		s.nRumble = g_nRumble;
		FearPadSetSettings(s);
	}
	CBaseScreen::OnFocus(bFocus);
}

uint32 CScreenJoystick::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	return CBaseScreen::OnCommand(dwCommand, dwParam1, dwParam2);
}
