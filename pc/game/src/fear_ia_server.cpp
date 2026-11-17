// F.E.A.R. PC - Instant Action on the server (FEAR_PC_IA): the run's flag and its statistics.
//
// See fear_ia.h for the mode. The server keeps one run: whether the mode is on (set by
// kFearIAStart, cleared by the next MID_START_GAME), and from kFearIABegin to the end of the
// run the numbers the summary screen and the score need. They are counted next to the places
// MissionStats counts shots, hits and kills, into this file's own structure.
//
// Kills by kind follow the 360, which splits them by damage type: its types 8 and 10 are melee,
// 1 and 14 firearm, 5 and 17 explosive. The PC's DamageType enum has the same order up to 16,
// so by name that is DT_MELEE and DT_RIFLE_BUTT, DT_BULLET and DT_ENERGY, and DT_EXPLODE; the
// 360's 17 is past the end of the PC's enum and has no counterpart. A kill of any other type
// counts as an enemy killed and in none of the three, as on the 360.
//
// Combos: see FearIAServerComboBegin in fear_ia.h. Damage is dealt synchronously (a damage
// message to the victim, whose CDestructible handles it and its death before DoDamage
// returns), so every kill of one trigger pull's vectors, or of one blast's sweep of the
// objects in its radius, lands between the Begin and the End around it. A projectile that
// flies (a grenade, a rocket) kills later, from its explosion: that blast is its own scope.
#include "Stdafx.h"

#include "fear_ia.h"

#include "DamageTypes.h"
#include "Destructible.h"
#include "EngineTimer.h"
#include "MsgIDs.h"
#include "PlayerInventory.h"
#include "PlayerObj.h"
#include "WeaponDB.h"

namespace
{
	const uint32 kMaxWeapons = 256;	// weapon records are indexed with a uint8

	struct WeaponCount
	{
		uint32 nShots;
		uint32 nHits;
		uint32 nKills;
	};

	struct Run
	{
		bool	bMode;			// Instant Action: no saving
		bool	bRunning;		// between kFearIABegin and the end: the statistics count
		bool	bEnded;			// the summary is final
		uint8	nMap;
		double	fStart;			// simulation time
		double	fEnd;
		FearIASummary summary;
		WeaponCount aWeapons[kMaxWeapons];
	};
	Run g_Run;

	// The open combo scopes and the kills inside the outermost one. Kept balanced whether or
	// not a run is on.
	uint32 g_nComboDepth = 0;
	uint32 g_nComboKills = 0;

	double Now()
	{
		return SimulationTimer::Instance().GetTimerAccumulatedS();
	}

	// The single player's object.
	CPlayerObj* Player()
	{
		CPlayerObj::PlayerObjList::const_iterator iter = CPlayerObj::GetPlayerObjList().begin();
		if (iter == CPlayerObj::GetPlayerObjList().end())
			return NULL;
		return *iter;
	}

	WeaponCount* WeaponSlot(const void* hWeapon)
	{
		if (!hWeapon)
			return NULL;
		const uint32 nIndex = g_pWeaponDB->GetRecordIndex((HWEAPON)hWeapon);
		return nIndex < kMaxWeapons ? &g_Run.aWeapons[nIndex] : NULL;
	}

	void ResetRun()
	{
		const bool bMode = g_Run.bMode;
		const uint8 nMap = g_Run.nMap;
		memset(&g_Run, 0, sizeof(g_Run));
		g_Run.bMode = bMode;
		g_Run.nMap = nMap;
		g_Run.summary.nFavoriteWeapon = kFearIANoWeapon;
		g_nComboKills = 0;
	}

	// Medkits: the gear that heals and is carried (counted) rather than used when picked up.
	uint8 MedkitsHeld(CPlayerObj* pPlayer)
	{
		uint32 nHeld = 0;
		const uint8 nGear = g_pWeaponDB->GetNumGear();
		for (uint8 n = 0; n < nGear; ++n)
		{
			HGEAR hGear = g_pWeaponDB->GetGearRecord(n);
			if (!hGear)
				continue;
			if (g_pWeaponDB->GetInt32(hGear, WDB_GEAR_nMaxAmount) > 0 && g_pWeaponDB->GetFloat(hGear, WDB_GEAR_fHealth) > 0.0f)
				nHeld += pPlayer->GetInventory()->GetGearCount(hGear);
		}
		return (uint8)LTMIN(nHeld, (uint32)255);
	}

	// The weapon with the most kills; more hits, then the lower record, break a tie.
	uint8 FavoriteWeapon()
	{
		uint8 nBest = kFearIANoWeapon;
		for (uint32 n = 0; n < kMaxWeapons && n < kFearIANoWeapon; ++n)
		{
			const WeaponCount& w = g_Run.aWeapons[n];
			if (!w.nKills)
				continue;
			if (nBest == kFearIANoWeapon || w.nKills > g_Run.aWeapons[nBest].nKills ||
				(w.nKills == g_Run.aWeapons[nBest].nKills && w.nHits > g_Run.aWeapons[nBest].nHits))
				nBest = (uint8)n;
		}
		return nBest;
	}

	void EndRun(bool bCompleted)
	{
		if (g_Run.bEnded)
			return;
		// A run that never began (no kFearIABegin) ends with empty statistics and no time.
		g_Run.fEnd = Now();
		if (!g_Run.bRunning)
			g_Run.fStart = g_Run.fEnd;
		g_Run.bRunning = false;
		g_Run.bEnded = true;

		FearIASummary& s = g_Run.summary;
		s.bCompleted = bCompleted;
		s.fSeconds = (float)LTMAX(g_Run.fEnd - g_Run.fStart, 0.0);
		s.nFavoriteWeapon = FavoriteWeapon();
		CPlayerObj* pPlayer = Player();
		if (pPlayer)
		{
			s.nMedkits = MedkitsHeld(pPlayer);
			s.fArmor = LTMAX(pPlayer->GetDestructible()->GetArmorPoints(), 0.0f);
		}
	}

	void SendSummary()
	{
		CPlayerObj* pPlayer = Player();
		if (!pPlayer || !pPlayer->GetClient())
			return;
		const FearIASummary& s = g_Run.summary;
		CAutoMessage cMsg;
		cMsg.Writeuint8(MID_INSTANT_ACTION);
		cMsg.Writeuint8(kFearIASummary);
		cMsg.Writebool(s.bCompleted);
		cMsg.Writefloat(s.fSeconds);
		cMsg.Writeuint32(s.nShots);
		cMsg.Writeuint32(s.nHits);
		cMsg.Writeuint32(s.nKills);
		cMsg.Writeuint32(s.nMeleeKills);
		cMsg.Writeuint32(s.nFirearmKills);
		cMsg.Writeuint32(s.nExplosiveKills);
		cMsg.Writeuint8(s.nMedkits);
		cMsg.Writefloat(s.fArmor);
		cMsg.Writeuint8(s.nBoosters);
		cMsg.Writeuint8(s.nFavoriteWeapon);
		cMsg.Writeuint8(s.nCombos);
		for (uint8 n = 0; n < s.nCombos; ++n)
			cMsg.Writeuint8(s.aCombos[n]);
		g_pLTServer->SendToClient(cMsg.Read(), pPlayer->GetClient(), MESSAGE_GUARANTEED);
	}
}

bool FearIAServerActive()
{
	return g_Run.bMode;
}

void FearIAServerStartGame()
{
	g_Run.bMode = false;
	g_Run.bRunning = false;
	g_Run.bEnded = false;
}

void FearIAServerOnMessage(ILTMessage_Read* pMsg)
{
	if (!pMsg || IsMultiplayerGameServer())
		return;
	switch (pMsg->Readuint8())
	{
	case kFearIAStart:
		g_Run.bMode = true;
		g_Run.nMap = pMsg->Readuint8();
		ResetRun();
		break;
	case kFearIABegin:
		if (g_Run.bMode && !g_Run.bRunning && !g_Run.bEnded)
		{
			ResetRun();
			g_Run.bRunning = true;
			g_Run.fStart = Now();
		}
		break;
	case kFearIAFailed:
		if (g_Run.bMode)
		{
			// If the run already ended (the map was completed), that result stands.
			EndRun(false);
			SendSummary();
		}
		break;
	default:
		break;
	}
}

bool FearIAServerExitLevel()
{
	if (!g_Run.bMode)
		return false;
	if (!g_Run.bEnded)
	{
		EndRun(true);
		SendSummary();
	}
	return true;
}

void FearIAServerShot(const void* hWeapon, uint32 nVectors)
{
	if (!g_Run.bRunning)
		return;
	g_Run.summary.nShots += nVectors;
	if (WeaponCount* pWeapon = WeaponSlot(hWeapon))
		pWeapon->nShots += nVectors;
}

void FearIAServerHit(const void* hWeapon)
{
	if (!g_Run.bRunning)
		return;
	g_Run.summary.nHits++;
	if (WeaponCount* pWeapon = WeaponSlot(hWeapon))
		pWeapon->nHits++;
}

void FearIAServerKill(int nDamageType, const void* hAmmo)
{
	if (!g_Run.bRunning)
		return;
	FearIASummary& s = g_Run.summary;
	s.nKills++;
	switch (nDamageType)
	{
	case DT_MELEE:
	case DT_RIFLE_BUTT:
		s.nMeleeKills++;
		break;
	case DT_BULLET:
	case DT_ENERGY:
		s.nFirearmKills++;
		break;
	case DT_EXPLODE:
		s.nExplosiveKills++;
		break;
	default:
		break;
	}
	// The weapon is the first that takes the ammo that killed.
	if (hAmmo)
	{
		if (WeaponCount* pWeapon = WeaponSlot(g_pWeaponDB->GetWeaponFromAmmo((HAMMO)hAmmo, !USE_AI_DATA)))
			pWeapon->nKills++;
	}
	if (g_nComboDepth)
		g_nComboKills++;
}

void FearIAServerGear(const void* hGear)
{
	if (!g_Run.bRunning || !hGear)
		return;
	// The boosters are the gear that raises a maximum: health (Health booster) or slow-mo
	// (Reflex booster).
	if (g_pWeaponDB->GetFloat((HGEAR)hGear, WDB_GEAR_fHealthMax) > 0.0f ||
		g_pWeaponDB->GetFloat((HGEAR)hGear, WDB_GEAR_fSlowMoMaxBonus) > 0.0f)
	{
		if (g_Run.summary.nBoosters < 255)
			g_Run.summary.nBoosters++;
	}
}

void FearIAServerComboBegin()
{
	if (g_nComboDepth++ == 0)
		g_nComboKills = 0;
}

void FearIAServerComboEnd()
{
	if (!g_nComboDepth || --g_nComboDepth)
		return;
	FearIASummary& s = g_Run.summary;
	if (g_Run.bRunning && g_nComboKills >= 2 && s.nCombos < kFearIAMaxCombos)
		s.aCombos[s.nCombos++] = (uint8)LTMIN(g_nComboKills, (uint32)255);
	g_nComboKills = 0;
}
