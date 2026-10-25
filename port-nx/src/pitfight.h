// F.E.A.R. 360 - PitFight: Condemned's AI fight pit, set up in a F.E.A.R. map (#67).
//
// Both 360 executables carry a server object class, PitFight, from Monolith's Condemned: Criminal
// Origins (the menu's fighter list is Condemned's cast): teams of one AI each that fight, score and
// respawn. Nothing in F.E.A.R. uses it. Its world (PitFight00) and its menu scene (PitFightScene,
// in global\ui\XUI\assets.xzp) aren't on the disc, but the arena code is all there:
//   - the arena is an object the menu finds by the name PitFight00. Its commands (registered at
//     F.E.A.R. sub_8305BEE0): NUMTEAMS n (2 to start with), SCORETOWIN n (0: no end),
//     TEAMMODEL t name, TEAMWEAPON t weapon, CLEARTEAMS, START, END
//   - START (sub_82673540) sends each team's spawner, <name>_AISpawner_<tt>, the command
//     SPAWNFROM StartingPoint<tt>
//   - a new fighter sends FIGHTERSPAWNED t, and the arena gives it its team's weapon, if any
//     (ADDWEAPON <weapon> RightHand)
//   - FIGHTERDIED t (sub_82673700) respawns team t a second later. The team whose number plus one
//     is the first number in the sender's name scores, until one reaches SCORETOWIN.
//
// fear_pitfight names a map (InstantAction1, ...). At startup a patched copy of the player's own
// <map>.wldsrvr (the map's server objects: a count, then each object's class and properties) is
// written to <user data>/overrides/PitFight/, with these objects added:
//   - the arena, PitFight00
//   - for each team, a template AI cloned from one of the map's own (by model, fear_pitfight_teams),
//     whose DeathCommand sends FIGHTERDIED t. It keeps the template's weapons, so the arena has no
//     TEAMWEAPON and the fighter doesn't send FIGHTERSPAWNED (an empty weapon would make that a
//     malformed ADDWEAPON)
//   - a spawner for each team aimed at it, alignment BadCharacter for team 0 and GoodCharacter for
//     team 1 so the two fight (the first also hates the player), spawning PitFight_Fighter<t+1>
//   - a starting point for each team: two of the map's own spawner positions near the player's start
//   - a startup command that sets the teams up and sends START.
// The other files of the map folder are hard links to the originals, and the game's path to the
// folder is redirected there (a VFS symbolic link, as look_tuning does for the database); what the
// override folder doesn't have comes from the player's own Game folder. The game files are not
// changed.

#pragma once

namespace rex {
class Runtime;
}

namespace fear::pitfight {

// After the runtime has mounted the game data (ReXApp::OnPostSetup).
void Apply(rex::Runtime& runtime);

}  // namespace fear::pitfight
