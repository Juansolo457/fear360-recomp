// F.E.A.R. 360 - fixes for game code that misbehaves at high frame rates. See high_fps.h.

#include "high_fps.h"

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(fear_ai_every_frame, true, "F.E.A.R.",
                    "Update enemies every frame (EchoPatch's AI fix). Off = as shipped: above "
                    "100 fps the AI updates every other frame and reacts slowly");

void FearAiNextUpdate(PPCRegister& f1) {
  // Only the AI's own delay (0.01 s); 0 means "stop updating" and stays.
  if (!REXCVAR_GET(fear_ai_every_frame) || !(f1.f64 > 0.0 && f1.f64 <= 0.0101)) return;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("F.E.A.R.: AI updates every frame (fear_ai_every_frame; the game asked for {} s)",
                f1.f64);
  }
  f1.f64 = 1e-8;
}
