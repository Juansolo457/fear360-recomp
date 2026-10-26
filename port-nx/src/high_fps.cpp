// F.E.A.R. 360 - fixes for game code that misbehaves at high frame rates. See high_fps.h.

#include "high_fps.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fear_title.h"

REXCVAR_DEFINE_BOOL(fear_ai_every_frame, true, "F.E.A.R.",
                    "Update enemies every frame (EchoPatch's AI fix). Off = as shipped: above "
                    "100 fps the AI updates every other frame and reacts slowly");
REXCVAR_DEFINE_BOOL(fear_high_fps_fixes, true, "F.E.A.R.",
                    "EchoPatch's other high frame rate fixes: sliding on slopes, the jump kick, "
                    "speed kept on landing and out of water, water waves and splashes, the slow-mo "
                    "meter (high_fps.h). Off = as shipped");

namespace {

using GuestFn = void (*)(PPCContext&, uint8_t*);
using Clock = std::chrono::steady_clock;

// EchoPatch's step for its frame-time fixes (TARGET_FRAME_TIME).
constexpr double kStep = 1.0 / 60.0;

// CCharacter's m_bSevered, set once DamageTracker::CheckForSever has cut a piece off (high_fps.h).
constexpr uint32_t kSeveredOffset = fear::title::kFiles ? 0x714 : 0x680;
// CMoveMgr's m_bJumped, in both games.
constexpr uint32_t kJumpedOffset = 120;
// CPlayerMgr's slow-mo state (EffectState: entering 0, in 1, exiting 2, out 3) and charge (double),
// and CGameServerShell's m_hSlowMoRecord.
constexpr uint32_t kSlowMoStateOffset = fear::title::kFiles ? 640 : 632;
constexpr uint32_t kSlowMoChargeOffset = fear::title::kFiles ? 872 : 864;
constexpr uint32_t kServerSlowMoRecordOffset = fear::title::kFiles ? 0xEEF0 : 0xB368;
constexpr uint32_t kSlowMoOut = 3;

bool Fixes() { return REXCVAR_GET(fear_high_fps_fixes); }

uint8_t* Membase() { return rex::system::kernel_memory()->virtual_membase(); }

uint32_t LoadWord(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return std::byteswap(value);
}

double LoadDouble(const uint8_t* base, uint32_t address) {
  uint64_t value;
  std::memcpy(&value, base + address, 8);
  return std::bit_cast<double>(std::byteswap(value));
}

void StoreDouble(uint8_t* base, uint32_t address, double number) {
  const uint64_t value = std::byteswap(std::bit_cast<uint64_t>(number));
  std::memcpy(base + address, &value, 8);
}

void LogOnce(std::atomic<bool>& logged, const char* what) {
  if (!logged.exchange(true)) REXLOG_INFO("F.E.A.R.: high frame rate fix in use: {}", what);
}

// The player's slow-mo charge as the client last had it, for the server's update (#13).
std::atomic<double> g_client_charge{0.0};

thread_local bool t_wave_frame_call = false;

// CMoveMgr::UpdateOnGround (#11): once a jump starts, m_bJumped stays set for 1/60 s.
void OnGroundUpdate(PPCContext& ctx, uint8_t* base, GuestFn original) {
  if (!Fixes()) {
    original(ctx, base);
    return;
  }
  static bool was_jumped = false;
  static double held = -1.0;  // how long the jump state has been kept, -1 when not
  static Clock::time_point last{};
  const auto now = Clock::now();
  const double frame =
      last == Clock::time_point{}
          ? 0.0
          : std::clamp(std::chrono::duration<double>(now - last).count(), 0.0, 0.1);
  last = now;
  const uint32_t jumped = ctx.r3.u32 + kJumpedOffset;
  if (!was_jumped && base[jumped]) held = 0.0;
  original(ctx, base);
  if (held >= 0.0) {
    if (held < kStep) {
      if (!base[jumped]) {
        static std::atomic<bool> logged{false};
        LogOnce(logged, "jump state kept for 1/60 s (jump kick)");
      }
      base[jumped] = 1;
      held += frame;
    } else {
      held = -1.0;
    }
  }
  was_jumped = base[jumped] != 0;
}

// CPolyGridFX::UpdateWaveProp (#5): the per-frame call steps the waves at 60 Hz.
void WaveUpdate(PPCContext& ctx, uint8_t* base, GuestFn original) {
  const bool frame_call = std::exchange(t_wave_frame_call, false);
  if (!frame_call || !Fixes()) {
    original(ctx, base);
    return;
  }
  const uint32_t grid = ctx.r3.u32;
  int steps = 0;
  {
    static std::mutex mutex;
    static std::unordered_map<uint32_t, double> pending;  // by water surface
    std::lock_guard<std::mutex> lock(mutex);
    double& left = pending[grid];
    left = std::min(left + std::max(ctx.f1.f64, 0.0), 5 * kStep);
    while (left >= kStep) {
      left -= kStep;
      ++steps;
    }
  }
  static std::atomic<bool> logged{false};
  LogOnce(logged, "water waves stepped at 60 Hz");
  for (int i = 0; i < steps; ++i) {
    ctx.r3.u64 = grid;
    ctx.f1.f64 = double(float(kStep));
    original(ctx, base);
  }
}

// CPolyGridFX::CollisionHandlerCB (#6): each pair of bodies splashes at most every 1/60 s.
void SplashContact(PPCContext& ctx, uint8_t* base, GuestFn original) {
  if (!Fixes()) {
    original(ctx, base);
    return;
  }
  const uint32_t a = ctx.r3.u32, b = ctx.r4.u32;
  const uint64_t key = a < b ? (uint64_t(b) << 32) | a : (uint64_t(a) << 32) | b;
  const auto now = Clock::now();
  bool skip = false;
  {
    struct Pair {
      uint64_t key = 0;
      Clock::time_point last{};
    };
    static std::mutex mutex;
    static std::array<Pair, 64> pairs;  // the most recent pairs, oldest overwritten
    static size_t next = 0;
    std::lock_guard<std::mutex> lock(mutex);
    Pair* found = nullptr;
    for (Pair& pair : pairs) {
      if (pair.key == key && pair.last != Clock::time_point{}) {
        found = &pair;
        break;
      }
    }
    if (found && now - found->last < std::chrono::duration<double>(kStep)) {
      skip = true;
    } else if (found) {
      found->last = now;
    } else {
      pairs[next] = {key, now};
      next = (next + 1) % pairs.size();
    }
  }
  if (skip) {
    static std::atomic<bool> logged{false};
    LogOnce(logged, "water splashes at most every 1/60 s per pair");
    base[ctx.r8.u32] = 1;  // bIgnoreCollision, as the callback always sets it
    return;
  }
  original(ctx, base);
}

// CPlayerMgr::HandleMsgSlowMo (#13): a server message can't lower a recharging meter (out of
// slow-mo before and after) or raise one in use.
void SlowMoMessage(PPCContext& ctx, uint8_t* base, GuestFn original) {
  if (!Fixes()) {
    original(ctx, base);
    return;
  }
  const uint32_t charge_at = ctx.r3.u32 + kSlowMoChargeOffset;
  const uint32_t state_at = ctx.r3.u32 + kSlowMoStateOffset;
  const double old_charge = LoadDouble(base, charge_at);
  const uint32_t old_state = LoadWord(base, state_at);
  original(ctx, base);
  double charge = LoadDouble(base, charge_at);
  const uint32_t state = LoadWord(base, state_at);
  const bool recharging = old_state == kSlowMoOut && state == kSlowMoOut && charge < old_charge;
  const bool raised = state != kSlowMoOut && old_charge > 0.0 && charge > old_charge;
  if (recharging || raised) {
    static std::atomic<bool> logged{false};
    LogOnce(logged, "slow-mo meter kept against the server's message");
    charge = old_charge;
    StoreDouble(base, charge_at, charge);
  }
  g_client_charge = charge;
}

// CGameServerShell::UpdateSlowMo (#13): slow-mo ends once the client's charge, seen above 0 since
// this slow-mo began, is spent.
void ServerSlowMoUpdate(PPCContext& ctx, uint8_t* base, GuestFn original, GuestFn exit_slowmo) {
  if (!Fixes()) {
    original(ctx, base);
    return;
  }
  static uint32_t last_record = 0;
  static bool charge_seen = false;
  const uint32_t shell = ctx.r3.u32;
  const uint32_t record = LoadWord(base, shell + kServerSlowMoRecordOffset);
  if (record != last_record) {
    last_record = record;
    charge_seen = false;
  }
  const double charge = g_client_charge;
  if (charge > 0.01) charge_seen = true;
  if (record && charge_seen && charge <= 0.01) {
    static std::atomic<bool> logged{false};
    LogOnce(logged, "slow-mo ends with the meter");
    ctx.r3.u64 = shell;
    ctx.r4.u64 = 1;  // bDoTransition
    ctx.f1.f64 = 0.0;  // fCharge
    exit_slowmo(ctx, base);
    return;
  }
  original(ctx, base);
}

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

void FearFrictionFrameTime(PPCRegister& f1) {
  // A frame of 0 (paused) stays 0.
  if (!Fixes() || !(f1.f64 > 0.0 && f1.f64 < kStep)) return;
  static std::atomic<bool> logged{false};
  LogOnce(logged, "slope friction by at least 1/60 s a frame");
  f1.f64 = kStep;
}

bool FearSkipLandingDamping() { return Fixes(); }

bool FearSkipSurfaceStop() { return Fixes(); }

void FearWaveFrameCall() { t_wave_frame_call = true; }

void FearSlowMoEnter(PPCRegister& r3, PPCRegister& r7, PPCRegister& f1) {
  if (!Fixes() || !r3.u32) return;
  const double charge = LoadDouble(Membase(), r3.u32 + kSlowMoChargeOffset);
  if ((r7.u32 & 0xFF) && charge > 0.0 && f1.f64 > charge) {
    static std::atomic<bool> logged{false};
    LogOnce(logged, "slow-mo period clamped to the meter");
    f1.f64 = charge;
  }
  g_client_charge = charge;
}

void FearSlowMoUpdate(PPCRegister& r3) {
  if (!Fixes() || !r3.u32) return;
  g_client_charge = LoadDouble(Membase(), r3.u32 + kSlowMoChargeOffset);
}

// The functions these replace, by game (high_fps.h): each hook runs the original as above.
#define FEAR_HIGH_FPS_HOOK(sub, fix)  \
  REX_EXTERN(__imp__##sub);           \
  REX_HOOK_RAW(sub) { fix(ctx, base, __imp__##sub); }

#if defined(FEAR_TITLE_FILES)
FEAR_HIGH_FPS_HOOK(sub_823690D8, OnGroundUpdate)
FEAR_HIGH_FPS_HOOK(sub_823F8268, WaveUpdate)
FEAR_HIGH_FPS_HOOK(sub_823F9ED8, SplashContact)
FEAR_HIGH_FPS_HOOK(sub_82358078, SlowMoMessage)
REX_EXTERN(sub_82564718);  // CGameServerShell::ExitSlowMo
REX_EXTERN(__imp__sub_82564978);
REX_HOOK_RAW(sub_82564978) { ServerSlowMoUpdate(ctx, base, __imp__sub_82564978, sub_82564718); }
#else
FEAR_HIGH_FPS_HOOK(sub_823924A8, OnGroundUpdate)
FEAR_HIGH_FPS_HOOK(sub_82407820, WaveUpdate)
FEAR_HIGH_FPS_HOOK(sub_824090D8, SplashContact)
FEAR_HIGH_FPS_HOOK(sub_82381BF0, SlowMoMessage)
REX_EXTERN(sub_8251E2E8);  // CGameServerShell::ExitSlowMo
REX_EXTERN(__imp__sub_8251E578);
REX_HOOK_RAW(sub_8251E578) { ServerSlowMoUpdate(ctx, base, __imp__sub_8251E578, sub_8251E2E8); }
#endif
