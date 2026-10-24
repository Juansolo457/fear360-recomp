// F.E.A.R. 360 - how the enemies fight at each difficulty (#3).
//
// The AI felt slow and not deadly at Normal, at any frame rate. The 360 versions made Normal
// easier: their game databases (Server/Difficulty/Normal) have AIDifficultyFactor 0.7 and
// DamageAdjustment 0.45, where every PC database (F.E.A.R. 1.08, Extraction Point, Perseus
// Mandate) has 1.0 and 0.65. Easy, Hard and Very Hard are the same on both. In the game code the
// factor multiplies the AI's accuracy and divides its miss spread, grenade delays and dodge
// timing (RAISE_BY_DIFFICULTY / LOWER_BY_DIFFICULTY); the damage adjustment becomes the console
// variable AIDamageAdjustNormal, how hard enemies hit the player. So the 360's Normal sits
// between the PC's Easy and Normal.
//
// The difficulty changes nothing else about the enemies: the factor is all of their tactics
// (Very Hard's 1.5 means about 2.25x Normal's grenades, 1.5x the dodging, better aim), the damage
// adjustment how hard they hit. Played at the PC's Normal they still seemed passive; Very Hard
// "really, really smart, throwing grenades at me immediately", but hitting very hard.
//
// fear_ai picks:
//   sharp  (the default) Very Hard's factor at every difficulty, the PC's damage per difficulty:
//          enemies fight like Very Hard's and hit as hard as the chosen difficulty says
//   pc     the PC version's difficulties (Normal 1.0 and 0.65)
//   360    the console's (Normal 0.7 and 0.45)
// Each value is read through one function, CServerDB::GetDifficultyFactor and
// ::GetDamageAdjustment (the database lookup by name, per difficulty, when the server starts); the
// hooks sit right after the lookup, with the value in f1, tell the difficulty by the 360's value
// (each is different) and replace it.
//                          F.E.A.R.                  F.E.A.R. Files
//   GetDifficultyFactor    sub_825DB5B0, 0x825DB60C  sub_82628648, 0x826286A4
//   GetDamageAdjustment    sub_825DB620, 0x825DB67C  sub_826286B8, 0x82628714

#pragma once

#include <rex/ppc/context.h>

void FearDifficultyFactor(PPCRegister& f1);
void FearDamageAdjustment(PPCRegister& f1);
