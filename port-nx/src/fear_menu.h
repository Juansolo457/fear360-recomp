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
//
// The mouse (#109). The 360 kept the PC menus' mouse code; only the window messages that called
// it are gone. CBaseScreen's handlers find the control under a point (in the screen's pixels),
// select it and click it, and CMessageBox's window does the same while a message box shows. A
// fourth hook, FearMenuMouse, at CScreenMgr::UpdateInterfaceSFX's entry (once a frame while a menu
// screen is up), hands them what the mouse did since the last frame (fear_mkb.h), from the window
// to the screen's pixels (the display mode, fitted to the window as the presenter shows it).
// The 360 replaced the PC screens' Back button with button prompts ("B Back", "A Select"; text
// controls at CBaseScreen +0x1B4 and +0x1B8 in both games, outside the screen's list): a click on
// one presses its button, as a controller would (#110). On the main screen the game's items are
// wider than their text and reach into our column; the screen takes the first item under the
// point, so a point on one of ours goes to it directly (SetSelection, its OnLButtonUp).
//
// The menus over play (the pause menu and the rest) aren't screens but CMenuMgr's, with the PC's
// mouse handlers too: CInterfaceMgr's user menus (+0x5AC, open in interface state 5, the pause
// menu) and system menus (+0x32C8, state 1), methods 8 OnLButtonDown, 9 OnLButtonUp,
// 11 OnRButtonDown, 12 OnRButtonUp, 14 OnMouseMove, 15 OnMouseWheel, each passing the event to
// the open menu (+8). A fifth hook, FearMenuMouseInGame, at the binding manager's per-frame
// update (sub_82336FA8, every state) hands them the mouse in those states. The menu's prompts are
// text controls inside it (CBaseMenu +0xC6C B, +0xD4C A). F.E.A.R. only so far. A message box's
// prompts ("A Yes", "B No") are text controls it creates (CMessageBox +0x574 A, +0x578 B), clicked
// the same way wherever the box shows.
//                        F.E.A.R.            F.E.A.R. Files
//   UpdateInterfaceSFX   sub_8245FF30        sub_82454C28     (CScreenMgr method 28; Files 29)
//   g_pInterfaceMgr      0x83370664          0x834227DC
// Both: the current screen at CScreenMgr +0x60; CBaseScreen methods 17 OnLButtonDown,
// 18 OnLButtonUp, 20 OnRButtonDown, 21 OnRButtonUp, 23 OnMouseMove, 24 OnMouseWheel (one later
// than the PC's); CInterfaceMgr's message box at +0x42F0 (shown: byte +0x580; its window +0x2C,
// CLTGUICtrl methods 30 OnLButtonDown, 31 OnLButtonUp, 36 OnMouseMove).

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
void FearMenuMouse(PPCRegister& r1, PPCRegister& r3);
void FearMenuMouseInGame(PPCRegister& r1);
