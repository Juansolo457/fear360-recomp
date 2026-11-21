// F.E.A.R. PC - the Controller screen (FEAR_PC_PAD): Options > Controls > Joystick.
//
// With the game controller read through SDL (fear_pad.h), the PC's DirectInput joystick page
// has nothing left to set, so its screen (CScreenJoystick, with its place in the menus and its
// layout record in the game database) shows the 360 controller's settings instead. The original
// implementation in ScreenJoystick.cpp is compiled out under FEAR_PC_PAD.
#include "stdafx.h"

#include "ScreenJoystick.h"
#include "ScreenCommands.h"
#include "ScreenMgr.h"
#include "fear_pad.h"
#include "fearpc/layout.h"

namespace
{
	// The values the controls edit; read when the screen opens, applied and saved when it closes.
	bool g_bEnabled = true;
	bool g_bSoftLock = true;
	uint8 g_nLayout = 0;
	uint8 g_nLook = 0;		// index into kLook
	int g_nSensitivity = 100;
	int g_nRumble = 100;
	uint8 g_nGlyphs = 1;	// PadGlyphs

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

	// Under the settings, what each button does in the chosen layout: four actions a row, each
	// its button (drawn by CLTGUIString from the 360's button character) and its name. Four rows
	// is what fits under the settings at 720p.
	struct LegendEntry
	{
		fearpc::PadAction eAction;
		const wchar_t* szName;
	};
	const int kLegendColumns = 4;
	const LegendEntry kLegend[][kLegendColumns] = {
		{ { fearpc::PadAction::kFire, L"Fire" },				{ fearpc::PadAction::kThrowGrenade, L"Throw grenade" },
		  { fearpc::PadAction::kJump, L"Jump" },				{ fearpc::PadAction::kMelee, L"Melee" } },
		{ { fearpc::PadAction::kUseOrReload, L"Use, reload" },	{ fearpc::PadAction::kMedkit, L"Medkit" },
		  { fearpc::PadAction::kSlowMo, L"Slow-mo" },			{ fearpc::PadAction::kNextWeapon, L"Next weapon" } },
		{ { fearpc::PadAction::kNextGrenade, L"Next grenade" },	{ fearpc::PadAction::kFlashlight, L"Flashlight" },
		  { fearpc::PadAction::kLeanLeft, L"Lean left" },		{ fearpc::PadAction::kLeanRight, L"Lean right" } },
		{ { fearpc::PadAction::kCrouch, L"Crouch" },			{ fearpc::PadAction::kZoom, L"Zoom" },
		  { fearpc::PadAction::kObjectives, L"Objectives" },	{ fearpc::PadAction::kMenu, L"Menu" } },
	};
	const int kLegendRows = LTARRAYSIZE(kLegend);
	CLTGUIColumnCtrl* g_pLegend[kLegendRows] = {};
	const uint32 kCmdLayout = CMD_CUSTOM + 1;

	void UpdateLegend()
	{
		const fearpc::PadLayout eLayout = g_nLayout ? fearpc::PadLayout::kLeaner : fearpc::PadLayout::kNormal;
		for (int nRow = 0; nRow < kLegendRows; ++nRow)
		{
			if (!g_pLegend[nRow])
				continue;
			for (int nSide = 0; nSide < kLegendColumns; ++nSide)
			{
				wchar_t szGlyph[2] = { (wchar_t)fearpc::ButtonGlyph(fearpc::LayoutInput(eLayout, kLegend[nRow][nSide].eAction)), 0 };
				g_pLegend[nRow]->SetString((uint8)(nSide * 2), szGlyph);
			}
		}
	}

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
	ccs.nCommandID = kCmdLayout;	// sent when the choice changes: the legend follows it
	ccs.pCommandHandler = this;
	CLTGUICycleCtrl* pCycle = AddCycle(L"Button layout", ccs);
	pCycle->AddString(L"Normal");
	pCycle->AddString(L"Lean");
	ccs.nCommandID = 0;
	ccs.pCommandHandler = NULL;

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
	// PadSoftLock. Not called aim assist: the 360's assist pulls toward the target, and this slows
	// the turn instead.
	pToggle = AddToggle(L"Slow turn on enemies", tcs);
	pToggle->SetOnString(L"On");
	pToggle->SetOffString(L"Off");

	scs.pnValue = &g_nRumble;
	scs.nMin = 0;
	scs.nMax = 100;
	scs.nIncrement = 10;
	AddSlider(L"Vibration", scs);

	ccs.pnValue = &g_nGlyphs;
	pCycle = AddCycle(L"Button prompts", ccs);
	pCycle->AddString(L"Keyboard");
	pCycle->AddString(L"Automatic");
	pCycle->AddString(L"Xbox");
	pCycle->AddString(L"PlayStation");

	// The legend, in smaller text so the screen holds it all.
	const uint32 nLegendHeight = LTMAX(nHeight * 2 / 3, 12);
	// Wider than the settings rows: the longest name ("Throw grenade") needs about 135 at this size.
	const uint32 nLegendWidth = (uint32)(nGap + nWidth) + 160;
	const uint32 nGlyphWidth = nLegendHeight * 2, nNameWidth = nLegendWidth / kLegendColumns - nGlyphWidth;
	CLTGUICtrl_create lcs;
	lcs.rnBaseRect.m_vMin.Init();
	lcs.rnBaseRect.m_vMax = LTVector2n((int32)nLegendWidth, (int32)nLegendHeight);
	for (int nRow = 0; nRow < kLegendRows; ++nRow)
	{
		CLTGUIColumnCtrl* pRow = AddColumnCtrl(lcs, false, NULL, nLegendHeight);
		for (int nSide = 0; nSide < kLegendColumns; ++nSide)
		{
			pRow->AddColumn(L"", nGlyphWidth);
			pRow->AddColumn(kLegend[nRow][nSide].szName, nNameWidth);
		}
		pRow->Enable(false);
		g_pLegend[nRow] = pRow;
	}

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
		g_nGlyphs = s.nGlyphs;
		g_nLook = 0;
		for (uint8 i = 0; i < LTARRAYSIZE(kLook); ++i)
			if (kLook[i].nRamp == s.nLookRamp)
				g_nLook = i;
		UpdateData(false);
		UpdateLegend();
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
		s.nGlyphs = g_nGlyphs;
		FearPadSetSettings(s);
	}
	CBaseScreen::OnFocus(bFocus);
}

uint32 CScreenJoystick::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	if (dwCommand == kCmdLayout)
	{
		UpdateData();
		UpdateLegend();
		return 1;
	}
	return CBaseScreen::OnCommand(dwCommand, dwParam1, dwParam2);
}
