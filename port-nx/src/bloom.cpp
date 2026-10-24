// F.E.A.R. 360 - the screen bloom's strength and threshold. See bloom.h.

#include "bloom.h"

#include <atomic>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_DOUBLE(fear_bloom_intensity, 0.0, "F.E.A.R.",
                      "Screen bloom strength: 0 = off, as on PC (the default); below 0 = the "
                      "360's (0.2 unless ScreenBloom is set)");
REXCVAR_DEFINE_DOUBLE(fear_bloom_threshold, -1.0, "F.E.A.R.",
                      "Brightness a pixel needs to bloom, 0-1 (below 0 = the game's: 0.8 unless "
                      "ScreenBloom_Threshold is set)");

void FearBloomParams(PPCRegister& f1, PPCRegister& f31) {
  const double intensity = REXCVAR_GET(fear_bloom_intensity);
  const double threshold = REXCVAR_GET(fear_bloom_threshold);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO("F.E.A.R.: screen bloom strength {} threshold {} (the game's {} and {})",
                intensity >= 0 ? intensity : f31.f64, threshold >= 0 ? threshold : f1.f64,
                f31.f64, f1.f64);
  }
  if (intensity >= 0) f31.f64 = intensity;
  if (threshold >= 0) f1.f64 = threshold;
}
