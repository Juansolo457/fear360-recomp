// F.E.A.R. 360 - the collection menu: F.E.A.R. and F.E.A.R. Files (Extraction Point and Perseus
// Mandate) started from one menu, like the Master Chief Collection (docs/FILES.md).
//
// The two games are two programs (fear.exe, fearfiles.exe): both load their translation at
// 0x82000000 in the same guest address space, so one process runs one of them. The menu is in
// both, shown before any game loads (ReXApp::OnFinalizePaths, with the window and ImGui up and no
// runtime yet). Picking the game this program runs starts it; picking the other starts that
// program (which waits for this one to exit) and closes this one. "Switch Game" in each game's
// main menu (fear_menu.h) starts this program again, back at the menu. Built this way so that a
// later single program can keep the menu and load the chosen game in place of starting it.
//
// The titles come from a collection file, written by tools/launch.ps1 -Collection:
//   [[title]]
//   id = "fear"                      # this program's own is fear::title::kId
//   name = "F.E.A.R."
//   subtitle = "First Encounter Assault Recon"
//   exe = 'C:\...\fear.exe'
//   working_dir = 'C:\...\port-nx'
//   args = ['--game_data_root', 'C:\...\assets', ...]   # each title keeps its own user folder
//   later = true                     # optional: listed as coming later, can't be started
// (tools/steamdeck/fear-linux.sh writes one too: 1.0 lists F.E.A.R. Files with later = true.)
// fear_collection names the file (empty: no menu, the game starts as before);
// fear_collection_start, set when the menu starts the other program, skips the menu.

#pragma once

#include <functional>

#include "fear_panel.h"

namespace fear::collection {

// Whether Switch Game is offered in the main menu: this run is part of a collection that lists
// another game it can start (not one coming later, and its program is there).
bool Enabled();

// In ReXApp::OnFinalizePaths. True: start this program's game now (not in a collection, or told to
// start it). False: the menu is up; `start` runs on the UI thread when this game is picked, and
// the app is closed (host.quit) when the other one is, or on Quit.
bool StartNow(const fear::panel::Host& host, std::function<void()> start);

// From the main menu: starts this program again at the collection menu, after this run has exited.
// The caller then closes this run. False if it couldn't be started.
bool SwitchGame();

}  // namespace fear::collection
