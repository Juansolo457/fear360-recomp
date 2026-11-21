// F.E.A.R. PC - the Instant Action screens (FEAR_PC_IA): the map screen under the main menu
// and the summary of a run.
//
// See fear_ia.h for the mode. The PC's string database has none of the 360's Instant Action
// strings, so the text is here, in English, as the 360 has it.
//
// Both screens take the Joystick screen's layout record (ScreenEnum.h names them
// "ScreenJoystick"): the game database has no record for them, and that one is known to hold
// rows of a label and a value this wide, since the Controller screen uses it
// (fear_pad_screen.cpp).
//
// The summary's rows are the 360's, in two columns so they fit under the title in smaller
// text: the result is the title, then time remaining, enemies killed and how, combo kills, hit
// rate, favorite weapon, armor and medkits left, the booster line and the score, with the
// map's best beside it.
#include "stdafx.h"

#include "fear_ia.h"

#include "BaseScreen.h"
#include "GameClientShell.h"
#include "InterfaceMgr.h"
#include "ScreenCommands.h"
#include "ScreenMgr.h"
#include "WeaponDB.h"

namespace
{
	const uint32 kCmdMap = CMD_CUSTOM + 1;		// + the map
	const uint32 kCmdRetry = CMD_CUSTOM + 1;	// the summary's own
	const uint32 kCmdMainMenu = CMD_CUSTOM + 2;

	// The difficulty the map screen's cycle edits: a GameDifficulty.
	uint8 g_nDifficulty = GD_NORMAL;

	// The summary's rows: a label and a value on the left, and on the right.
	enum SummaryRow
	{
		kRowTime,		// Time remaining		| Hit rate
		kRowKills,		// Enemies killed		| Favorite weapon
		kRowMelee,		// Via melee			| Armor remaining
		kRowFirearm,	// Via fire-arm			| Medkits remaining
		kRowExplosive,	// Via explosive		| You found the Booster!
		kRowCombo,		// Total Combo Kills	|
		kRowScore,		// Score				| Best
		kSummaryRows
	};
	const wchar_t* const kSummaryLabels[kSummaryRows][2] = {
		{ L"Time remaining:",		L"Hit rate:" },
		{ L"Enemies killed:",		L"Favorite weapon:" },
		{ L"Via melee:",			L"Armor remaining:" },
		{ L"Via fire-arm:",			L"Medkits remaining:" },
		{ L"Via explosive:",		L"" },
		{ L"Total Combo Kills:",	L"" },
		{ L"Score:",				L"Best:" },
	};
	// The columns of a row.
	enum
	{
		kColLeftLabel,
		kColLeftValue,
		kColRightLabel,
		kColRightValue,
	};

	const wchar_t* WeaponName(uint8 nWeapon)
	{
		if (nWeapon == kFearIANoWeapon)
			return L"-";
		HWEAPON hWeapon = g_pWeaponDB->GetWeaponRecord(nWeapon);
		if (!hWeapon)
			return L"-";
		HWEAPONDATA hWpnData = g_pWeaponDB->GetWeaponData(hWeapon, !USE_AI_DATA);
		const wchar_t* wszName = g_pWeaponDB->GetWStringFromId(hWpnData, WDB_WEAPON_nShortNameId);
		return wszName && wszName[0] ? wszName : L"-";
	}
}

//
// The map screen: the maps that are installed, each with its best score, and the difficulty.
//

class CScreenInstantAction : public CBaseScreen
{
public:
	CScreenInstantAction();

	virtual bool	Build();
	virtual void	OnFocus(bool bFocus);

protected:
	virtual uint32	OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

	CLTGUIColumnCtrl*	m_pMaps[kFearIAMaps];
};

CScreenInstantAction::CScreenInstantAction()
{
	for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
		m_pMaps[nMap] = NULL;
}

bool CScreenInstantAction::Build()
{
	CreateTitle(L"Instant action");

	// As the Controller screen: this layout record has no column widths of its own.
	const int nGap = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 0), 300);
	const int nWidth = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 1), 260);
	const int nHeight = g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenFontSize);
	const LTVector2n vRow(nGap + nWidth, nHeight);
	// "Map 3 - Construction" is wider than the label column.
	const int nNameWidth = nGap + 60;

	CLTGUICtrl_create cs;
	cs.rnBaseRect.m_vMin.Init();
	cs.rnBaseRect.m_vMax = vRow;
	cs.pCommandHandler = this;
	for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
	{
		if (!FearIAMapExists(nMap))
			continue;
		cs.nCommandID = kCmdMap + nMap;
		CLTGUIColumnCtrl* pRow = AddColumnCtrl(cs);
		if (!pRow)
			continue;
		pRow->AddColumn(FearIAMapName(nMap), nNameWidth);
		pRow->AddColumn(L"", nGap + nWidth - nNameWidth);
		m_pMaps[nMap] = pRow;
	}

	CLTGUICycleCtrl_create ccs;
	ccs.rnBaseRect.m_vMin.Init();
	ccs.rnBaseRect.m_vMax = vRow;
	ccs.nHeaderWidth = nGap;
	ccs.pnValue = &g_nDifficulty;
	CLTGUICycleCtrl* pCycle = AddCycle(L"Difficulty", ccs);
	// The game's four, by its names for them (GD_EASY to GD_VERYHARD).
	pCycle->AddString(L"Low");
	pCycle->AddString(L"Moderate");
	pCycle->AddString(L"High");
	pCycle->AddString(L"Extreme");

	if (!CBaseScreen::Build())
		return false;
	UseBack(true);
	return true;
}

void CScreenInstantAction::OnFocus(bool bFocus)
{
	if (bFocus)
	{
		g_nDifficulty = (uint8)LTCLAMP((int)g_pGameClientShell->GetDifficulty(), (int)GD_EASY, (int)GD_VERYHARD);
		for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
		{
			if (!m_pMaps[nMap])
				continue;
			wchar_t szBest[32] = L"";
			const float fBest = FearIABest(nMap);
			if (fBest >= 0.0f)
				LTSNPrintF(szBest, LTARRAYSIZE(szBest), L"Best: %.1f", fBest);
			m_pMaps[nMap]->SetString(1, szBest);
		}
		UpdateData(false);
	}
	else
	{
		UpdateData();
	}
	CBaseScreen::OnFocus(bFocus);
}

uint32 CScreenInstantAction::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	if (dwCommand >= kCmdMap && dwCommand < kCmdMap + kFearIAMaps)
	{
		UpdateData();
		FearIAStart((int)(dwCommand - kCmdMap), g_nDifficulty);
		return 1;
	}
	return CBaseScreen::OnCommand(dwCommand, dwParam1, dwParam2);
}

//
// The summary screen: shown when a run ends, in place of the end of the level or the failure
// screen. Retry restarts the map; the other item, and Escape, go to the main menu.
//

class CScreenInstantActionSummary : public CBaseScreen
{
public:
	CScreenInstantActionSummary();

	virtual bool	Build();
	virtual void	OnFocus(bool bFocus);
	virtual void	Escape();

protected:
	virtual uint32	OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2);

	void	SetValue(int nRow, uint8 nColumn, const wchar_t* szText);

	CLTGUIColumnCtrl*	m_pRows[kSummaryRows];
	CLTGUITextCtrl*		m_pRetry;
};

CScreenInstantActionSummary::CScreenInstantActionSummary() :
	m_pRetry(NULL)
{
	for (int nRow = 0; nRow < kSummaryRows; ++nRow)
		m_pRows[nRow] = NULL;
}

bool CScreenInstantActionSummary::Build()
{
	const int nGap = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 0), 300);
	const int nWidth = LTMAX(g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenColumnWidths, 1), 260);
	const int nHeight = g_pLayoutDB->GetInt32(m_hLayout, LDB_ScreenFontSize);

	// The rows in smaller text and wider than a settings row, as the Controller screen's
	// legend; the score's row in the menu's own size.
	const uint32 nRowHeight = LTMAX(nHeight * 3 / 4, 12);
	const uint32 nRowWidth = (uint32)(nGap + nWidth) + 160;
	const uint32 nLeftLabel = nRowWidth * 27 / 100, nLeftValue = nRowWidth * 17 / 100;
	const uint32 nRightLabel = nRowWidth * 28 / 100, nRightValue = nRowWidth - nLeftLabel - nLeftValue - nRightLabel;

	CLTGUICtrl_create cs;
	cs.rnBaseRect.m_vMin.Init();
	for (int nRow = 0; nRow < kSummaryRows; ++nRow)
	{
		const uint32 nSize = nRow == kRowScore ? (uint32)nHeight : nRowHeight;
		cs.rnBaseRect.m_vMax = LTVector2n((int32)nRowWidth, (int32)nSize);
		CLTGUIColumnCtrl* pRow = AddColumnCtrl(cs, false, NULL, nSize);
		if (!pRow)
			continue;
		pRow->AddColumn(kSummaryLabels[nRow][0], nLeftLabel);
		pRow->AddColumn(L"", nLeftValue);
		pRow->AddColumn(kSummaryLabels[nRow][1], nRightLabel);
		pRow->AddColumn(L"", nRightValue);
		// Not selectable, but drawn as the menu's text rather than greyed out.
		pRow->Enable(false);
		pRow->SetColors(m_SelectedColor, m_NonSelectedColor, m_NonSelectedColor);
		m_pRows[nRow] = pRow;
	}

	cs.rnBaseRect.m_vMax = LTVector2n(nGap + nWidth, nHeight);
	cs.nCommandID = kCmdRetry;
	m_pRetry = AddTextItem(L"Retry", cs);
	cs.nCommandID = kCmdMainMenu;
	AddTextItem(L"Main menu", cs);

	if (!CBaseScreen::Build())
		return false;
	UseBack(false);
	return true;
}

void CScreenInstantActionSummary::SetValue(int nRow, uint8 nColumn, const wchar_t* szText)
{
	if (m_pRows[nRow])
		m_pRows[nRow]->SetString(nColumn, szText);
}

void CScreenInstantActionSummary::OnFocus(bool bFocus)
{
	if (bFocus)
	{
		const FearIASummary& s = FearIAClientSummary();
		CreateTitle(s.bCompleted ? L"Mission Completed!" : L"Mission Failed!");

		wchar_t szText[64];

		// h:mm:ss, as the 360 shows it.
		const int nSeconds = (int)FearIAClientTimeRemaining();
		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d:%02d:%02d", nSeconds / 3600, nSeconds / 60 % 60, nSeconds % 60);
		SetValue(kRowTime, kColLeftValue, szText);
		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%.1f%%", FearIAClientHitRate() * 100.0f);
		SetValue(kRowTime, kColRightValue, szText);

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.nKills);
		SetValue(kRowKills, kColLeftValue, szText);
		SetValue(kRowKills, kColRightValue, WeaponName(s.nFavoriteWeapon));

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.nMeleeKills);
		SetValue(kRowMelee, kColLeftValue, szText);
		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.fArmor);
		SetValue(kRowMelee, kColRightValue, szText);

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.nFirearmKills);
		SetValue(kRowFirearm, kColLeftValue, szText);
		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.nMedkits);
		SetValue(kRowFirearm, kColRightValue, szText);

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", (int)s.nExplosiveKills);
		SetValue(kRowExplosive, kColLeftValue, szText);
		SetValue(kRowExplosive, kColRightLabel, s.nBoosters ? L"You found the Booster!" : L"");

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%d", FearIAClientComboKills());
		SetValue(kRowCombo, kColLeftValue, szText);

		LTSNPrintF(szText, LTARRAYSIZE(szText), L"%.1f", FearIAClientScore());
		SetValue(kRowScore, kColLeftValue, szText);
		const float fBest = FearIABest(FearIAMap());
		if (fBest >= 0.0f)
			LTSNPrintF(szText, LTARRAYSIZE(szText), FearIAClientNewBest() ? L"%.1f (new)" : L"%.1f", fBest);
		else
			szText[0] = 0;
		SetValue(kRowScore, kColRightValue, szText);

		if (m_pRetry)
			SetSelection(GetIndex(m_pRetry));
	}
	CBaseScreen::OnFocus(bFocus);
}

void CScreenInstantActionSummary::Escape()
{
	// To the main menu, which disconnects from the level and ends the run (FearIAClear).
	m_pScreenMgr->ClearHistory();
	m_pScreenMgr->SetCurrentScreen(SCREEN_ID_MAIN);
}

uint32 CScreenInstantActionSummary::OnCommand(uint32 dwCommand, uint32 dwParam1, uint32 dwParam2)
{
	switch (dwCommand)
	{
	case kCmdRetry:
		if (!FearIAStart(FearIAMap(), (int)g_pGameClientShell->GetDifficulty()))
			Escape();
		return 1;
	case kCmdMainMenu:
		Escape();
		return 1;
	default:
		return CBaseScreen::OnCommand(dwCommand, dwParam1, dwParam2);
	}
}

CBaseScreen* FearIACreateScreen(int nScreenID)
{
	switch (nScreenID)
	{
	case SCREEN_ID_INSTANT_ACTION:
		return debug_new(CScreenInstantAction);
	case SCREEN_ID_INSTANT_ACTION_SUMMARY:
		return debug_new(CScreenInstantActionSummary);
	default:
		return NULL;
	}
}
