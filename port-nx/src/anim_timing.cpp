// F.E.A.R. 360 - model animation time at any frame rate. See anim_timing.h.

#include "anim_timing.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(fear_anim_exact_time, true, "F.E.A.R.",
                    "Step model animations by the exact time that passed, keeping the fraction of "
                    "a millisecond the engine drops each frame (off = as shipped: slowed "
                    "animations, such as bullet-impact puffs, freeze at high frame rates)");

namespace {

// The fraction of a millisecond left over, by guest object.
struct Carry {
  std::mutex mutex;
  std::unordered_map<uint32_t, double> left;

  // The whole milliseconds to step now; the rest waits for the next step.
  double Take(uint32_t key, double ms) {
    std::lock_guard<std::mutex> lock(mutex);
    // Models and trackers come and go; what a freed one left is under a millisecond.
    if (left.size() > 65536) left.clear();
    double& rest = left[key];
    const double total = ms + rest;
    const double whole = std::floor(total);
    rest = total - whole;
    return whole;
  }
};

Carry g_models, g_trackers;

bool Usable(double ms) { return ms >= 0.0 && ms < 1e9; }  // not NaN, not absurd

}  // namespace

void FearAnimFrameMs(PPCRegister& r3, PPCRegister& f0) {
  if (!REXCVAR_GET(fear_anim_exact_time) || !Usable(f0.f64)) return;
  f0.f64 = g_models.Take(r3.u32, f0.f64);
}

void FearAnimTrackerMs(PPCRegister& r3, PPCRegister& f0) {
  // Most trackers play at rate 1: whole milliseconds in, nothing dropped.
  if (!REXCVAR_GET(fear_anim_exact_time) || !Usable(f0.f64) || f0.f64 == std::floor(f0.f64)) {
    return;
  }
  // Steps the engine would have rounded to nothing (a frozen animation), for the log.
  static std::atomic<uint64_t> rescued{0};
  if (f0.f64 < 1.0) {
    const uint64_t n = rescued.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) == 0) {
      REXLOG_INFO("F.E.A.R.: {} animation steps under a millisecond kept (latest {:.3f} ms)", n,
                  f0.f64);
    }
  }
  f0.f64 = g_trackers.Take(r3.u32, f0.f64);
}
