// F.E.A.R. PC - Instant Action on the client (FEAR_PC_IA): the run's flag, the clock, the
// summary and the best scores.
//
// See fear_ia.h for the mode and the order things happen in. The screens are in
// fear_ia_screens.cpp.
//
// The clock is the HUD's main timer (CHUDTimerMain, the multiplayer round clock), set to end
// 900 s of game time after play first starts. Game time is the simulation's, which is what the
// server measures the run with, so the clock and "Time remaining" agree; it also means the
// clock slows with slow-mo, as everything on that timer does. At zero the PC's timer stops
// drawing where the 360's turns red; the run goes on either way.
//
// Best scores: fearpc_ia.txt in the user directory, a line "MapN score" for each map that has
// one. The best is over every run of the map, failed ones included, as the 360 submits both.
#include "stdafx.h"

#include "fear_ia.h"

#include <stdio.h>

#include "ClientConnectionMgr.h"
#include "EngineTimer.h"
#include "GameClientShell.h"
#include "HUDMgr.h"
#include "HUDTimer.h"
#include "InterfaceMgr.h"
#include "MissionMgr.h"
#include "MsgIDs.h"
#include "PlayerMgr.h"
#include "ScreenMgr.h"
#include "ltfileoperations.h"
#include "fearpc/instant_action.h"

namespace
{
	const wchar_t* const kMapNames[kFearIAMaps] = {
		L"Map 1 - Distribution",
		L"Map 2 - Rooftop",
		L"Map 3 - Construction",
		L"Map 4 - Vault",
	};

	// How long a failed run waits for its summary before the game's own failure screen is
	// shown instead, seconds of real time. The server is in this process: it answers in a frame.
	const double kSummaryWait = 3.0;

	bool g_bPending = false;	// FearIAStart is starting a level: the next new game is a run
	bool g_bActive = false;		// a run is loading, being played or summed up
	bool g_bBegun = false;		// play has started: the clock is set, the server is counting
	bool g_bFailed = false;		// the mission failed and the summary was asked for
	double g_fFailedTime = 0.0;
	int g_nMap = 0;

	FearIASummary g_Summary = {};
	float g_fScore = 0.0f;
	float g_fTimeRemaining = 0.0f;
	float g_fHitRate = 0.0f;
	int g_nComboKills = 0;
	bool g_bNewBest = false;

	// Negative: no score yet.
	float g_fBest[kFearIAMaps] = { -1.0f, -1.0f, -1.0f, -1.0f };
	bool g_bBestLoaded = false;

	void WorldName(int nMap, char* szName, uint32 nSize)
	{
		LTSNPrintF(szName, nSize, "Worlds\\Release\\InstantAction%d", nMap + 1);
	}

	void BestPath(char* szPath, uint32 nSize)
	{
		LTFileOperations::GetUserDirectory(szPath, nSize);
		LTStrCat(szPath, "fearpc_ia.txt", nSize);
	}

	void LoadBest()
	{
		if (g_bBestLoaded)
			return;
		g_bBestLoaded = true;
		char szPath[MAX_PATH * 2];
		BestPath(szPath, LTARRAYSIZE(szPath));
		FILE* pFile = fopen(szPath, "r");
		if (!pFile)
			return;
		char szName[64];
		float fValue;
		while (fscanf(pFile, "%63s %f", szName, &fValue) == 2)
		{
			for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
			{
				char szMap[16];
				LTSNPrintF(szMap, LTARRAYSIZE(szMap), "Map%d", nMap + 1);
				if (LTStrIEquals(szName, szMap) && fValue >= 0.0f)
					g_fBest[nMap] = fValue;
			}
		}
		fclose(pFile);
	}

	void SaveBest()
	{
		char szPath[MAX_PATH * 2];
		BestPath(szPath, LTARRAYSIZE(szPath));
		FILE* pFile = fopen(szPath, "w");
		if (!pFile)
			return;
		for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
			if (g_fBest[nMap] >= 0.0f)
				fprintf(pFile, "Map%d %.1f\n", nMap + 1, g_fBest[nMap]);
		fclose(pFile);
	}

	void SendIA(FearIAMsg eMsg, bool bMap)
	{
		CAutoMessage cMsg;
		cMsg.Writeuint8(MID_INSTANT_ACTION);
		cMsg.Writeuint8((uint8)eMsg);
		if (bMap)
			cMsg.Writeuint8((uint8)g_nMap);
		g_pLTClient->SendToServer(cMsg.Read(), MESSAGE_GUARANTEED);
	}

	// The score and the rows worked out with it, from pc/core.
	void Derive(const FearIASummary& s)
	{
		int aCombos[kFearIAMaxCombos];
		for (int n = 0; n < s.nCombos; ++n)
			aCombos[n] = s.aCombos[n];
		fearpc::InstantActionRun run;
		run.completed = s.bCompleted;
		run.seconds = s.fSeconds;
		run.shots = (int)s.nShots;
		run.hits = (int)s.nHits;
		run.enemy_kills = (int)s.nKills;
		run.medkits = s.nMedkits;
		run.armor = s.fArmor;
		run.boosters = s.nBoosters;
		run.combos = aCombos;
		run.combo_count = s.nCombos;
		g_fScore = fearpc::InstantActionScore(run);
		g_fTimeRemaining = fearpc::InstantActionTimeRemaining(run);
		g_fHitRate = fearpc::InstantActionHitRate(run);
		g_nComboKills = fearpc::InstantActionComboKills(run);
	}
}

bool FearIAActive()
{
	return g_bActive;
}

bool FearIALoading()
{
	return g_bActive || g_bPending;
}

const wchar_t* FearIABriefing()
{
	return L"OBJECTIVE:\n\nFight through to the end of the map. One life, no saves.\n\n"
		L"SCORE:\n\nEnemies killed, kills of two or more with one shot or blast, hit rate, armor and "
		L"medkits remaining, boosters found, and the time left on the 15 minute clock.";
}

int FearIAMap()
{
	return g_nMap;
}

const wchar_t* FearIAMapName(int nMap)
{
	return nMap >= 0 && nMap < kFearIAMaps ? kMapNames[nMap] : L"";
}

bool FearIAMapExists(int nMap)
{
	if (nMap < 0 || nMap >= kFearIAMaps || !g_pMissionMgr)
		return false;
	char szWorld[MAX_PATH];
	WorldName(nMap, szWorld, LTARRAYSIZE(szWorld));
	return g_pMissionMgr->WorldExists(szWorld);
}

bool FearIAAvailable()
{
	for (int nMap = 0; nMap < kFearIAMaps; ++nMap)
		if (FearIAMapExists(nMap))
			return true;
	return false;
}

bool FearIAStart(int nMap, int nDifficulty)
{
	if (!FearIAMapExists(nMap))
		return false;
	char szWorld[MAX_PATH];
	WorldName(nMap, szWorld, LTARRAYSIZE(szWorld));

	g_pGameClientShell->SetDifficulty((GameDifficulty)LTCLAMP(nDifficulty, (int)GD_EASY, (int)GD_VERYHARD));

	// Armed for this one start: CMissionMgr::StartGameFromLevel calls
	// CInterfaceMgr::StartingNewGame, which turns it into the run's flag.
	g_nMap = nMap;
	g_bPending = true;
	const bool bStarted = g_pClientConnectionMgr->SetupServerSinglePlayer() && g_pMissionMgr->StartGameFromLevel(szWorld);
	g_bPending = false;
	if (!bStarted)
		g_bActive = false;
	return bStarted;
}

void FearIAClear()
{
	g_bPending = false;
	g_bActive = false;
	g_bBegun = false;
	g_bFailed = false;
}

const FearIASummary& FearIAClientSummary()
{
	return g_Summary;
}

float FearIAClientScore()
{
	return g_fScore;
}

bool FearIAClientNewBest()
{
	return g_bNewBest;
}

float FearIAClientTimeRemaining()
{
	return g_fTimeRemaining;
}

float FearIAClientHitRate()
{
	return g_fHitRate;
}

int FearIAClientComboKills()
{
	return g_nComboKills;
}

float FearIABest(int nMap)
{
	LoadBest();
	return nMap >= 0 && nMap < kFearIAMaps ? g_fBest[nMap] : -1.0f;
}

void FearIAClientStartingNewGame()
{
	g_bActive = g_bPending;
	g_bPending = false;
	g_bBegun = false;
	g_bFailed = false;
}

void FearIAClientStartLevel()
{
	if (g_bActive)
		SendIA(kFearIAStart, true);
}

void FearIAClientPlaying()
{
	// The first time the game goes to the playing state after the level loaded.
	if (!g_bActive || g_bBegun || !g_pLTClient->IsConnected())
		return;
	g_bBegun = true;
	if (g_pMainTimer)
		g_pMainTimer->SetTime(g_pLTClient->GetGameTime() + (double)fearpc::kInstantActionSeconds, false);
	SendIA(kFearIABegin, false);
}

bool FearIAClientMissionFailed()
{
	if (!g_bActive)
		return false;
	const double fNow = RealTimeTimer::Instance().GetTimerAccumulatedS();
	if (!g_bFailed)
	{
		// Ask for the summary; the screen changes when it arrives (FearIAClientOnMessage).
		// This is called again every frame the player stays dead.
		g_bFailed = true;
		g_fFailedTime = fNow;
		SendIA(kFearIAFailed, false);
		return true;
	}
	if (fNow - g_fFailedTime < kSummaryWait)
		return true;
	// No answer: give the run up and let the game show its failure screen.
	FearIAClear();
	return false;
}

void FearIAClientOnMessage(ILTMessage_Read* pMsg)
{
	if (!pMsg || pMsg->Readuint8() != kFearIASummary)
		return;

	FearIASummary s = {};
	s.bCompleted = pMsg->Readbool();
	s.fSeconds = pMsg->Readfloat();
	s.nShots = pMsg->Readuint32();
	s.nHits = pMsg->Readuint32();
	s.nKills = pMsg->Readuint32();
	s.nMeleeKills = pMsg->Readuint32();
	s.nFirearmKills = pMsg->Readuint32();
	s.nExplosiveKills = pMsg->Readuint32();
	s.nMedkits = pMsg->Readuint8();
	s.fArmor = pMsg->Readfloat();
	s.nBoosters = pMsg->Readuint8();
	s.nFavoriteWeapon = pMsg->Readuint8();
	const uint8 nCombos = pMsg->Readuint8();
	for (uint8 n = 0; n < nCombos; ++n)
	{
		const uint8 nCombo = pMsg->Readuint8();
		if (s.nCombos < kFearIAMaxCombos)
			s.aCombos[s.nCombos++] = nCombo;
	}

	if (!g_bActive)
		return;
	g_bFailed = false;
	g_Summary = s;
	Derive(s);

	LoadBest();
	g_bNewBest = g_fScore > g_fBest[g_nMap];
	if (g_bNewBest)
	{
		g_fBest[g_nMap] = g_fScore;
		SaveBest();
	}

	// To the summary screen, as the game goes to its failure screen (CInterfaceMgr::
	// MissionFailed): the player's modes are cleared and the player is out of the world.
	g_pPlayerMgr->ClearPlayerModes();
	g_pGameClientShell->SetWorldNotLoaded();
	g_pInterfaceMgr->SwitchToScreen(SCREEN_ID_INSTANT_ACTION_SUMMARY);
}
