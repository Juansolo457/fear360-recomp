// F.E.A.R. 360 - PC Settings, Switch Game and Quit to Desktop in the main menu. See fear_menu.h.

#include "fear_menu.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "fear_collection.h"
#include "fear_title.h"

namespace {

// Each executable's own functions and data, the same code at different addresses.
struct MenuCode {
  uint32_t add_text_item;  // CBaseScreen::AddTextItem(const wchar_t*, layout, ...)
  uint32_t add_control;    // CBaseScreen::AddControl(ctrl)
  uint32_t empty_help;     // "": no help line under the item
  // Options' layout arguments when its Build ends: the registers that go in r6-r10, and the one
  // whose word goes on the stack (80(r1)).
  int layout[5];
  int stack_word;
};
// F.E.A.R. (565507D9): Build sub_823FF3C0, OnCommand sub_823FF7E8.
constexpr MenuCode kFear = {0x824A7288, 0x824AA8B0, 0x82001C4A, {27, 26, 25, 24, 23}, 28};
// F.E.A.R. Files (565507E8): Build sub_823EAAC0, OnCommand sub_823EAF68.
constexpr MenuCode kFiles = {0x823E2388, 0x823E5948, 0x82000C38, {28, 27, 26, 25, 24}, 29};
constexpr MenuCode kCode = fear::title::kFiles ? kFiles : kFear;

constexpr uint32_t kShowSlot = 160;    // the control's Show(bool) in its method table
constexpr uint32_t kNoMenuFlag = 432;  // CScreenMain byte: built without the list

constexpr uint32_t kCommandSettings = 9001;
constexpr uint32_t kCommandQuit = 9002;
constexpr uint32_t kCommandSwitch = 9003;

struct Item {
  const char16_t* text;
  uint32_t command;
};
constexpr Item kItems[] = {{u"PC Settings", kCommandSettings},
                           {u"Switch Game", kCommandSwitch},
                           {u"Quit to Desktop", kCommandQuit}};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

std::mutex g_mutex;
fear::menu::Actions g_actions;
uint32_t g_screen = 0;
uint32_t g_controls[kItemCount] = {};
uint32_t g_texts[kItemCount] = {};

rex::memory::Memory* GuestMemory() { return rex::system::kernel_memory(); }
uint8_t* Base() { return GuestMemory()->virtual_membase(); }

uint32_t Load32(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, Base() + address, 4);
  return rex::byte_swap(value);
}
uint8_t Load8(uint32_t address) { return Base()[address]; }
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

// A non-volatile register by number (the layout arguments are in r23-r29).
const PPCRegister& Gpr(const PPCContext& ctx, int n) {
  switch (n) {
    case 23: return ctx.r23;
    case 24: return ctx.r24;
    case 25: return ctx.r25;
    case 26: return ctx.r26;
    case 27: return ctx.r27;
    case 28: return ctx.r28;
    case 29: return ctx.r29;
    case 30: return ctx.r30;
    default: return ctx.r31;
  }
}

// The thread's context, if the hook was handed this thread's registers (the generated code
// keeps them in the context, not in locals).
PPCContext* Context(const PPCRegister& reg, const PPCRegister PPCContext::*field) {
  PPCContext* ctx = rex::runtime::current_ppc_context();
  if (!ctx || &(ctx->*field) != &reg) {
    static bool logged = false;
    if (!logged) REXLOG_ERROR("F.E.A.R. menu: hook registers are not the thread's context");
    logged = true;
    return nullptr;
  }
  return ctx;
}

// Switch Game only in a collection (fear_collection set).
bool Offered(const Item& item) {
  return item.command != kCommandSwitch || fear::collection::Enabled();
}

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
void FearMenuAddItems(PPCRegister& r30) {
  PPCContext* ctx = Context(r30, &PPCContext::r30);
  if (!ctx) return;
  const uint32_t screen = r30.u32;
  if (Load8(screen + kNoMenuFlag)) return;

  const PPCContext saved = *ctx;
  const uint32_t sp = saved.r1.u32;
  uint32_t controls[kItemCount] = {};
  std::vector<std::u16string_view> added;
  for (size_t i = 0; i < kItemCount; ++i) {
    if (!Offered(kItems[i])) continue;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_texts[i]) g_texts[i] = GuestText(kItems[i].text);
    }
    if (!g_texts[i]) continue;
    Store32(sp + 80, Gpr(saved, kCode.stack_word).u32);
    Base()[sp + 95] = 0;      // fixed width: no
    Store32(sp + 100, 0);     // font: the screen's
    Store32(sp + 108, 0);     // font size: the screen's
    Store32(sp + 112, kItems[i].command);
    Store32(sp + 116, kCode.empty_help);
    ctx->r3.u64 = screen;
    ctx->r4.u64 = g_texts[i];
    ctx->r5.u64 = (uint64_t(kItems[i].command) << 32) | kCode.empty_help;
    ctx->r6 = Gpr(saved, kCode.layout[0]);
    ctx->r7 = Gpr(saved, kCode.layout[1]);
    ctx->r8 = Gpr(saved, kCode.layout[2]);
    ctx->r9 = Gpr(saved, kCode.layout[3]);
    ctx->r10 = Gpr(saved, kCode.layout[4]);
    Call(*ctx, kCode.add_text_item);
    const uint32_t control = ctx->r3.u32;
    if (!control) continue;
    ctx->r3.u64 = screen;
    ctx->r4.u64 = control;
    Call(*ctx, kCode.add_control);
    Show(*ctx, control, false);  // like the game's items, until Press Start is accepted
    controls[i] = control;
    added.push_back(kItems[i].text);
  }
  *ctx = saved;

  std::lock_guard<std::mutex> lock(g_mutex);
  g_screen = screen;
  std::memcpy(g_controls, controls, sizeof(controls));
  REXLOG_INFO("F.E.A.R. menu: added {} items to the main menu", added.size());
}

// Press Start accepted (F.E.A.R.: command 19; Files: 21) has just shown the game's items.
void FearMenuShowItems(PPCRegister& r31) {
  PPCContext* ctx = Context(r31, &PPCContext::r31);
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

// CScreenMain::OnCommand(command, param1, param2): ours return 1 (handled) without the game.
bool FearMenuCommand(PPCRegister& r3, PPCRegister& r4) {
  const uint32_t command = r4.u32;
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
