# Enemy AI: next-level combatants (ideas)

Written 2026-10-25 on the `enemy-ai-ideas` branch (#67). This is ideation only: nothing was
built or run for it.

The user's goal: "a next level of AI combatants for the player to face", in a wave mode like
Gears of War's Horde or Halo: Reach and ODST's Firefight. The first idea was two instances of
the game in multiplayer, with the game's AI playing one side. That has to wait for multiplayer
(#65), and F.E.A.R. has no bots, so this note starts from the enemy AI the game already has.

## What the game already has

These findings come from the 360 game data and executables (both games), and from Monolith's
public PC SDK game code. The SDK code was only read for reference; nothing is copied from it.

- **The AI is a planner.** Each enemy picks a goal from its goal set, then plans actions to meet
  it from what its sensors have put in its working memory. Squads coordinate through a shared
  blackboard. F.E.A.R.'s 360 database (`Game/database/Loki.gamedb`, read with `tools/gamedb.py`)
  has 58 goals, 100 actions, 51 sensors, 43 goal sets and 29 attribute sets.
- **Each enemy type has tuning values** in `AI/Attributes`:
  - accuracy, and how fast it rises and falls (`Accuracy`, `AccuracyIncreaseRate`,
    `AccuracyDecreaseRate`);
  - miss spread (`AccuracyMissPerturb`, `FullAccuracyRadius`);
  - senses (`SeeDistance`, `HearDistance`, `TargetIsAimingAtMeFOV`);
  - turn and run speeds (`RotationTime*`, `RunSpeed`);
  - an `UpdateRate`. It's 0.33 for `SpecialForces`; what it times is still to be confirmed.

  On top of these, the difficulty level scales accuracy, miss spread, grenade use and dodging
  (`port-nx/src/difficulty.cpp`, the Enemy AI option, #3).
- **The roster.**
  - The attribute sets for combatants are `SpecialForces`, `Recon`, `Sniper`, `Assassin`,
    `HeavyArmor`, `PoweredArmor` (the REV2), `ATCSecurity`, `Ghost`, `FloorDrone`,
    `FlyingDrone`, `Turret`, `Turret_Ceiling` and `MeatPuppet`.
  - The models include the Replica soldier and its Elite, Recon, Tactical, Urban, Desert and
    Fatigue variants, plus phantoms (`Ghost`, `BigGhost`).
  - F.E.A.R. Files' roster is still to be listed. Its database is packed in its catalog.
- **Spawning.** Levels place `AISpawner` objects. A `SPAWN` or `SPAWNFROM <object>` command
  creates an AI from a template object in the level, and level scripts send those commands
  (`msg <spawner> SPAWN`).
- **Where the AI can go.** The AI needs navigation data to move around a map.
  - Campaign and Instant Action worlds have it. `Docks` and `InstantAction1` have a nav mesh,
    about 850 and 1,050 AI node entries (cover, ambush, patrol and so on), and 52 and 40
    spawner entries.
  - The multiplayer worlds checked (`Docks`, `Office`, `Campus`) have none of it.
  - So the AI can fight on any campaign or Instant Action map today, but not on a multiplayer
    map.
- **PitFight, a leftover AI arena.**
  - Both 360 executables contain a `PitFight` object and the classes `CPitFightStart`,
    `CPitFightReset`, `CPitFightAIList`, `CPitFightWeaponList` and `CPitFightScene`.
  - Its strings describe teams of AI fighting each other:
    - the teams: `NUMTEAMS`, `TEAMMODEL %d %s`, `TEAMWEAPON %d %s`, `TEAMCOUNT %d`;
    - the match: `SCORETOWIN`, `FIGHTERSPAWNED`, `FIGHTERDIED`;
    - the spawning: spawners named `%s_AISpawner_%02d` that spawn from `StartingPoint%02d`
      objects.
  - It refers to a world named `PitFight00`, but no such world is on the disc.
  - It looks like Monolith's own tool for watching AI fight AI, which is the play-against-itself
    idea. Looked into on 2026-10-25: see [PitFight](#pitfight) below.
- **Instant Action** has four maps in F.E.A.R. and eight in F.E.A.R. Files. They were built for
  combat, with AI already placed.

## PitFight

Read on 2026-10-25 from F.E.A.R.'s 360 executable: its strings, its RTTI, and the disassembly of
the PitFight code. Nothing was built or run.

**It comes from Condemned, not F.E.A.R.** The menu's fighter list holds 39 entries, and they're
the cast of Monolith's Condemned: Criminal Origins (Becker, Vanhorn, Rosa, the SKX killers, the
addicts, mannequins and vagrants). The 360 F.E.A.R. was built on the same 360 engine code, so
Condemned's melee test pit came along. Nothing in F.E.A.R. uses it.

**How it works:**

- **The arena:** a server object of class `PitFight`, which derives from `GameBase`. The menu
  looks it up by the name `PitFight00`.
- **Its commands** (the command manager's `msg PitFight00 (...)`):

  | Command | Effect |
  |---|---|
  | `NUMTEAMS n` | Sets the number of teams. |
  | `SCORETOWIN n` | Sets the score that wins the match. |
  | `TEAMMODEL t model` | Sets team `t`'s character. |
  | `TEAMWEAPON t weapon` | Sets team `t`'s weapon. |
  | `CLEARTEAMS` | Clears the team setup. |
  | `START` | Starts the match. |
  | `END` | Sends `REMOVE` to every AI and every weapon item. |

- **The fighters:**
  - **Spawning:** `START` looks for one spawner per team, named `<model>_AISpawner_<tt>` (for
    example `Soldier_AISpawner_00`). It sends that spawner `SPAWNFROM StartingPoint<tt>`, so each
    team has one fighter at its own starting point.
  - **`FIGHTERSPAWNED t`:** gives the new fighter its team's weapon
    (`AddWeapon <weapon> RightHand`).
  - **`FIGHTERDIED t`:** counts the kill and respawns that team's fighter a second later
    (`delay 1 (msg <spawner> (SPAWNFROM StartingPoint<tt>))`).
  - **Who sends these:** the fighters must send both messages themselves. In the original world
    that was the job of each template AI's own commands: the AI's `Initial` command and its
    `DeathCommand`.
- **The menu:** an XUI scene (`PitFightScene`, with fighter and weapon lists for each team, score
  text, and start and reset buttons). It sends `TEAMCOUNT`, then `TEAMMODEL` and `TEAMWEAPON` for
  each team, then `START`.
  - The arena doesn't register `TEAMCOUNT`. Its command is `NUMTEAMS`, so the menu and the arena
    don't quite agree, which suggests the feature was unfinished in this branch.
  - The scene itself would be in `global\ui\XUI\assets.xzp`, which the game asks for at startup
    and the disc doesn't have.

**What's missing in F.E.A.R.:**

- the `PitFight00` world, or any world with these objects;
- the menu scene;
- characters for it: the Condemned names mean nothing here, but F.E.A.R.'s own models fit the
  same scheme (`Soldier`, `HeavyArmor`, `Assassin`, `Powerarmor` and so on).

The arena code itself is intact.

**To see it run in F.E.A.R.:**

1. **Map:** use an Instant Action map, which has AI navigation.
2. **Objects:** give the map the objects PitFight expects. All of them are ordinary world
   objects:
   - a `PitFight` object named `PitFight00`;
   - a template AI for each team, with a model, the weapon left to PitFight, and an alignment
     that makes the two teams enemies. Each template's `Initial` command sends
     `msg PitFight00 (FIGHTERSPAWNED t)`, and its `DeathCommand` sends
     `msg PitFight00 (FIGHTERDIED t)`;
   - an `AISpawner` for each team, named `<model>_AISpawner_<tt>`, aimed at that template;
   - a `StartingPoint<tt>` object for each team.
3. **Start:** send `NUMTEAMS`, `SCORETOWIN`, `TEAMMODEL` and `TEAMWEAPON` for each team, then
   `START`.
4. **Watch:** the player watches in god mode. The score and each kill go to the log, and later
   to the HUD.

**Getting the objects in:**

- **A patched copy of the map's object file:** the most likely route. Write a patched copy of the
  player's own `.wldsrvr` to `<user data>/overrides/` at startup and redirect the game to it,
  as `look_tuning.cpp` already does for `Loki.gamedb`. The game files aren't changed and
  nothing goes in the repo. The `.wldsrvr` object format has to be read first.
- **Creating the objects while the game runs:** call the game's own object spawner. Our hooks
  can only change registers, so this needs a way to call guest code first.
- **Sending the commands:** the same patched map can carry them, as the `Initial` command of an
  extra object. Otherwise our code passes the game's command manager a string at a point where
  it already runs level commands.

A launch option would pick the map, the two fighters and weapons, and the score to win, for
example `--fear_pitfight="InstantAction1;Soldier,<weapon>;HeavyArmor,<weapon>;5"`. The same
object injection is also what the wave mode needs to place its spawners.

## The mode: Battalion

A Horde- or Firefight-style survival mode. It starts on the Instant Action maps, and campaign
arenas come later.

**The name** was chosen on 2026-10-25. Fettel commands a battalion of Replica soldiers, and the
game's own dialogue says so. Inside the mode, other names come from the same lore:

- **Sets** get Interval-style titles, like the campaign's chapters ("Interval 01 - Inception").
- **Boss waves** are Synchronicity waves, after the game's "synchronicity event".
- **Slow motion** keeps the game's own word, SlowMo.

In code the mode is `battalion` (`port-nx/src/battalion.*`, the launch option `fear_battalion`).

- **Waves and sets.** A set is five waves. Each wave brings more enemies, and tougher ones, than
  the last. The fifth wave is a boss wave: Heavy Armor, the REV2, or a pack of Assassins. Sets
  repeat at a higher difficulty, either without end or up to a target.
- **A director spawns the squads.** They come in from spawners the player can't see, at least a
  minimum distance away, and from more than one side.
- **Between waves:**
  - a short breather;
  - ammo and health;
  - a weapon choice (#60's loadouts, with John Wick mode, #61, as one loadout).
- **Modifiers**, like Halo's skulls but F.E.A.R.'s own:
  - no slow-mo;
  - lights out (flashlight only);
  - assassins only;
  - a phantom wave (phantoms and hallucinations);
  - iron man (no health packs);
  - point blank (shotguns and melee only).
- **Score:**
  - points for kills, headshots, melee kills and slow-mo kills;
  - points for time survived;
  - a local high-score table for each map.
- **HUD:** the wave and set numbers and the enemies left, shown the same way as the god mode
  message (#55).
- **Co-op, later:** two players against the waves, once System Link works (#65).

## The next-level part: smarter, not stronger

In 2005 the AI was praised for flanking, suppressing fire, flushing the player out with grenades
and talking to each other. The aim is enemies that outthink the player. They don't get better
aim and they don't see through walls. Ideas, roughly cheapest first:

1. **Tune the existing AI:**
   - quicker reactions, from the attribute values above and the sensor update rates;
   - the Sharp difficulty factor;
   - better squad sizes and mixes.

   The AI lab below measures the results.
2. **A director**, like Left 4 Dead's. It reads the player's health, ammo, slow-mo meter and
   position, then sets the pace: when to push, when to let up, and where the next squad comes
   from.
3. **Squad tactics on top of the planner:**
   - flanking from two sides at once;
   - suppressing fire while others advance;
   - pushing when the player reloads or slow-mo runs out;
   - grenades to flush out a camper;
   - snipers covering the approaches;
   - assassins striking while the player is busy with soldiers;
   - falling back and regrouping when a squad is losing.
4. **Learn the player during a session.** Note where they hide, how they use slow-mo and which
   weapon they favor, then counter it: grenades on their favorite spots, and squads that spread
   out against slow-mo entries.
5. **Use the game's own voice lines** so new behaviors are called out the way the original ones
   are ("flanking", "reloading", "grenade"). Much of what made the 2005 AI feel smart was
   hearing it.
6. **Keep it fair:**
   - enemies signal an attack before they make it;
   - they never know anything they couldn't have seen or heard;
   - the difficulty comes from tactics.

## The AI lab: tuning by measurement

1. **Telemetry.** Hooks log what each enemy senses, which goals and actions it chooses, its
   shots and hits, and how each fight ends, the same way the difficulty hooks log today. The user
   plays and records as now, and the logs line up with the videos.
2. **A scripted test player.** A second instance runs with its own profile and cache.
   `tools/autoplay.ps1` already drives the game with key presses. The test player plays the same
   fight many times for each setting, with god mode on (`fear_invulnerable`), for A/B
   comparisons.
3. **AI against AI.** PitFight, or our own team setup on an Instant Action map, lets two AI
   teams with different tuning fight many unattended rounds. That's the original two-instance
   idea, in one instance.

## Multiplayer

Bots on multiplayer maps need AI navigation those maps don't have: a nav mesh and nodes
generated for each map. They also need a layer that turns the AI's decisions into a player's
movement and aim. That's the largest piece, and it waits for #65.

## Phases

| Phase | What | Rough size |
|---|---|---|
| 0. Discovery | <ul><li>PitFight running in F.E.A.R.: read the `.wldsrvr` object format, then inject PitFight's objects (see [PitFight](#pitfight)).</li><li>How Instant Action starts and ends a map.</li><li>How our code can send the game's commands (spawn, messages).</li><li>F.E.A.R. Files' roster.</li></ul> | small |
| 1. Telemetry | <ul><li>Logging of AI decisions and combat.</li><li>The AI lab's log reader.</li></ul> | small to medium |
| 2. Wave prototype | One Instant Action map: <ul><li>waves spawned from its spawners;</li><li>a wave counter;</li><li>resupply.</li></ul> | medium |
| 3. Tactics | Tuning first, then new behaviors where tuning isn't enough. | medium to large |
| 4. The mode | <ul><li>A menu entry, in the Instant Action menu or the collection launcher (#43).</li><li>Modifiers and scores.</li><li>Loadouts (#60, #61).</li></ul> | medium |
| 5. Co-op and multiplayer bots | After System Link (#65). | large |

## Ground rules

- By default the campaign stays as the game shipped it, in line with the 1:1 PC port goal. The
  new mode, and any AI changes beyond the original, are options.
- The PC SDK source is a read-only reference. The work is our own code in `port-nx/src`, using
  hooks and the game's own functions and commands. Nothing is copied from the SDK.
- No game data goes in the repo. World files, databases and the values read from them stay
  local, apart from the few names and numbers in this note.

## Open questions

- PitFight: the `.wldsrvr` object format, and how fighters of two models get hostile alignments
  (`Character/Alignments` has `BadCharacter`, `GoodCharacter`, `NeutralCharacter`, `Player` and
  others). F.E.A.R. Files has the same PitFight classes, unchecked beyond their names.
- Can spawners be added while the game runs, or does a wave mode have to use each map's own
  spawners and template objects?
- Does Instant Action's own scoring and end-of-map logic help a wave mode, or get in its way?
- What does `UpdateRate` time? How do the sensor update rates interact with the high-frame-rate
  fix (#3, [AI-HIGH-FPS.md](AI-HIGH-FPS.md))?
- What enemies does F.E.A.R. Files have, and which of its eight Instant Action maps suit waves?

## Related issues

| Issue | Topic |
|---|---|
| #3 | AI at high frame rates |
| #4, #62 | Ragdolls |
| #13 | Slow-mo timing |
| #55 | God mode |
| #60, #61 | Instant Action loadouts |
| #65 | Multiplayer |
| #43 | Collection menu |
