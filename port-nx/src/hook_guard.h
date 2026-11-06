// F.E.A.R. 360 - tripwires for the hooks that replace a whole game function (REX_HOOK_RAW). A
// hook on a real function that isn't the one it was written for still builds and runs, so each
// such hook first checks something that must be true on the right function. When it isn't, the
// check logs once and the hook runs the game's function unchanged, as if its fix were off.
//
//   if (!FEAR_HOOK_HOLDS("GetWeaponSlot", capacity <= 10)) {
//     original(ctx, base);
//     return;
//   }
//
// Only what must hold if the match is right, and cheap: a few comparisons. The hooks placed inside
// a function ([[entrypoint.midasm_hook]]) have the startup check of the code at their addresses
// instead (image_check.h), as every hook does.

#pragma once

#include <atomic>

#include <rex/logging.h>

namespace fear::hook_guard {

// Whether the check held; a failed one is logged the first time (logged is that check's own flag).
inline bool Holds(bool holds, std::atomic<bool>& logged, const char* hook, const char* expected) {
  if (holds) return true;
  if (!logged.exchange(true)) {
    REXLOG_ERROR("F.E.A.R.: the {} hook found what its function shouldn't have (expected {}): its "
                 "fix is skipped. Wrong function or an unexpected game state",
                 hook, expected);
  }
  return false;
}

}  // namespace fear::hook_guard

// The condition's own text is the "expected" in the log line.
#define FEAR_HOOK_HOLDS(hook, condition)                                  \
  ([&] {                                                                  \
    static std::atomic<bool> logged{false};                               \
    return fear::hook_guard::Holds((condition), logged, hook, #condition); \
  }())
