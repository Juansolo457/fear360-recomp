// F.E.A.R. 360 - the look and the input of the port's own panels: PC Settings, the quit
// confirmation (fear_settings.h) and the collection menu (fear_collection.h).
//
// A panel is an ImGui dialog over the game, centered, in a large font (Bahnschrift from Windows),
// driven with the controller (D-pad or left stick, A, B, read through XInput: the game's input
// system gives a panel nothing), the keyboard (arrows, Enter, Escape) or the mouse. While one is
// open the game gets no input, and after it closes not until every button is let go, so the press
// that closed it doesn't reach the menu behind it.

#pragma once

#include <cstdint>
#include <functional>

#include <rex/ui/imgui_dialog.h>

struct ImFontAtlas;

namespace fear::panel {

// What a panel needs from the app.
struct Host {
  rex::ui::ImGuiDrawer* drawer = nullptr;
  // Runs a function on the UI thread after the current frame.
  std::function<void(std::function<void()>)> later;
  // Closes the game, as the window's close button does.
  std::function<void()> quit;
};

enum Input : uint32_t {
  kUp = 1,
  kDown = 2,
  kLeft = 4,
  kRight = 8,
  kAccept = 16,
  kBack = 32,
};
inline constexpr uint32_t kDirections = kUp | kDown | kLeft | kRight;

// What was pressed since the last frame: controller, keyboard. Held directions repeat. What is
// already held when the panel opens (the press that opened it) counts only once let go.
class Navigator {
 public:
  uint32_t Poll();

 private:
  uint32_t held_ = ~0u;
  double next_repeat_ = 1e30;
  double opened_ = -1;
};

// A dialog that holds the game's input while it exists. Deletes itself after Close().
class Panel : public rex::ui::ImGuiDialog {
 protected:
  explicit Panel(rex::ui::ImGuiDrawer* drawer);
  ~Panel() override;

  Navigator navigator_;
  int selected_ = 0;
};

// Whether a panel is open now.
bool AnyOpen();

// A centered, untitled panel in the panel font, width_em font sizes wide; draw it between the two
// (EndPanel always, whatever BeginPanel returned).
bool BeginPanel(const char* id, float width_em);
void EndPanel();
// The panel font at a size for the display.
float FontSize();
void PushFont(float scale = 1.0f);
void PopFont();

// Grey wrapped text, for notes.
void Dim(const char* text);
// A full-width selectable row; true when clicked. Hovering with a moving mouse selects it.
bool Row(const char* label, int index, int& selected);

// In ReXApp::OnConfigureFonts.
void AddFonts(ImFontAtlas* atlas);

// For InputSystem::SetActiveCallback: whether the game gets the controller and keyboard now.
bool GameGetsInput();

}  // namespace fear::panel
