// F.E.A.R. 360 - a log of ClientFX effects, for finding why one looks wrong: every effect created
// (by type), model effects (CLTBModelFX) over their life, and decal sizes (CDecalFX). Called through [[entrypoint.midasm_hook]]s in
// fear_manifest.toml just before the effect sets its object's color and scale
// (sub_82BA9098, the 360's CLTBModelFX::Update), with the effect (r31), its object (r4), its
// unit lifetime (f31: 0 at birth, 1 at the end) and the color (f1-f4) or scale (f1).
// fear_debug_model_fx (off by default) turns the log on: one line per effect each time its
// unit lifetime crosses a tenth, at most 3000 lines.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

REXCVAR_DEFINE_BOOL(fear_debug_model_fx, false, "F.E.A.R.",
                    "Log model effects' lifetime, color and scale (debugging)");
// See FearDebugFireKey below.
REXCVAR_DEFINE_BOOL(fear_debug_weapon_fire, false, "F.E.A.R.",
                    "Log weapon fire keys (shot or dropped) and impact effects; with "
                    "fear_debug_model_fx, only the effects created around a shot (debugging)");

namespace {

struct Effect {
  int tenth = -1;
  float scale = 0;
};

std::mutex g_mutex;
std::unordered_map<uint32_t, Effect> g_effects;
uint32_t g_lines = 0;
uint32_t g_fire_lines = 0;  // fear_debug_weapon_fire's own budget
std::atomic<int64_t> g_last_fire{0};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// With fear_debug_weapon_fire, effects are logged only within 300 ms of a fire key, so a level's
// own effects don't use up the budget before the test.
bool AroundShot() {
  return !REXCVAR_GET(fear_debug_weapon_fire) || NowMs() - g_last_fire.load() < 300;
}

}  // namespace

void FearDebugModelFxScale(PPCRegister& r31, PPCRegister& f1, PPCRegister& f31) {
  if (!REXCVAR_GET(fear_debug_model_fx)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_effects[r31.u32].scale = float(f1.f64);
  (void)f31;
}

void FearDebugModelFxColor(PPCRegister& r31, PPCRegister& r4, PPCRegister& f31, PPCRegister& f1,
                           PPCRegister& f2, PPCRegister& f3, PPCRegister& f4) {
  if (!REXCVAR_GET(fear_debug_model_fx) || !AroundShot()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_lines >= 3000) return;
  if (g_effects.size() > 4096) g_effects.clear();
  Effect& effect = g_effects[r31.u32];
  const int tenth = int(std::floor(f31.f64 * 10.0));
  if (tenth == effect.tenth) return;
  effect.tenth = tenth;
  ++g_lines;
  REXLOG_INFO("[fx] model {:08X} object {:08X} life {:.3f} color {:.2f} {:.2f} {:.2f} alpha {:.3f} "
              "scale {:.2f}",
              r31.u32, r4.u32, f31.f64, f1.f64, f2.f64, f3.f64, f4.f64, effect.scale);
}

// A ClientFX effect created, by type (the hooks at each type's create function).
namespace {
void Created(const char* type) {
  if (!REXCVAR_GET(fear_debug_model_fx) || !AroundShot()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_lines >= 3000) return;
  ++g_lines;
  REXLOG_INFO("[fx] created {}", type);
}
}  // namespace

#define FEAR_FX_CREATED(type)   void FearDebugFxCreated##type() { Created(#type); }
FEAR_FX_CREATED(ParticleSystem)
FEAR_FX_CREATED(Sprite)
FEAR_FX_CREATED(LTBModel)
FEAR_FX_CREATED(DynaLight)
FEAR_FX_CREATED(Sound)
FEAR_FX_CREATED(CameraShake)
FEAR_FX_CREATED(Create)
FEAR_FX_CREATED(CreateRay)
FEAR_FX_CREATED(FlareSprite)
FEAR_FX_CREATED(Lightning)
FEAR_FX_CREATED(Tracer)
FEAR_FX_CREATED(LensFlare)
FEAR_FX_CREATED(DebrisSystem)
FEAR_FX_CREATED(VideoController)
FEAR_FX_CREATED(Decal)
FEAR_FX_CREATED(Overlay)
FEAR_FX_CREATED(PolyTrail)
FEAR_FX_CREATED(Rumble)

// A decal's size as CDecalFX::ProjectDecal computes it, just after width and height are scaled.
void FearDebugDecalSize(PPCRegister& r31, PPCRegister& f0, PPCRegister& f13, PPCRegister& f1,
                        PPCRegister& f12) {
  if (!REXCVAR_GET(fear_debug_model_fx) || !AroundShot()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_lines >= 3000) return;
  ++g_lines;
  REXLOG_INFO("[fx] decal {:08X} width {:.2f} height {:.2f} (scale {:.3f}) depth {:.2f}", r31.u32,
              f0.f64, f13.f64, f1.f64, f12.f64);
}

// Weapon fire, for the pistol's missing impacts (docs/PROGRESS.md, open item 12): a fire key from
// the player's animation becomes a shot only if the weapon is still firing (state 2) when the key
// arrives (CClientWeapon::HandleFireKey, sub_82429E68: the check at 0x8242A034), and every impact
// a shot makes goes through CWeaponFX::CreateObject (sub_82428A68). fear_debug_weapon_fire logs
// both, so a session shows whether a pistol shot was dropped, hit nothing, or made an impact whose
// effects didn't show (fear_debug_model_fx logs those).
namespace {
void NewShot(int64_t now);
void Impact(int64_t now);
}  // namespace

void FearDebugFireKey(PPCRegister& r29, PPCRegister& r3) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  g_last_fire = NowMs();
  std::lock_guard<std::mutex> lock(g_mutex);
  NewShot(g_last_fire);
  if (g_fire_lines >= 20000) return;
  ++g_fire_lines;
  REXLOG_INFO("[fire] key: weapon {:08X} state {} ({})", r29.u32, r3.s32,
              r3.s32 == 2 ? "shot" : "dropped");
}

void FearDebugWeaponImpact(PPCRegister& r3) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Impact(NowMs());
  if (g_fire_lines >= 20000) return;
  ++g_fire_lines;
  REXLOG_INFO("[fire] impact effects {:08X}", r3.u32);
}

// The particles of each shot's impact, for the dust that looks thinner at 80 fps than at 30 (open
// item 12). A particle group that first emits within 100 ms of an impact belongs to that shot; its
// particle system is the effect whose "was drawn" flag the group points at (group + 8 holds the
// address of system + 180). Followed through the game's own particle code:
//   CParticleSystemGroup::EmitParticleBatch (sub_82BB6E40): each batch, once its size is known
//   CParticleSystemGroup::RenderParticleSystem (sub_82BB7558): each render; the dynamic vertex
//     buffer lock, whose failure drops the rest of the group for that frame; the vertices granted
//   CParticleSystemFX::HandleUpdateTime (sub_82BAE3B8): updates made, and skipped (under 5 ms
//     since the last, or under 0.3 s for a system that wasn't drawn since)
// One line per shot, 3 s after it, with fear_debug_weapon_fire.
namespace {

struct ShotParticles {
  int64_t time = 0;
  uint32_t systems = 0, batches = 0, emitted = 0;
  uint32_t renders = 0, drawn = 0, lock_failures = 0, partial = 0;
  uint32_t updates = 0, skipped = 0;
};

std::map<uint32_t, ShotParticles> g_shots;  // by shot number
std::unordered_map<uint32_t, uint32_t> g_group_shot, g_system_shot;
uint32_t g_shot_number = 0;
int64_t g_last_impact = 0;
uint32_t g_lock_failures = 0;  // any group

ShotParticles* ShotOf(const std::unordered_map<uint32_t, uint32_t>& owners, uint32_t key) {
  const auto owner = owners.find(key);
  if (owner == owners.end()) return nullptr;
  const auto shot = g_shots.find(owner->second);
  return shot == g_shots.end() ? nullptr : &shot->second;
}

void Forget(std::unordered_map<uint32_t, uint32_t>& owners, uint32_t shot) {
  for (auto it = owners.begin(); it != owners.end();) {
    it = it->second == shot ? owners.erase(it) : std::next(it);
  }
}

void FlushShots(int64_t now) {
  for (auto it = g_shots.begin(); it != g_shots.end();) {
    if (now - it->second.time < 3000) break;  // in order of time
    const ShotParticles& s = it->second;
    if (g_fire_lines < 20000) {
      ++g_fire_lines;
      REXLOG_INFO("[particles] shot {}: {} systems, {} batches, {} particles emitted; {} renders "
                  "drew {} (lock failures {}, partial locks {}); updates {}, skipped {}",
                  it->first, s.systems, s.batches, s.emitted, s.renders, s.drawn,
                  s.lock_failures, s.partial, s.updates, s.skipped);
    }
    Forget(g_group_shot, it->first);
    Forget(g_system_shot, it->first);
    it = g_shots.erase(it);
  }
}

void NewShot(int64_t now) {
  FlushShots(now);
  g_shots[++g_shot_number].time = now;
}

void Impact(int64_t now) { g_last_impact = now; }

uint32_t GuestWord(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, rex::system::kernel_memory()->virtual_membase() + address, 4);
  return rex::byte_swap(value);
}

}  // namespace

void FearDebugParticleEmit(PPCRegister& r29, PPCRegister& r25) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  ShotParticles* shot = ShotOf(g_group_shot, r29.u32);
  if (!shot) {
    if (NowMs() - g_last_impact > 100 || g_shots.empty()) return;
    const uint32_t number = g_shots.rbegin()->first;
    g_group_shot[r29.u32] = number;
    g_system_shot[GuestWord(r29.u32 + 8) - 180] = number;
    shot = &g_shots.rbegin()->second;
    ++shot->systems;
  }
  ++shot->batches;
  shot->emitted += r25.u32;
}

void FearDebugParticleRender(PPCRegister& r27) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (ShotParticles* shot = ShotOf(g_group_shot, r27.u32)) ++shot->renders;
}

void FearDebugParticleLock(PPCRegister& r27, PPCRegister& r3) {
  if (!REXCVAR_GET(fear_debug_weapon_fire) || r3.u32 == 0) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (ShotParticles* shot = ShotOf(g_group_shot, r27.u32)) ++shot->lock_failures;
  if (++g_lock_failures <= 50 && g_fire_lines < 20000) {
    ++g_fire_lines;
    REXLOG_INFO("[particles] vertex buffer lock failed ({:08X}) for group {:08X}", r3.u32,
                r27.u32);
  }
}

void FearDebugParticleGranted(PPCRegister& r27, PPCRegister& r10, PPCRegister& r22) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  ShotParticles* shot = ShotOf(g_group_shot, r27.u32);
  if (!shot) return;
  const uint32_t granted = r10.u32 / 4;
  shot->drawn += granted;
  if (granted < std::min<uint32_t>(r22.u32, 4096)) ++shot->partial;
}

void FearDebugParticleUpdate(PPCRegister& r11) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (ShotParticles* shot = ShotOf(g_system_shot, r11.u32)) ++shot->updates;
}

void FearDebugParticleSkip(PPCRegister& r11) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (ShotParticles* shot = ShotOf(g_system_shot, r11.u32)) ++shot->skipped;
}

// Bullet tracers above ~90 fps (open item: they "twitch in midair and are stuck there for a
// second" in heavy firefights at 120). CTracerFX::Update (sub_82B9EA80) moves a tracer by
// velocity x frame time, as on PC, so this checks what it is given: per 10 s, the frame times
// tracers were updated with, how far they advanced, gaps between one tracer's updates, and
// tracers alive for over a second. The tracer's fields (the PC layout, the same on 360): +0x14
// its properties (+0x38 length, +0x44 velocity, +0x4F start emitted), +0x80 the distance
// travelled, +0x84 the length of its ray.
REXCVAR_DEFINE_BOOL(fear_debug_tracers, false, "F.E.A.R.",
                    "Log bullet tracers' updates every 10 s: frame times, progress, stalls "
                    "(debugging)");

namespace {

struct Tracer {
  int64_t first_ns = 0, last_ns = 0;
  float distance = 0;
  uint32_t updates = 0;
};

struct TracerWindow {
  int64_t start_ns = 0;
  uint32_t updates = 0, tracers = 0, gaps = 0, past_end = 0, long_lived = 0;
  uint32_t buckets[6] = {};  // frame time: 0, <2, <6, <10, <20, >=20 ms
  double dt_sum = 0, dt_max = 0, advance_sum = 0, gap_max_ms = 0, life_max_ms = 0;
};

std::mutex g_tracer_mutex;
std::unordered_map<uint32_t, Tracer> g_tracers;
TracerWindow g_tracer_window;
uint32_t g_tracer_lines = 0;

float GuestFloat(uint32_t address) {
  const uint32_t word = GuestWord(address);
  float value;
  std::memcpy(&value, &word, 4);
  return value;
}

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void EndTracer(TracerWindow& w, const Tracer& t) {
  const double life_ms = double(t.last_ns - t.first_ns) / 1e6;
  w.life_max_ms = std::max(w.life_max_ms, life_ms);
  if (life_ms > 1000.0) ++w.long_lived;
}

void ReportTracers(int64_t now) {
  TracerWindow& w = g_tracer_window;
  // Tracers not updated for 0.5 s are gone (their pool slots are reused).
  for (auto it = g_tracers.begin(); it != g_tracers.end();) {
    if (now - it->second.last_ns > 500000000) {
      EndTracer(w, it->second);
      it = g_tracers.erase(it);
    } else {
      ++it;
    }
  }
  if (w.updates && g_tracer_lines < 2000) {
    ++g_tracer_lines;
    REXLOG_INFO("[tracers] {} updates of {} tracers; frame time avg {:.2f} ms, max {:.2f}; "
                "0/<2/<6/<10/<20/20+ ms: {}/{}/{}/{}/{}/{}; advance avg {:.1f} units; {} gaps "
                "over 50 ms between one tracer's updates (longest {:.0f} ms); {} updates past "
                "the end of the ray; {} tracers alive over 1 s (longest {:.0f} ms)",
                w.updates, w.tracers, w.dt_sum / w.updates * 1000.0, w.dt_max * 1000.0,
                w.buckets[0], w.buckets[1], w.buckets[2], w.buckets[3], w.buckets[4],
                w.buckets[5], w.advance_sum / w.updates, w.gaps, w.gap_max_ms, w.past_end,
                w.long_lived, w.life_max_ms);
  }
  w = TracerWindow();
  w.start_ns = now;
}

}  // namespace

void FearDebugTracerUpdate(PPCRegister& r31, PPCRegister& f1) {
  if (!REXCVAR_GET(fear_debug_tracers)) return;
  const int64_t now = NowNs();
  const double dt = f1.f64;
  const uint32_t props = GuestWord(r31.u32 + 0x14);
  const float distance = GuestFloat(r31.u32 + 0x80);
  const float ray = GuestFloat(r31.u32 + 0x84);
  const float velocity = GuestFloat(props + 0x44);
  const bool start_emitted = (GuestWord(props + 0x4C) & 0xFF) != 0;  // the byte at +0x4F
  const float length = start_emitted ? ray : GuestFloat(props + 0x38);
  std::lock_guard<std::mutex> lock(g_tracer_mutex);
  TracerWindow& w = g_tracer_window;
  if (!w.start_ns) w.start_ns = now;
  auto [it, added] = g_tracers.try_emplace(r31.u32);
  Tracer& t = it->second;
  // A slot reused by a new tracer: its distance starts over.
  if (!added && (distance < t.distance || now - t.last_ns > 500000000)) {
    EndTracer(w, t);
    t = Tracer();
    added = true;
  }
  if (added) {
    t.first_ns = now;
    ++w.tracers;
  } else {
    const double gap_ms = double(now - t.last_ns) / 1e6;
    if (gap_ms > 50.0) ++w.gaps;
    w.gap_max_ms = std::max(w.gap_max_ms, gap_ms);
  }
  t.last_ns = now;
  t.distance = distance;
  ++t.updates;
  ++w.updates;
  const double ms = dt * 1000.0;
  ++w.buckets[ms <= 0.0 ? 0 : ms < 2 ? 1 : ms < 6 ? 2 : ms < 10 ? 3 : ms < 20 ? 4 : 5];
  w.dt_sum += dt;
  w.dt_max = std::max(w.dt_max, dt);
  w.advance_sum += dt * velocity;
  if (distance - length >= ray) ++w.past_end;
  if (now - w.start_ns >= 10000000000ll) ReportTracers(now);
}
