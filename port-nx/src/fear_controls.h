// F.E.A.R. 360 - PC Settings' Controls page: the keyboard and mouse (fear_mkb.h), with the
// options PC F.E.A.R.'s Controls screens have.
//
// The mouse: sensitivity (a slider, also dragged with the mouse), up and down speed, invert, and
// smoothing. The keys: every action the keyboard drives, by the PC's groups (movement, weapons,
// actions, interface), two keys each. Choosing a key (Enter, a click) waits for the next key,
// mouse button or wheel notch, which then drives that action; a key another action had moves
// over. Escape cancels the wait and stays the pause menu's key, as on PC; Delete or Backspace
// clears the chosen key. Reset to defaults brings back the PC's layout (the 360's own profile
// defaults).
//
// Nothing applies until the panel saves: then the page's values go to settings.toml with the
// rest, and to the running game at once (the keys and the mouse are read live).

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fear_mkb.h"

namespace fear::controls {

// Whether the page is offered: F.E.A.R. with the keyboard and mouse on.
bool Available();

class Page {
 public:
  Page();
  ~Page();

  enum class Result { kStay, kBack, kSave };
  // Draws the page between the panel's BeginPanel and EndPanel, taking the panel navigator's
  // input for this frame.
  Result Draw(uint32_t input);

  bool Changed() const;
  // What to save and apply: (cvar, value) for every setting on the page.
  std::vector<std::pair<std::string, std::string>> Values() const;

 private:
  struct State {
    double sensitivity = 1.0;
    double vertical = 1.0;
    bool invert = false;
    double smoothing = 0.0;
    std::vector<std::vector<std::string>> keys;  // by action: key names, slot by slot ("" empty)
    bool operator==(const State&) const = default;
  };

  void Step(int row, int direction);
  void StartCapture();
  void StopCapture();
  void Bind(size_t action, int slot, const std::string& name);
  void Clear(size_t action, int slot);
  void ResetToDefaults();
  int Rows() const;
  std::string Note() const;

  std::vector<fear::mkb::ActionInfo> actions_;
  State state_;
  State saved_;  // as the page opened

  int selected_ = 0;
  int slot_ = 0;
  bool capturing_ = false;
  double quiet_until_ = 0.0;  // after a capture, the key that ended it doesn't also act
  bool scroll_to_selected_ = false;
  std::string message_;
};

}  // namespace fear::controls
