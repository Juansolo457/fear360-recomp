// F.E.A.R. 360 - entries added to the game's own main menu: Bonus Mission (F.E.A.R. only), PC
// Settings, Switch Game (in a collection, fear_collection.h) and Quit to Desktop. F.E.A.R. and
// F.E.A.R. Files alike.
//
// The 360's main screen (the SDK's CScreenMain) builds its list in code, each item a text control
// hidden until "Press Start" is accepted, when OnCommand shows them. Three
// [[entrypoint.midasm_hook]]s in each fear_manifest.toml:
//   FearMenuAddItems   at the end of Build, where both its paths meet: adds our controls after
//                      Options, with Options' layout arguments, by calling the game's own
//                      functions (CBaseScreen::AddTextItem with a text or the game's string ID,
//                      AddControl, Show(false))
//   FearMenuShowItems  where Press Start has shown the game's items: shows ours too
//   FearMenuCommand    at OnCommand's entry: our commands go to the host (SetActions) instead
//                      of the game; Bonus Mission opens the Campaign screen and picks its own
//                      Bonus Mission item there (the difficulty list, then Holiday; #111)
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

// The hooks take every register they read, r1 included: under the translator's
// non_volatile_as_local the non-volatiles are the generated function's locals, not the context's.
void FearMenuAddItems(PPCRegister& r1, PPCRegister& r23, PPCRegister& r24, PPCRegister& r25,
                      PPCRegister& r26, PPCRegister& r27, PPCRegister& r28, PPCRegister& r29,
                      PPCRegister& r30);
void FearMenuShowItems(PPCRegister& r1, PPCRegister& r31);
bool FearMenuCommand(PPCRegister& r1, PPCRegister& r3, PPCRegister& r4);
