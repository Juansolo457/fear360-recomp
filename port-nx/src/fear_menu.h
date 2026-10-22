// F.E.A.R. 360 - entries added to the game's own main menu: PC Settings, Switch Game (in a
// collection, fear_collection.h) and Quit to Desktop. F.E.A.R. and F.E.A.R. Files alike.
//
// The 360's main screen (the SDK's CScreenMain) builds its list in code, each item a text control
// hidden until "Press Start" is accepted, when OnCommand shows them. Three
// [[entrypoint.midasm_hook]]s in each fear_manifest.toml:
//   FearMenuAddItems   at the end of Build, where both its paths meet: adds our controls after
//                      Options, with Options' layout arguments, by calling the game's own
//                      functions (CBaseScreen::AddTextItem with a text, AddControl, Show(false))
//   FearMenuShowItems  where Press Start has shown the game's items: shows ours too
//   FearMenuCommand    at OnCommand's entry: our commands go to the host (SetActions) instead
//                      of the game
//                      F.E.A.R.            F.E.A.R. Files
//   Build              sub_823FF3C0        sub_823EAAC0     (method 4 of the screen's table)
//     end              0x823FF7A8          0x823EAF24
//     items            Campaign 12,        Extraction Point 13, Perseus Mandate 14,
//                      Instant Action 13,  Instant Action 15, Xbox Live 16, Options 17
//                      Xbox Live 14, Options 15
//   OnCommand          sub_823FF7E8        sub_823EAF68     (method 1)
//     Press Start      command 19,         command 21,
//                      shown at 0x823FF910 shown at 0x823EB274
// The host side (the settings panel, the quit confirmation) lives in fear_settings.*, the switch
// in fear_collection.*.

#pragma once

#include <functional>

#include <rex/ppc/context.h>

namespace fear::menu {

// What our entries do, called on the game thread that handles the menu command (the handlers
// move to the UI thread themselves). Set once, after the runtime is up.
struct Actions {
  std::function<void()> settings;
  std::function<void()> quit;
  std::function<void()> switch_game;
};
void SetActions(Actions actions);

}  // namespace fear::menu

void FearMenuAddItems(PPCRegister& r30);
void FearMenuShowItems(PPCRegister& r31);
bool FearMenuCommand(PPCRegister& r3, PPCRegister& r4);
