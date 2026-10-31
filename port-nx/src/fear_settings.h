// F.E.A.R. 360 - the PC Settings panel and the quit confirmation, opened from the game's own main
// menu (fear_menu.h).
//
// The panel lists the port's own settings (resolution, window, frame-rate limit, texture
// filtering, PC textures, right stick, aim assist, field of view, animation timing), each a cvar.
// They are saved
// to <user data>/settings.toml, which is loaded at startup before the cvars are finalised, below
// the command line: a flag the launcher passes still wins for that run. "Save and restart" starts
// the game again without those flags (and waits for this run to exit first), so what was saved
// applies. Window mode and animation timing apply at once; the rest on the next start. Saving
// keeps the file's lines for settings the panel doesn't write.
//
// With the keyboard and mouse on, its first row opens the Controls page (fear_controls.h): key
// bindings and the mouse's settings, saved with the rest and applied at once.
//
// The panels' look and input are fear_panel.h's; the restart is fear_process.h's.

#pragma once

#include <filesystem>

#include "fear_panel.h"

namespace fear::settings {

// In ReXApp::OnConfigurePaths: waits for the previous run after a restart, writes the
// first-run settings (resolution from the display) if there are none, and loads them.
void Load(const std::filesystem::path& user_data_root);

using Host = fear::panel::Host;

// On the UI thread. Each does nothing while a panel is already open.
void OpenSettings(const Host& host);
void OpenQuit(const Host& host);

}  // namespace fear::settings
