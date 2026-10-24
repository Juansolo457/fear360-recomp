// F.E.A.R. 360 - fixes for game code that misbehaves at high frame rates. See high_fps.h.

#include "high_fps.h"

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fear_title.h"

REXCVAR_DEFINE_BOOL(fear_ai_every_frame, true, "F.E.A.R.",
                    "Update enemies every frame (EchoPatch's AI fix). Off = as shipped: above "
                    "100 fps the AI updates every other frame and reacts slowly");

namespace {
// CCharacter's m_bSevered, set once DamageTracker::CheckForSever has cut a piece off (high_fps.h).
constexpr uint32_t kSeveredOffset = fear::title::kFiles ? 0x714 : 0x680;
}  // namespace

void FearAiNextUpdate(PPCRegister& r3, PPCRegister& f1) {
  // Only the AI's own delay (0.01 s); 0 means "stop updating" and stays.
  if (!REXCVAR_GET(fear_ai_every_frame) || !(f1.f64 > 0.0 && f1.f64 <= 0.0101)) return;
  // A body with a severed piece keeps the 0.01, as EchoPatch does: updated every frame, its
  // ragdoll moves erratically.
  if (r3.u32 && rex::system::kernel_memory()->virtual_membase()[r3.u32 + kSeveredOffset]) {
    static std::atomic<bool> logged_severed{false};
    if (!logged_severed.exchange(true)) {
      REXLOG_INFO("F.E.A.R.: a severed body keeps the game's AI update delay ({} s)", f1.f64);
    }
    return;
  }
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("F.E.A.R.: AI updates every frame (fear_ai_every_frame; the game asked for {} s)",
                f1.f64);
  }
  f1.f64 = 1e-8;
}
