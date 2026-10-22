// F.E.A.R. 360 - the PC Settings panel and the quit confirmation, opened from the game's own main
// menu (fear_menu.h).
//
// The panel lists the port's own settings (resolution, window, frame-rate limit, texture
// filtering, PC textures, right stick, aim assist, animation timing), each a cvar. They are saved
// to <user data>/settings.toml, which is loaded at startup before the cvars are finalised, below
// the command line: a flag the launcher passes still wins for that run. "Save and restart" starts
// the game again without those flags (and waits for this run to exit first), so what was saved
// applies. Window mode and animation timing apply at once; the rest on the next start.
//
// Driven with the controller (D-pad or left stick, A, B), the keyboard (arrows, Enter, Escape) or
// the mouse. While a panel is open the game gets no input, and after it closes not until the
// buttons are let go, so the press that closed it doesn't reach the menu behind it.

#pragma once

#include <filesystem>
#include <functional>

struct ImFontAtlas;

namespace rex::ui {
class ImGuiDrawer;
}

namespace fear::settings {

// In ReXApp::OnConfigurePaths: waits for the previous run after a restart, writes the
// first-run settings (resolution from the display) if there are none, and loads them.
void Load(const std::filesystem::path& user_data_root);

// In ReXApp::OnConfigureFonts: the panel's font (Bahnschrift or Segoe UI, from Windows).
void AddFonts(ImFontAtlas* atlas);

// For InputSystem::SetActiveCallback: whether the game gets the controller and keyboard now.
bool GameGetsInput();

struct Host {
  rex::ui::ImGuiDrawer* drawer = nullptr;
  // Runs a function on the UI thread after the current frame.
  std::function<void(std::function<void()>)> later;
  // Closes the game, as the window's close button does.
  std::function<void()> quit;
};

// On the UI thread. Each does nothing while a panel is already open.
void OpenSettings(const Host& host);
void OpenQuit(const Host& host);

}  // namespace fear::settings
