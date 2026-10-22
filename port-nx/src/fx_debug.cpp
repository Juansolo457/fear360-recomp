// F.E.A.R. 360 - a log of ClientFX effects, for finding why one looks wrong: every effect created
// (by type), model effects (CLTBModelFX) over their life, and decal sizes (CDecalFX). Called through [[entrypoint.midasm_hook]]s in
// fear_manifest.toml just before the effect sets its object's color and scale
// (sub_82BA9098, the 360's CLTBModelFX::Update), with the effect (r31), its object (r4), its
// unit lifetime (f31: 0 at birth, 1 at the end) and the color (f1-f4) or scale (f1).
// fear_debug_model_fx (off by default) turns the log on: one line per effect each time its
// unit lifetime crosses a tenth, at most 3000 lines.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

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
void FearDebugFireKey(PPCRegister& r29, PPCRegister& r3) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  g_last_fire = NowMs();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fire_lines >= 20000) return;
  ++g_fire_lines;
  REXLOG_INFO("[fire] key: weapon {:08X} state {} ({})", r29.u32, r3.s32,
              r3.s32 == 2 ? "shot" : "dropped");
}

void FearDebugWeaponImpact(PPCRegister& r3) {
  if (!REXCVAR_GET(fear_debug_weapon_fire)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fire_lines >= 20000) return;
  ++g_fire_lines;
  REXLOG_INFO("[fire] impact effects {:08X}", r3.u32);
}
