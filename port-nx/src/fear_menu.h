// F.E.A.R. 360 - two entries added to the game's own main menu: PC Settings and Quit to Desktop.
//
// The 360's main screen (the SDK's CScreenMain) builds its list in code: sub_823FF3C0 (Build,
// method 4 of the table at 0x8201FDE4) adds Campaign, Instant Action, Xbox Live and Options as
// text controls, each hidden until "Press Start" is accepted (command 19 in OnCommand,
// sub_823FF7E8, shows them). Three [[entrypoint.midasm_hook]]s in fear_manifest.toml:
//   FearMenuAddItems   at the end of Build (0x823FF7A8): adds our two controls after Options,
//                      with the same layout arguments, by calling the game's own functions
//                      (CBaseScreen::AddTextItem with a text, AddControl, Show(false))
//   FearMenuShowItems  where command 19 shows the game's items (0x823FF910): shows ours too
//   FearMenuCommand    at OnCommand's entry: our commands go to the host (SetActions) instead
//                      of the game
// The host side (the settings panel, the quit confirmation) lives in fear_settings.*.

#pragma once

#include <functional>

#include <rex/ppc/context.h>

namespace fear::menu {

// What our two entries do, called on the game thread that handles the menu command (the
// handlers move to the UI thread themselves). Set once, after the runtime is up.
void SetActions(std::function<void()> settings, std::function<void()> quit);

}  // namespace fear::menu

void FearMenuAddItems(PPCRegister& r30);
void FearMenuShowItems(PPCRegister& r31);
bool FearMenuCommand(PPCRegister& r3, PPCRegister& r4);
