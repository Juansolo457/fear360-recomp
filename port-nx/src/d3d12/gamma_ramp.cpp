// D3D12 renderer: the display controller's gamma ramp (gamma_ramp.h).
#include "gamma_ramp.h"

#include "../native/native_system.h"

#include <rex/logging.h>

#include <algorithm>
#include <cstring>

namespace fear::d3d12 {

double PwlOut(const uint32_t* words, uint32_t channel, uint32_t x) {
  const uint32_t w = words[(x >> 3) * 3 + channel];
  return std::min(1.0, ((w & 0xFFFFu) + (x & 7u) * (w >> 16) / 8.0) / 65472.0);
}

bool GammaRamp::Read(RampMode mode, uint64_t frame, uint32_t* out) {
  const uint32_t* words = mode == kRampTable ? fear_nx::native::GammaRamp256() : nullptr;
  if (!words) {
    if (mode == kRampPwl && !pwl_logged_) {
      pwl_logged_ = true;
      REXLOG_WARN("D3D12 present: the front buffer asks for the PWL gamma ramp, which fear-nx "
                  "does not track; shown without a ramp");
    }
    return false;
  }
  const uint32_t count = mode == kRampPwl ? kPwlWords : kTableWords;
  // The ring thread writes the ramp as it executes the guest's register writes; a ramp read
  // during an update shows for one frame, the next frame reads the finished one.
  uint32_t ramp[kPwlWords];
  for (uint32_t i = 0; i < count; ++i) ramp[i] = __atomic_load_n(words + i, __ATOMIC_RELAXED);
  std::memcpy(out, ramp, count * 4);
  if (mode != last_mode_ || std::memcmp(ramp, last_, count * 4)) {
    last_mode_ = mode;
    std::memcpy(last_, ramp, count * 4);
    for (uint32_t c = 0; c < 3; ++c)
      white_[c] = mode == kRampPwl ? float(PwlOut(ramp, c, 1023))
                                   : float((ramp[255] >> (20 - 10 * c)) & 1023) / 1023.0f;
    if (changes_logged_ < 8) {
      ++changes_logged_;
      if (mode == kRampPwl)
        REXLOG_INFO(
            "D3D12 present: PWL gamma ramp at frame {}: 0.25/0.5/0.75 -> R {:.3f} "
            "{:.3f} {:.3f}, G {:.3f} {:.3f} {:.3f}, B {:.3f} {:.3f} {:.3f}",
            frame, PwlOut(ramp, 0, 256), PwlOut(ramp, 0, 512), PwlOut(ramp, 0, 767),
            PwlOut(ramp, 1, 256), PwlOut(ramp, 1, 512), PwlOut(ramp, 1, 767), PwlOut(ramp, 2, 256),
            PwlOut(ramp, 2, 512), PwlOut(ramp, 2, 767));
      else
        REXLOG_INFO(
            "D3D12 present: gamma table at frame {}: 64/128/192 -> R {} {} {} (of 1023)",
            frame, (ramp[64] >> 20) & 1023, (ramp[128] >> 20) & 1023, (ramp[192] >> 20) & 1023);
    }
  }
  return true;
}

}  // namespace fear::d3d12
