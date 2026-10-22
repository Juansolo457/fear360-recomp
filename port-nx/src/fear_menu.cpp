// F.E.A.R. 360 - PC Settings and Quit to Desktop in the main menu. See fear_menu.h.

#include "fear_menu.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

namespace {

// The 360 executable's functions and data (title 565507D9).
constexpr uint32_t kAddTextItem = 0x824A7288;  // CBaseScreen::AddTextItem(const wchar_t*, ...)
constexpr uint32_t kAddControl = 0x824AA8B0;   // CBaseScreen::AddControl(ctrl)
constexpr uint32_t kShowSlot = 160;            // the control's Show(bool) in its method table
constexpr uint32_t kEmptyHelp = 0x82001C4A;    // "": no help line under the menu
constexpr uint32_t kNoMenuFlag = 432;          // CScreenMain byte: built without the list

constexpr uint32_t kCommandSettings = 9001;
constexpr uint32_t kCommandQuit = 9002;

struct Item {
  const char16_t* text;
  uint32_t command;
};
constexpr Item kItems[] = {{u"PC Settings", kCommandSettings},
                           {u"Quit to Desktop", kCommandQuit}};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

std::mutex g_mutex;
std::function<void()> g_settings;
std::function<void()> g_quit;
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

}  // namespace

namespace fear::menu {

void SetActions(std::function<void()> settings, std::function<void()> quit) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_settings = std::move(settings);
  g_quit = std::move(quit);
}

}  // namespace fear::menu

// At the end of CScreenMain::Build, after Options. The arguments are Options' own: the layout
// struct is passed by value in r5-r10 and on the stack (80: its last word, still in r28), with
// the command and help ID in its first doubleword (112, 116); r23-r28 still hold the rest.
void FearMenuAddItems(PPCRegister& r30) {
  PPCContext* ctx = Context(r30, &PPCContext::r30);
  if (!ctx) return;
  const uint32_t screen = r30.u32;
  if (Load8(screen + kNoMenuFlag)) return;

  const PPCContext saved = *ctx;
  const uint32_t sp = saved.r1.u32;
  uint32_t controls[kItemCount] = {};
  for (size_t i = 0; i < kItemCount; ++i) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_texts[i]) g_texts[i] = GuestText(kItems[i].text);
    }
    if (!g_texts[i]) continue;
    Store32(sp + 80, saved.r28.u32);
    Base()[sp + 95] = 0;      // fixed width: no
    Store32(sp + 100, 0);     // font: the screen's
    Store32(sp + 108, 0);     // font size: the screen's
    Store32(sp + 112, kItems[i].command);
    Store32(sp + 116, kEmptyHelp);
    ctx->r3.u64 = screen;
    ctx->r4.u64 = g_texts[i];
    ctx->r5.u64 = (uint64_t(kItems[i].command) << 32) | kEmptyHelp;
    ctx->r6 = saved.r27;
    ctx->r7 = saved.r26;
    ctx->r8 = saved.r25;
    ctx->r9 = saved.r24;
    ctx->r10 = saved.r23;
    Call(*ctx, kAddTextItem);
    const uint32_t control = ctx->r3.u32;
    if (!control) continue;
    ctx->r3.u64 = screen;
    ctx->r4.u64 = control;
    Call(*ctx, kAddControl);
    Show(*ctx, control, false);  // like the game's items, until Press Start is accepted
    controls[i] = control;
  }
  *ctx = saved;

  std::lock_guard<std::mutex> lock(g_mutex);
  g_screen = screen;
  std::memcpy(g_controls, controls, sizeof(controls));
  REXLOG_INFO("F.E.A.R. menu: added PC Settings ({:08X}) and Quit to Desktop ({:08X})",
              controls[0], controls[1]);
}

// Command 19 (Press Start accepted) has just shown the game's items.
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
  if (command != kCommandSettings && command != kCommandQuit) return false;
  std::function<void()> action;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    action = command == kCommandSettings ? g_settings : g_quit;
  }
  if (action) action();
  r3.u64 = 1;
  return true;
}
