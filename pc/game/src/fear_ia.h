// F.E.A.R. PC - the Xbox 360 game's Instant Action mode (FEAR_PC_IA).
//
// On the 360 Instant Action is four ordinary scripted levels played against a 15 minute clock,
// with a results screen that adds a score up from the run's statistics (docs/PC-FINDINGS.md,
// "Instant Action on the 360, from its code"; the score itself is pc/core instant_action.h).
// Here it is the same thing on the PC game: a main menu entry, a map screen, a run with the
// HUD clock and no saving, and a summary screen in place of the end-of-level and failure flows.
// The game mode stays "SinglePlayer"; a run is a flag on the client and one on the server.
//
// The maps are the 360's worlds converted to Worlds\Release\InstantAction1..4. They keep the
// 360's script, which ends a run with "msg player exitlevel". A map is offered when its world
// file exists; with none, the main menu has no Instant Action entry.
//
// How a run goes:
//   - The map screen (or Retry) calls FearIAStart: difficulty, the client's flag, then the
//     call chain Custom level uses (SetupServerSinglePlayer, CMissionMgr::StartGameFromLevel).
//     The flag is armed just for that call: any other way of starting a game clears it
//     (FearIAClientStartingNewGame, from CInterfaceMgr::StartingNewGame), as does the main menu.
//   - CMissionMgr::FinishStartGameFromLevel sends kFearIAStart between MID_START_GAME (where the
//     server clears its flag) and MID_START_LEVEL, so the server refuses saves from the first
//     moment of the load: no reload save, checkpoint or quick save is written during a run.
//   - The first time play starts the client sets the HUD clock to 900 s and sends kFearIABegin;
//     the server clears its statistics and stamps the start time.
//   - The map's exitlevel reaches FearIAServerExitLevel, which ends the run as completed and
//     sends the summary. A mission failure (the player's death included) reaches
//     FearIAClientMissionFailed, which asks the server with kFearIAFailed for the summary of a
//     failed run. Either way the client shows the summary screen when the summary arrives.
//
// The statistics are kept in our own structure on the server and sent in our own message:
// MissionStats (Shared/SharedMission.h) goes into save files and MID_PLAYER_SUMMARY and is
// left alone. Nothing here changes a class or structure of the game's.
//
// Include after the precompiled header (stdafx.h): this needs only the engine's base types.
#pragma once

#include "ltbasedefs.h"

class ILTMessage_Read;
class CBaseScreen;

const int kFearIAMaps = 4;
// The most combos a summary carries; a run has a few at most.
const int kFearIAMaxCombos = 128;
// FearIASummary::nFavoriteWeapon with no kills.
const uint8 kFearIANoWeapon = 255;

// What follows the message id in MID_INSTANT_ACTION (Shared/MsgIDs.h), one byte.
enum FearIAMsg
{
	kFearIAStart,		// client to server, before MID_START_LEVEL: uint8 map (0 to 3)
	kFearIABegin,		// client to server, when play starts: nothing
	kFearIAFailed,		// client to server, on a mission failure: nothing
	kFearIASummary,		// server to client: a FearIASummary, see FearIAWriteSummary
};

// A finished run, as the server sends it. In the message, in this order: bool completed, float
// seconds, uint32 shots, hits, kills, melee, firearm and explosive kills, uint8 medkits, float
// armor, uint8 boosters, uint8 favorite weapon, uint8 combo count and a uint8 for each combo.
struct FearIASummary
{
	bool	bCompleted;			// the map ended the level; false for a failure
	float	fSeconds;			// how long the run took, in game time
	uint32	nShots;				// as MissionStats counts them: vectors, for accuracy weapons
	uint32	nHits;
	uint32	nKills;				// characters the player killed
	uint32	nMeleeKills;		// by damage type, as the 360 splits them
	uint32	nFirearmKills;
	uint32	nExplosiveKills;
	uint8	nMedkits;			// held at the end
	float	fArmor;				// at the end
	uint8	nBoosters;			// Health and Reflex boosters picked up
	uint8	nFavoriteWeapon;	// index of the weapon record with the most kills, or kFearIANoWeapon
	uint8	nCombos;			// each time one shot or one explosion killed 2 or more
	uint8	aCombos[kFearIAMaxCombos];
};

//
// The client (fear_ia_client.cpp, fear_ia_screens.cpp).
//

// A run is being started, played or summed up. The client refuses to save or quick load.
bool FearIAActive();
// A run is being started or is active: the loading screens name the map.
bool FearIALoading();
// What the loading screen says about the mode.
const wchar_t* FearIABriefing();
// The map of the current or last run, 0 to 3.
int FearIAMap();
const wchar_t* FearIAMapName(int nMap);
// The map's world file is there.
bool FearIAMapExists(int nMap);
// Any map is: the main menu shows the entry.
bool FearIAAvailable();
// Starts a run on a map at a difficulty (GameDifficulty). False if the level could not start.
bool FearIAStart(int nMap, int nDifficulty);
// The main menu has the focus: no run.
void FearIAClear();
// The summary of the run that just ended, its score, and whether that beat the map's best.
const FearIASummary& FearIAClientSummary();
float FearIAClientScore();
bool FearIAClientNewBest();
// Its derived rows (pc/core instant_action.h): seconds left on the clock, hits per shot (0 to
// 1) and the enemies in combos.
float FearIAClientTimeRemaining();
float FearIAClientHitRate();
int FearIAClientComboKills();
// The best score kept for a map (fearpc_ia.txt in the user directory); negative with none.
float FearIABest(int nMap);

// Hooks in the game client.
void FearIAClientStartingNewGame();					// CInterfaceMgr::StartingNewGame
void FearIAClientStartLevel();						// CMissionMgr::FinishStartGameFromLevel
void FearIAClientPlaying();							// CInterfaceMgr::PrePlayingState
bool FearIAClientMissionFailed();					// CInterfaceMgr::MissionFailed; true: handled
void FearIAClientOnMessage(ILTMessage_Read* pMsg);	// CGameClientShell::OnMessage
CBaseScreen* FearIACreateScreen(int nScreenID);		// CScreenMgr::AddScreen

//
// The server (fear_ia_server.cpp). Records are passed as plain pointers (HRECORD, HWEAPON,
// HAMMO and HGEAR are all "const CRecord*") so this header needs no database header.
//

// The server's flag: saving is refused (CServerSaveLoadMgr::CanSaveGame).
bool FearIAServerActive();
// A game is starting (CServerMissionMgr::HandleStartGame): no run until kFearIAStart says so.
void FearIAServerStartGame();
void FearIAServerOnMessage(ILTMessage_Read* pMsg);	// CGameServerShell::OnMessage
// The player was told to exit the level (CPlayerObj::HandleExitLevelMsg). True in a run: the
// run is over, the summary is sent and the level is not left.
bool FearIAServerExitLevel();
// The statistics, from where MissionStats counts the same things.
void FearIAServerShot(const void* hWeapon, uint32 nVectors);	// CPlayerObj::HandleWeaponFireMessage
void FearIAServerHit(const void* hWeapon);						// CProjectile::ImpactDamageObject
void FearIAServerKill(int nDamageType, const void* hAmmo);		// CDestructible::HandleDestruction
void FearIAServerGear(const void* hGear);						// CPlayerInventory::PickupGear
// A combo is the kills between a Begin and its End: one trigger pull of the player's
// (CPlayerObj::HandleWeaponFireMessage, around CWeapon::Fire) or one explosion's blast
// (Explosion::Start, around AreaDamageObjectsInSphere). Begin and End nest; the outermost
// End records the count when it is 2 or more.
void FearIAServerComboBegin();
void FearIAServerComboEnd();
