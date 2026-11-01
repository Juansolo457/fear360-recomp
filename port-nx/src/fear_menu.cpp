// F.E.A.R. 360 - Bonus Mission, Recompilation Settings, Switch Game and Quit to Desktop in the main
// menu. See fear_menu.h.

#include "fear_menu.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xvideo.h>
#include <rex/types.h>

#include "fear_collection.h"
#include "fear_mkb.h"
#include "fear_panel.h"
#include "fear_title.h"

namespace {

// Each executable's own functions and data, the same code at different addresses.
struct MenuCode {
  uint32_t add_text_item;  // CBaseScreen::AddTextItem(const wchar_t*, layout, ...)
  // The same with a string ID (the game's own text, translated), the same layout arguments; 0:
  // not looked up.
  uint32_t add_text_id_item;
  uint32_t add_control;    // CBaseScreen::AddControl(ctrl)
  uint32_t empty_help;     // "": no help line under the item
  // Options' layout arguments when its Build ends: the registers that go in r6-r10, and the one
  // whose word goes on the stack (80(r1)).
  int layout[5];
  int stack_word;
  uint32_t game_items;  // the game's own list: Campaign/Extraction Point ... Options
  uint32_t set_selection;  // CBaseScreen::SetSelection(index, find_selectable)
};
// F.E.A.R. (565507D9): Build sub_823FF3C0, OnCommand sub_823FF7E8.
constexpr MenuCode kFear = {0x824A7288, 0x824A86E0, 0x824AA8B0, 0x82001C4A,
                            {27, 26, 25, 24, 23}, 28, 4, 0x824A6F40};
// F.E.A.R. Files (565507E8): Build sub_823EAAC0, OnCommand sub_823EAF68.
constexpr MenuCode kFiles = {0x823E2388, 0, 0x823E5948, 0x82000C38, {28, 27, 26, 25, 24}, 29, 5,
                             0x823E2060};
constexpr MenuCode kCode = fear::title::kFiles ? kFiles : kFear;

constexpr uint32_t kShowSlot = 160;    // the control's Show(bool) in its method table
constexpr uint32_t kNoMenuFlag = 432;  // CScreenMain byte: built without the list

// Our items go in a second column beside the game's (#80): below Options they ran into the help
// line at the bottom of the screen. CBaseScreen::AddControl (both games) places a control at the
// screen's cursor and moves the cursor down by the control's height plus the screen's spacing;
// the cursor is moved to the column's top first and put back after.
constexpr uint32_t kControls = 0xC4;     // CBaseScreen: the controls, a vector (begin +4, end +8)
constexpr uint32_t kCursor = 0x104;      // CBaseScreen: the next control's position (x, y)
constexpr uint32_t kBasePosSlot = 0x3C;  // control method: its position (into r3's buffer)
// The second column's left edge, from the game's first item, in the menu's 640-wide units: past
// the widest game item (Instant action, left of the bright middle of the background; Files:
// Perseus Mandate Campaign, which ends near 357), from screenshots (2026-10-28).
constexpr int32_t kColumnOffset = fear::title::kFiles ? 330 : 220;

// The mouse (#109): see fear_menu.h.
constexpr uint32_t kInterfaceMgr = fear::title::kFiles ? 0x834227DC : 0x83370664;  // g_pInterfaceMgr
constexpr uint32_t kCurrentScreen = 0x60;  // CScreenMgr: the current screen
// CBaseScreen's mouse handlers in its method table: (x, y), the wheel's also its delta.
constexpr uint32_t kScreenLButtonDown = 0x44, kScreenLButtonUp = 0x48, kScreenRButtonDown = 0x50,
                   kScreenRButtonUp = 0x54, kScreenMouseMove = 0x5C, kScreenMouseWheel = 0x60;
// CBaseScreen: its button prompts at the bottom ("B Back", "A Select"), text controls outside
// the screen's list (#110), and CLTGUICtrl's IsOnMe(x, y) in its method table.
constexpr uint32_t kPromptBack = 0x1B4, kPromptSelect = 0x1B8;
constexpr uint32_t kCtrlIsOnMe = 0x68;
constexpr uint32_t kSelection = 0x114;  // CBaseScreen: the selected control's index (16 bits)
// The menus over play (F.E.A.R. only so far): CInterfaceMgr's interface state and its two
// CMenuMgrs, the user menus (the pause menu; open in state 5) and the system menus (state 1).
// Their mouse handlers in CMenuMgr's method table, (x, y), the wheel's also its delta; the open
// menu (a CLTGUIWindow); and its button prompts, text controls inside the menu (CBaseMenu).
constexpr uint32_t kGameState = 8;
constexpr uint32_t kStatePlaying = 1, kStateMenu = 5;
constexpr uint32_t kUserMenus = 0x5AC, kSystemMenus = 0x32C8;
constexpr uint32_t kMenusLButtonDown = 0x20, kMenusLButtonUp = 0x24, kMenusRButtonDown = 0x2C,
                   kMenusRButtonUp = 0x30, kMenusMouseMove = 0x38, kMenusMouseWheel = 0x3C;
constexpr uint32_t kCurrentMenu = 8;
constexpr uint32_t kMenuPromptBack = 0xC6C, kMenuPromptSelect = 0xD4C;
constexpr uint32_t kMessageBox = 0x42F0;        // CInterfaceMgr: its CMessageBox
constexpr uint32_t kMessageBoxShown = 0x580;    // CMessageBox: shown (byte)
constexpr uint32_t kMessageBoxWindow = 0x2C;    // CMessageBox: its window, a CLTGUICtrl
// CMessageBox: its button prompts ("A Yes", "B No"), text controls it creates (pointers).
constexpr uint32_t kMessageBoxPromptA = 0x574, kMessageBoxPromptB = 0x578;
// CLTGUICtrl's mouse handlers in its method table, (x, y).
constexpr uint32_t kCtrlLButtonDown = 0x78, kCtrlLButtonUp = 0x7C, kCtrlMouseMove = 0x90;

constexpr uint32_t kCommandSettings = 9001;
constexpr uint32_t kCommandQuit = 9002;
constexpr uint32_t kCommandSwitch = 9003;
constexpr uint32_t kCommandBonus = 9004;

// The bonus mission, Holiday (F.E.A.R. only, #111), through the Campaign screen's own way in. The
// main menu's Campaign command (12) is CScreenMgr::SetCurrentScreen(2), whose switch runs the
// Campaign screen's OnFocus at once (CScreenMgr::SwitchToScreen); that screen's command 33, its
// "Bonus Mission" item, shows the difficulty list in bonus mode, and a difficulty then starts
// Missions/Shared.BonusMission instead of a new campaign. The Campaign item waits for the
// profile's unlock (finishing Interval 05, #39); ours is open from the start.
constexpr uint32_t kBonusText = 0x820295DC;  // "IDS_BONUSMISSION"
constexpr uint32_t kBonusHelp = 0x820295C4;  // "IDS_HELP_BONUSMISSION"
constexpr uint32_t kScreenMgr = 0xC;          // CBaseScreen: its screen manager
constexpr uint32_t kSetCurrentScreen = 0x50;  // CScreenMgr: SetCurrentScreen(screen ID)
constexpr uint32_t kScreenCampaign = 2;
constexpr uint32_t kOnCommandSlot = 4;  // a screen's OnCommand(command, param1, param2)
constexpr uint32_t kCampaignOnCommand = 0x824C2830;  // CScreenSingle::OnCommand
constexpr uint32_t kCampaignBonus = 33;

// The help line under the menu while an item is selected. A control's help is a string ID, which
// the screen looks up in the game's string table (an unknown one shows "[ERROR: Invalid String
// ID]"): ours get IDs of their own, and FearMenuHelpText answers the lookup for them.
struct Item {
  const char16_t* text;
  uint32_t command;
  const char* help_id;
  const char16_t* help;
  // Or the game's own string IDs, text and help (its translations; F.E.A.R.'s image): 0, ours.
  uint32_t game_text = 0;
  uint32_t game_help = 0;
};
constexpr Item kItems[] = {
    {u"Bonus Mission", kCommandBonus, nullptr, nullptr, kBonusText, kBonusHelp},
    {u"Recompilation Settings", kCommandSettings, "Fear360_Help_PCSettings",
     u"Set display, control and gameplay options added by the recompilation."},
    {u"Switch Game", kCommandSwitch, "Fear360_Help_SwitchGame",
     u"Play the other game in the collection."},
    {u"Quit to Desktop", kCommandQuit, "Fear360_Help_Quit", u"Exit to the desktop."}};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

std::mutex g_mutex;
fear::menu::Actions g_actions;
uint32_t g_screen = 0;
uint32_t g_controls[kItemCount] = {};
uint32_t g_texts[kItemCount] = {};
// Guest copies, kept for the process: the help IDs and their text. FearMenuHelpText reads them
// without the lock (each is written once, before the control that uses it exists).
std::atomic<uint32_t> g_help_ids[kItemCount] = {};
std::atomic<uint32_t> g_helps[kItemCount] = {};

rex::memory::Memory* GuestMemory() { return rex::system::kernel_memory(); }
uint8_t* Base() { return GuestMemory()->virtual_membase(); }

uint32_t Load32(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, Base() + address, 4);
  return rex::byte_swap(value);
}
uint8_t Load8(uint32_t address) { return Base()[address]; }
uint16_t Load16(uint32_t address) {
  uint16_t value;
  std::memcpy(&value, Base() + address, 2);
  return rex::byte_swap(value);
}
void Store32(uint32_t address, uint32_t value) {
  value = rex::byte_swap(value);
  std::memcpy(Base() + address, &value, 4);
}

// A guest wide string (UTF-16, big-endian), kept for the life of the process: the control may
// keep the pointer.
uint32_t GuestText(std::u16string_view text) {
  const uint32_t bytes = uint32_t(text.size() + 1) * 2;
  const uint32_t address = GuestMemory()->SystemHeapAlloc(bytes);
  if (!address) return 0;
  uint8_t* out = Base() + address;
  for (char16_t c : text) {
    *out++ = uint8_t(c >> 8);
    *out++ = uint8_t(c);
  }
  out[0] = out[1] = 0;
  return address;
}

// A guest C string, kept for the life of the process.
uint32_t GuestCString(std::string_view text) {
  const uint32_t address = GuestMemory()->SystemHeapAlloc(uint32_t(text.size() + 1));
  if (!address) return 0;
  std::memcpy(Base() + address, text.data(), text.size());
  Base()[address + text.size()] = 0;
  return address;
}

// A missing function stops the game with a crash report naming the address, as any call does.
void Call(PPCContext& ctx, uint32_t address) {
  ctx.last_indirect_target = address;
  rex::runtime::ResolveIndirectFunction(address)(ctx, Base());
}

void Show(PPCContext& ctx, uint32_t control, bool show) {
  ctx.r3.u64 = control;
  ctx.r4.u64 = show ? 1 : 0;
  Call(ctx, Load32(Load32(control) + kShowSlot));
}

// The non-volatile registers Build leaves the layout arguments in, as the hook was handed them.
struct Gprs {
  const PPCRegister* r[7];  // r23-r29
  const PPCRegister& operator()(int n) const { return *r[n - 23]; }
};
constexpr bool InGprs(const MenuCode& code) {
  for (int n : code.layout) {
    if (n < 23 || n > 29) return false;
  }
  return code.stack_word >= 23 && code.stack_word <= 29;
}
static_assert(InGprs(kFear) && InGprs(kFiles), "FearMenuAddItems is handed r23-r29");

// The thread's context, if the hook was handed this thread's registers. r1 is the one checked: it
// stays in the context whatever the translator's options (the non-volatiles may be locals).
PPCContext* Context(const PPCRegister& r1) {
  PPCContext* ctx = rex::runtime::current_ppc_context();
  if (!ctx || &ctx->r1 != &r1) {
    static bool logged = false;
    if (!logged) REXLOG_ERROR("F.E.A.R. menu: hook registers are not the thread's context");
    logged = true;
    return nullptr;
  }
  return ctx;
}

// Switch Game only in a collection with another game to start (fear_collection.h); the game's own
// texts only where they're mapped (the bonus mission: F.E.A.R.).
bool Offered(const Item& item) {
  if (item.game_text && !kCode.add_text_id_item) return false;
  return item.command != kCommandSwitch || fear::collection::Enabled();
}

// The bonus mission (see kBonusText): from the main screen to the Campaign screen, then its Bonus
// Mission command, as if picked there.
void StartBonus(PPCContext& ctx, uint32_t screen) {
  const uint32_t screens = Load32(screen + kScreenMgr);
  if (!screens) return;
  const PPCContext saved = ctx;
  ctx.r3.u64 = screens;
  ctx.r4.u64 = kScreenCampaign;
  Call(ctx, Load32(Load32(screens) + kSetCurrentScreen));
  const uint32_t campaign = Load32(screens + kCurrentScreen);
  if (campaign && Load32(Load32(campaign) + kOnCommandSlot) == kCampaignOnCommand) {
    ctx.r3.u64 = campaign;
    ctx.r4.u64 = kCampaignBonus;
    ctx.r5.u64 = 0;
    ctx.r6.u64 = 0;
    Call(ctx, kCampaignOnCommand);
    REXLOG_INFO("F.E.A.R. menu: bonus mission: the Campaign screen's difficulty list");
  } else {
    REXLOG_ERROR("F.E.A.R. menu: bonus mission: the Campaign screen didn't open");
  }
  ctx = saved;
}

// A window position on the game's screen, in its pixels, as the presenter shows the screen: the
// display mode fitted to the window, centred, aspect kept.
void ToScreen(const fear::mkb::MenuMouse& m, uint32_t screen_width, uint32_t screen_height,
              int32_t& x, int32_t& y) {
  const double sw = screen_width, sh = screen_height;
  const double scale = std::min(double(m.width) / sw, double(m.height) / sh);
  const double left = (double(m.width) - sw * scale) * 0.5;
  const double top = (double(m.height) - sh * scale) * 0.5;
  x = int32_t(std::floor((m.x - left) / scale));
  y = int32_t(std::floor((m.y - top) / scale));
}

// The screen's size for the mouse, in its pixels: the display mode's. Logged the first time.
void ScreenSize(const fear::mkb::MenuMouse& first, uint32_t& width, uint32_t& height) {
  rex::system::X_VIDEO_MODE mode;
  rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
  width = std::max(1u, uint32_t(mode.display_width));
  height = std::max(1u, uint32_t(mode.display_height));
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("F.E.A.R. menu: the mouse on the menus ({}x{} window, {}x{} screen)", first.width,
                first.height, width, height);
  }
}

// The message box's window takes the left button and moves while one shows: its method for the
// event, or 0.
uint32_t MessageBoxSlot(fear::mkb::MenuMouse::Kind kind) {
  using Kind = fear::mkb::MenuMouse::Kind;
  return kind == Kind::kMove       ? kCtrlMouseMove
         : kind == Kind::kLeftDown ? kCtrlLButtonDown
         : kind == Kind::kLeftUp   ? kCtrlLButtonUp
                                   : 0;
}

// Which of a screen's or menu's button prompts (controls; 0: none) is at (x, y), shown: B (Back
// or Cancel), A, or none.
enum class Prompt { kNone, kBack, kSelect };
Prompt PromptAt(PPCContext& ctx, uint32_t back, uint32_t select, int32_t x, int32_t y) {
  for (const auto& [control, prompt] :
       {std::pair{back, Prompt::kBack}, std::pair{select, Prompt::kSelect}}) {
    if (!control) continue;
    ctx.r3.u64 = control;
    ctx.r4.s64 = x;
    ctx.r5.s64 = y;
    Call(ctx, Load32(Load32(control) + kCtrlIsOnMe));
    if (ctx.r3.u32 & 0xFF) return prompt;
  }
  return Prompt::kNone;
}
Prompt g_prompt_down = Prompt::kNone;  // the prompt the left button went down on (game thread)

// The prompts stand in for the PC's Back button: a click on one (down and up on it) presses its
// button, and the screen or menu doesn't get that click. True: the event was the prompts'.
bool PromptClick(PPCContext& ctx, fear::mkb::MenuMouse::Kind kind, uint32_t back,
                 uint32_t select, int32_t x, int32_t y) {
  using Kind = fear::mkb::MenuMouse::Kind;
  if (kind == Kind::kLeftDown) {
    g_prompt_down = PromptAt(ctx, back, select, x, y);
    return g_prompt_down != Prompt::kNone;
  }
  if (kind != Kind::kLeftUp || g_prompt_down == Prompt::kNone) return false;
  if (PromptAt(ctx, back, select, x, y) == g_prompt_down) {
    fear::mkb::PressMenuPad(g_prompt_down == Prompt::kBack ? fear::mkb::MenuPad::kB
                                                           : fear::mkb::MenuPad::kA);
  }
  g_prompt_down = Prompt::kNone;
  return true;
}

// While a message box shows, the mouse is its: its prompts ("A Yes", "B No") are clicked like a
// screen's, and its window takes the left button and moves. False: no message box shows.
bool ToMessageBox(PPCContext& ctx, uint32_t interface_mgr, const fear::mkb::MenuMouse& m,
                  int32_t x, int32_t y) {
  const uint32_t box = interface_mgr + kMessageBox;
  if (!interface_mgr || !Load8(box + kMessageBoxShown)) return false;
  if (PromptClick(ctx, m.kind, Load32(box + kMessageBoxPromptB), Load32(box + kMessageBoxPromptA),
                  x, y)) {
    return true;
  }
  const uint32_t window = box + kMessageBoxWindow;
  if (const uint32_t slot = MessageBoxSlot(m.kind)) {
    ctx.r3.u64 = window;
    ctx.r4.s64 = x;
    ctx.r5.s64 = y;
    Call(ctx, Load32(Load32(window) + slot));
  }
  return true;
}

// Which of our items is at (x, y) on the main screen, or 0.
uint32_t OurItemAt(PPCContext& ctx, uint32_t screen, int32_t x, int32_t y) {
  uint32_t controls[kItemCount];
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (screen != g_screen) return 0;
    std::memcpy(controls, g_controls, sizeof(controls));
  }
  for (uint32_t control : controls) {
    if (!control) continue;
    ctx.r3.u64 = control;
    ctx.r4.s64 = x;
    ctx.r5.s64 = y;
    Call(ctx, Load32(Load32(control) + kCtrlIsOnMe));
    if (ctx.r3.u32 & 0xFF) return control;
  }
  return 0;
}

// Selects one of the screen's controls, as pointing at it does.
void Select(PPCContext& ctx, uint32_t screen, uint32_t control) {
  const uint32_t begin = Load32(screen + kControls + 4), end = Load32(screen + kControls + 8);
  for (uint32_t i = 0; begin && begin + 4 * i < end; ++i) {
    if (Load32(begin + 4 * i) != control) continue;
    if (Load16(screen + kSelection) != i) {
      ctx.r3.u64 = screen;
      ctx.r4.u64 = i;
      ctx.r5.u64 = 0;
      Call(ctx, kCode.set_selection);
    }
    return;
  }
}
uint32_t g_item_down = 0;  // our item the left button went down on (game thread)

}  // namespace

namespace fear::menu {

void SetActions(Actions actions) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_actions = std::move(actions);
}

}  // namespace fear::menu

// At the end of CScreenMain::Build, after Options. The arguments are Options' own: the layout
// struct is passed by value in r5-r10 and on the stack (80: its last word), with the command and
// help ID in its first doubleword (112, 116); the non-volatile registers still hold the rest.
void FearMenuAddItems(PPCRegister& r1, PPCRegister& r23, PPCRegister& r24, PPCRegister& r25,
                      PPCRegister& r26, PPCRegister& r27, PPCRegister& r28, PPCRegister& r29,
                      PPCRegister& r30) {
  PPCContext* ctx = Context(r1);
  if (!ctx) return;
  const uint32_t screen = r30.u32;
  if (Load8(screen + kNoMenuFlag)) return;

  const Gprs gpr = {{&r23, &r24, &r25, &r26, &r27, &r28, &r29}};
  const PPCContext saved = *ctx;
  const uint32_t sp = saved.r1.u32;

  // The second column's top: the game's first item's position.
  const uint32_t cursor_x = Load32(screen + kCursor), cursor_y = Load32(screen + kCursor + 4);
  const uint32_t begin = Load32(screen + kControls + 4), end = Load32(screen + kControls + 8);
  bool column = false;
  if (end > begin && (end - begin) / 4 >= kCode.game_items) {
    const uint32_t first = Load32(end - 4 * kCode.game_items);
    static uint32_t pos = 0;  // the method's output (8 bytes), kept for the process
    if (!pos) pos = GuestMemory()->SystemHeapAlloc(8);
    if (first && pos) {
      ctx->r3.u64 = pos;
      ctx->r4.u64 = first;
      Call(*ctx, Load32(Load32(first) + kBasePosSlot));
      const int32_t x = int32_t(Load32(pos)), y = int32_t(Load32(pos + 4));
      REXLOG_INFO("F.E.A.R. menu: first item at ({}, {}), cursor ({}, {}), spacing {}", x, y,
                  int32_t(cursor_x), int32_t(cursor_y), int32_t(Load32(screen + 0xD4)));
      Store32(screen + kCursor, uint32_t(x + kColumnOffset));
      Store32(screen + kCursor + 4, uint32_t(y));
      column = true;
    }
  }
  uint32_t controls[kItemCount] = {};
  std::vector<std::u16string_view> added;
  for (size_t i = 0; i < kItemCount; ++i) {
    if (!Offered(kItems[i])) continue;
    uint32_t text = kItems[i].game_text, help = kItems[i].game_help;
    if (!text) {
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_texts[i]) g_texts[i] = GuestText(kItems[i].text);
        if (!g_helps[i]) g_helps[i] = GuestText(kItems[i].help);
        if (g_helps[i] && !g_help_ids[i]) g_help_ids[i] = GuestCString(kItems[i].help_id);
      }
      if (!g_texts[i]) continue;
      text = g_texts[i];
      help = g_help_ids[i] ? g_help_ids[i].load() : kCode.empty_help;
    }
    Store32(sp + 80, gpr(kCode.stack_word).u32);
    Base()[sp + 95] = 0;      // fixed width: no
    Store32(sp + 100, 0);     // font: the screen's
    Store32(sp + 108, 0);     // font size: the screen's
    Store32(sp + 112, kItems[i].command);
    Store32(sp + 116, help);
    ctx->r3.u64 = screen;
    ctx->r4.u64 = text;
    ctx->r5.u64 = (uint64_t(kItems[i].command) << 32) | help;
    ctx->r6 = gpr(kCode.layout[0]);
    ctx->r7 = gpr(kCode.layout[1]);
    ctx->r8 = gpr(kCode.layout[2]);
    ctx->r9 = gpr(kCode.layout[3]);
    ctx->r10 = gpr(kCode.layout[4]);
    Call(*ctx, kItems[i].game_text ? kCode.add_text_id_item : kCode.add_text_item);
    const uint32_t control = ctx->r3.u32;
    if (!control) continue;
    ctx->r3.u64 = screen;
    ctx->r4.u64 = control;
    Call(*ctx, kCode.add_control);
    Show(*ctx, control, false);  // like the game's items, until Press Start is accepted
    controls[i] = control;
    added.push_back(kItems[i].text);
  }
  if (column) {
    Store32(screen + kCursor, cursor_x);
    Store32(screen + kCursor + 4, cursor_y);
  }
  *ctx = saved;

  std::lock_guard<std::mutex> lock(g_mutex);
  g_screen = screen;
  std::memcpy(g_controls, controls, sizeof(controls));
  REXLOG_INFO("F.E.A.R. menu: added {} items to the main menu", added.size());
}

// Press Start accepted (F.E.A.R.: command 19; Files: 21) has just shown the game's items.
void FearMenuShowItems(PPCRegister& r1, PPCRegister& r31) {
  PPCContext* ctx = Context(r1);
  if (!ctx) return;
  uint32_t controls[kItemCount];
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (r31.u32 != g_screen) return;
    std::memcpy(controls, g_controls, sizeof(controls));
  }
  const PPCContext saved = *ctx;
  for (uint32_t control : controls) {
    if (control) Show(*ctx, control, true);
  }
  *ctx = saved;
}

// The string table's lookup (table, ID) -> text, at its start: our help IDs return our text
// without the table. The screen passes the control's own ID pointer, so the address is enough.
bool FearMenuHelpText(PPCRegister& r3, PPCRegister& r4) {
  const uint32_t id = r4.u32;
  if (!id) return false;
  for (size_t i = 0; i < kItemCount; ++i) {
    // Acquire: the ID is stored after its text (FearMenuAddItems), so the text is there too.
    if (g_help_ids[i].load(std::memory_order_acquire) == id) {
      r3.u64 = g_helps[i].load(std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}

// CScreenMgr::UpdateInterfaceSFX at its entry, once a frame while a menu screen is up (r3: the
// screen manager): what the mouse did since the last frame goes to the PC's handlers, as
// CInterfaceMgr's did on PC: to the message box's window while one shows (left button and moves
// only), otherwise to the current screen.
void FearMenuMouse(PPCRegister& r1, PPCRegister& r3) {
  using Kind = fear::mkb::MenuMouse::Kind;
  std::vector<fear::mkb::MenuMouse> events = fear::mkb::TakeMenuMouse();
  if (events.empty() || fear::panel::AnyOpen()) return;  // a panel of ours has the mouse
  PPCContext* ctx = Context(r1);
  if (!ctx) return;
  const uint32_t screens = r3.u32;
  uint32_t screen_width = 0, screen_height = 0;
  ScreenSize(events[0], screen_width, screen_height);

  const PPCContext saved = *ctx;
  for (const fear::mkb::MenuMouse& m : events) {
    int32_t x = 0, y = 0;
    ToScreen(m, screen_width, screen_height, x, y);
    // Read again for each: a click can change the screen or open a message box.
    if (ToMessageBox(*ctx, Load32(kInterfaceMgr), m, x, y)) continue;
    const uint32_t target = Load32(screens + kCurrentScreen);
    if (!target || PromptClick(*ctx, m.kind, Load32(target + kPromptBack),
                               Load32(target + kPromptSelect), x, y)) {
      continue;
    }
    // Our items on the main screen: the game's items are wider than their text and reach into our
    // column, and the screen takes the first item under the point, so theirs would win. A point
    // on one of ours goes to it here: a move selects it, a click (down and up) clicks it.
    if (m.kind == Kind::kLeftUp && g_item_down) {
      if (OurItemAt(*ctx, target, x, y) == g_item_down) {
        ctx->r3.u64 = g_item_down;
        ctx->r4.s64 = x;
        ctx->r5.s64 = y;
        Call(*ctx, Load32(Load32(g_item_down) + kCtrlLButtonUp));
      }
      g_item_down = 0;
      continue;
    }
    if (m.kind == Kind::kMove || m.kind == Kind::kLeftDown) {
      if (const uint32_t item = OurItemAt(*ctx, target, x, y)) {
        Select(*ctx, target, item);
        if (m.kind == Kind::kLeftDown) g_item_down = item;
        continue;
      }
    }
    const uint32_t slot = m.kind == Kind::kMove        ? kScreenMouseMove
                          : m.kind == Kind::kLeftDown  ? kScreenLButtonDown
                          : m.kind == Kind::kLeftUp    ? kScreenLButtonUp
                          : m.kind == Kind::kRightDown ? kScreenRButtonDown
                          : m.kind == Kind::kRightUp   ? kScreenRButtonUp
                                                       : kScreenMouseWheel;
    ctx->r3.u64 = target;
    ctx->r4.s64 = x;
    ctx->r5.s64 = y;
    ctx->r6.s64 = m.wheel;
    Call(*ctx, Load32(Load32(target) + slot));
  }
  *ctx = saved;
}

// CBindMgr's update at its entry (F.E.A.R.: sub_82336FA8), once a frame on the game's thread
// whatever the interface is doing: in the menus over play (the pause menu and the rest), what the
// mouse did goes to the open menu's manager, as CInterfaceMgr's handlers did on PC, or to the
// message box over it. Elsewhere it's left for FearMenuMouse (the screens).
void FearMenuMouseInGame(PPCRegister& r1) {
  using Kind = fear::mkb::MenuMouse::Kind;
  if constexpr (fear::title::kFiles) return;  // its menus aren't mapped (no keyboard and mouse)
  const uint32_t interface_mgr = Load32(kInterfaceMgr);
  if (!interface_mgr) return;
  const uint32_t state = Load32(interface_mgr + kGameState);
  const uint32_t menus = state == kStateMenu      ? interface_mgr + kUserMenus
                         : state == kStatePlaying ? interface_mgr + kSystemMenus
                                                  : 0;
  if (!menus) return;
  if (!Load32(menus + kCurrentMenu) && !Load8(interface_mgr + kMessageBox + kMessageBoxShown)) {
    return;
  }
  std::vector<fear::mkb::MenuMouse> events = fear::mkb::TakeMenuMouse();
  if (events.empty() || fear::panel::AnyOpen()) return;
  PPCContext* ctx = Context(r1);
  if (!ctx) return;
  uint32_t screen_width = 0, screen_height = 0;
  ScreenSize(events[0], screen_width, screen_height);

  const PPCContext saved = *ctx;
  for (const fear::mkb::MenuMouse& m : events) {
    int32_t x = 0, y = 0;
    ToScreen(m, screen_width, screen_height, x, y);
    if (ToMessageBox(*ctx, interface_mgr, m, x, y)) continue;
    const uint32_t menu = Load32(menus + kCurrentMenu);
    if (!menu ||
        PromptClick(*ctx, m.kind, menu + kMenuPromptBack, menu + kMenuPromptSelect, x, y)) {
      continue;
    }
    const uint32_t slot = m.kind == Kind::kMove        ? kMenusMouseMove
                          : m.kind == Kind::kLeftDown  ? kMenusLButtonDown
                          : m.kind == Kind::kLeftUp    ? kMenusLButtonUp
                          : m.kind == Kind::kRightDown ? kMenusRButtonDown
                          : m.kind == Kind::kRightUp   ? kMenusRButtonUp
                                                       : kMenusMouseWheel;
    ctx->r3.u64 = menus;
    ctx->r4.s64 = x;
    ctx->r5.s64 = y;
    ctx->r6.s64 = m.wheel;
    Call(*ctx, Load32(Load32(menus) + slot));
  }
  *ctx = saved;
}

// CScreenMain::OnCommand(command, param1, param2): ours return 1 (handled) without the game.
bool FearMenuCommand(PPCRegister& r1, PPCRegister& r3, PPCRegister& r4) {
  const uint32_t command = r4.u32;
  if (command == kCommandBonus) {
    if (PPCContext* ctx = Context(r1)) StartBonus(*ctx, r3.u32);
    r3.u64 = 1;
    return true;
  }
  if (command != kCommandSettings && command != kCommandQuit && command != kCommandSwitch) {
    return false;
  }
  std::function<void()> action;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    action = command == kCommandSettings ? g_actions.settings
             : command == kCommandQuit   ? g_actions.quit
                                         : g_actions.switch_game;
  }
  if (action) action();
  r3.u64 = 1;
  return true;
}
