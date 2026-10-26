// D3D12 renderer: the guest D3D hooks (fear_d3d.h fn::). The dispatch follows pgr4-native
// 8ea20d5's (pgr4_d3d_trace.cpp OnHook, MIT; see native/README.md).
//
// Each hook overrides the generated function (REX_HOOK_RAW) and always runs the original first,
// so the guest device stays as the game expects and fear-nx's ring gets the whole stream.
// While the renderer is live (always, unless it failed):
//   - every hooked call reports its entry and exit to the push-buffer tracker (d3d_state.h),
//     which decodes the packets the call wrote; the push-buffer functions (MakeSpace, the
//     segment wrap and restore) report the cursor's jump instead;
//   - the outermost draw, clear, resolve or Swap reaches the renderer after the original
//     returned, with its arguments at entry; the clears and resolves inside BeginTiling and
//     EndTiling carry that call (TilingCall);
//   - BeginVertices (and BeginIndexedVertices) only reserve: the D3D publishes their packets and
//     the game its vertices at EndVertices, where their draw is queued.
// After a failure, the hooks only call the originals.
#include "d3d_state.h"
#include "hook_args.h"
#include "renderer.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <array>
#include <cstdint>

REXCVAR_DEFINE_INT32(fear_d3d12_stats_frames, 150, "F.E.A.R. D3D12",
                     "Frames per summary line of the native renderer's statistics (0 = off)");

namespace {

namespace nat = fear::d3d12;
using nat::HookFn;
using nat::TilingCall;
using GuestFn = void (*)(PPCContext&, uint8_t*);

enum NativeClass : uint8_t { kNatDraw, kNatPendingDraw, kNatClear, kNatResolve, kNatSwap };

thread_local uint32_t t_native_depth = 0;  // active draw/clear/resolve/Swap calls
thread_local bool t_pending = false;       // a BeginVertices draw waits for EndVertices
thread_local nat::HookArgs t_pending_args;
thread_local TilingCall t_tiling = TilingCall::kNone;

nat::HookArgs EntryArgs(HookFn fn, const PPCContext& ctx) {
  nat::HookArgs a;
  a.fn = fn;
  a.device = ctx.r3.u32;
  const std::array<uint32_t, 8> r = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                                     ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  for (size_t i = 0; i < r.size(); ++i) a.r[i] = r[i];
  a.sp = ctx.r1.u32;
  a.f[0] = ctx.f1.f64;
  a.f[1] = ctx.f2.f64;
  a.f[2] = ctx.f3.f64;
  a.f[3] = ctx.f4.f64;
  a.ret = 0;
  a.tiling = t_tiling;
  return a;
}

// A draw BeginVertices reserved that never reached EndVertices (the game took another path):
// its packets were not published, so it is dropped.
void DropPending() {
  if (!t_pending) return;
  t_pending = false;
  static uint32_t dropped = 0;
  if (++dropped <= 8)
    REXLOG_WARN("D3D12: BeginVertices draw at {:08X} without EndVertices; dropped",
                t_pending_args.device);
}

// The original, with the tracker told about its entry and exit.
template <GuestFn kOriginal>
inline void RunTracked(PPCContext& ctx, uint8_t* base, uint32_t fn) {
  const uint32_t device = ctx.r3.u32;
  nat::GuestStateTracker& gs = nat::GuestState();
  gs.Sync(base, device, fn, t_native_depth == 0);
  kOriginal(ctx, base);
  gs.Sync(base, device, fn);
  gs.AfterCall(base, device);
}

template <HookFn kFn, NativeClass kClass, GuestFn kOriginal>
inline void NativeHook(PPCContext& ctx, uint8_t* base) {
  nat::NativeRenderer* const r = nat::NativeRenderer::Live();
  if (!r) {
    kOriginal(ctx, base);
    return;
  }
  constexpr uint32_t kAddr = uint32_t(kFn);
  const bool outer = t_native_depth++ == 0;
  nat::HookArgs a;
  if (outer) {
    DropPending();
    a = EntryArgs(kFn, ctx);
  }
  {
    const uint32_t device = ctx.r3.u32;
    nat::GuestStateTracker& gs = nat::GuestState();
    gs.Sync(base, device, kAddr, outer);
    if (outer && (kClass == kNatDraw || kClass == kNatPendingDraw)) gs.BeginDrawCall();
    kOriginal(ctx, base);
    gs.Sync(base, device, kAddr);
    gs.AfterCall(base, device);
  }
  --t_native_depth;
  if (!outer) return;
  a.ret = ctx.r3.u64;
  if constexpr (kClass == kNatDraw) {
    r->OnDraw(base, a);
  } else if constexpr (kClass == kNatPendingDraw) {
    // BeginVertices returns the vertex pointer (0: failed); BeginIndexedVertices an HRESULT
    // (0: S_OK), its pointers through out-parameters. Its draws are the particle batches
    // (dust, smoke, muzzle flashes).
    if (kFn == HookFn::kBeginIndexedVertices ? uint32_t(a.ret) == 0 : a.ret != 0) {
      t_pending_args = a;
      t_pending = true;
    }
  } else if constexpr (kClass == kNatClear) {
    r->OnClear(base, a);
  } else if constexpr (kClass == kNatResolve) {
    r->OnResolve(base, a);
  } else {
    static_assert(kClass == kNatSwap);
    r->OnSwap(base, a);
  }
}

// EndVertices: publishes BeginVertices' packets (device+40 = device+13076); the draw is queued
// once the tracker decoded them. F.E.A.R.'s D3D has one after each Begin (fear_d3d.h).
template <GuestFn kOriginal>
inline void EndVerticesHook(PPCContext& ctx, uint8_t* base, uint32_t fn) {
  nat::NativeRenderer* const r = nat::NativeRenderer::Live();
  if (!r) {
    t_pending = false;
    kOriginal(ctx, base);
    return;
  }
  nat::GuestStateTracker& gs = nat::GuestState();
  const uint32_t device = ctx.r3.u32;
  gs.Sync(base, device, fn, t_native_depth == 0);
  gs.BeginDrawCall();
  kOriginal(ctx, base);
  gs.Sync(base, device, fn);
  gs.AfterCall(base, device);
  if (t_pending && t_native_depth == 0) {
    t_pending = false;
    if (t_pending_args.device == device) r->OnDraw(base, t_pending_args);
  }
}

// BeginTiling / EndTiling: the clears and resolves they make carry the call.
template <TilingCall kCall, GuestFn kOriginal>
inline void TilingHook(PPCContext& ctx, uint8_t* base, uint32_t fn) {
  if (!nat::NativeRenderer::Live()) {
    kOriginal(ctx, base);
    return;
  }
  const TilingCall outer = t_tiling;
  t_tiling = kCall;
  RunTracked<kOriginal>(ctx, base, fn);
  t_tiling = outer;
}

// The functions that move the push cursor by assignment: decode up to the old place, then go on
// from the new one.
//
// Nested in another one (the wrap inside MakeSpace's submit, sub_8298A7E8 -> sub_8298A620), it
// does not decode at its entry: the outer call decoded everything the game wrote, and what lies
// between that and the cursor now is the submit's own bookkeeping (pointers, not packets), which
// decoded as packets with absurd lengths (torn at the jump, ~5,000 per 3 minutes).
thread_local uint32_t t_jump_depth = 0;

template <GuestFn kOriginal>
inline void JumpHook(PPCContext& ctx, uint8_t* base, uint32_t fn) {
  if (!nat::NativeRenderer::Live()) {
    kOriginal(ctx, base);
    return;
  }
  const uint32_t device = ctx.r3.u32;
  nat::GuestStateTracker& gs = nat::GuestState();
  if (t_jump_depth++ == 0) gs.Sync(base, device, fn, t_native_depth == 0);
  kOriginal(ctx, base);
  --t_jump_depth;
  gs.Jump(base, device, fn);
}

}  // namespace

#define FEAR_D3D12_HOOK(addr, kfn, cls)                          \
  REX_EXTERN(__imp__sub_##addr);                                 \
  REX_HOOK_RAW(sub_##addr) {                                     \
    NativeHook<HookFn::kfn, cls, __imp__sub_##addr>(ctx, base);  \
  }                                                              \
  static_assert(uint32_t(HookFn::kfn) == 0x##addr##u, "address")

#define FEAR_D3D12_JUMP_HOOK(addr, kfn)                                               \
  REX_EXTERN(__imp__sub_##addr);                                                      \
  REX_HOOK_RAW(sub_##addr) { JumpHook<__imp__sub_##addr>(ctx, base, nat::fn::kfn); }  \
  static_assert(nat::fn::kfn == 0x##addr##u, "address")

#define FEAR_D3D12_END_VERTICES_HOOK(addr, kfn)                   \
  REX_EXTERN(__imp__sub_##addr);                                  \
  REX_HOOK_RAW(sub_##addr) {                                      \
    EndVerticesHook<__imp__sub_##addr>(ctx, base, nat::fn::kfn);  \
  }                                                               \
  static_assert(nat::fn::kfn == 0x##addr##u, "address")

#define FEAR_D3D12_TILING_HOOK(addr, kfn, call)                                \
  REX_EXTERN(__imp__sub_##addr);                                               \
  REX_HOOK_RAW(sub_##addr) {                                                   \
    TilingHook<TilingCall::call, __imp__sub_##addr>(ctx, base, nat::fn::kfn);  \
  }                                                                            \
  static_assert(nat::fn::kfn == 0x##addr##u, "address")

// The hooks, by title (fear_d3d.h fn::): the symbols are the generated functions' names.
#if defined(FEAR_TITLE_FILES)
FEAR_D3D12_HOOK(8297ED48, kDrawIndexedVertices, kNatDraw);
FEAR_D3D12_HOOK(8297E960, kDrawVertices, kNatDraw);
FEAR_D3D12_HOOK(8297E338, kDrawVerticesUP, kNatDraw);
FEAR_D3D12_HOOK(8297DE88, kBeginVertices, kNatPendingDraw);
FEAR_D3D12_HOOK(8297E380, kBeginIndexedVertices, kNatPendingDraw);
FEAR_D3D12_HOOK(8297DB00, kClear, kNatClear);
FEAR_D3D12_HOOK(8297D9D8, kClearF, kNatClear);
FEAR_D3D12_HOOK(8297B5C8, kResolve, kNatResolve);
FEAR_D3D12_HOOK(829746C0, kSwap, kNatSwap);
FEAR_D3D12_JUMP_HOOK(82981538, kMakeSpace);
FEAR_D3D12_JUMP_HOOK(82981130, kWrapSegment);
FEAR_D3D12_JUMP_HOOK(8297FD00, kRestoreSegment);
FEAR_D3D12_END_VERTICES_HOOK(8297E328, kEndVertices);
FEAR_D3D12_END_VERTICES_HOOK(8297E8B0, kEndIndexedVertices);
FEAR_D3D12_TILING_HOOK(82979B10, kBeginTiling, kBegin);
FEAR_D3D12_TILING_HOOK(82979FA8, kEndTiling, kEnd);
#else  // F.E.A.R.
FEAR_D3D12_HOOK(82988340, kDrawIndexedVertices, kNatDraw);
FEAR_D3D12_HOOK(82987F38, kDrawVertices, kNatDraw);
FEAR_D3D12_HOOK(829878B0, kDrawVerticesUP, kNatDraw);
FEAR_D3D12_HOOK(829873D8, kBeginVertices, kNatPendingDraw);
FEAR_D3D12_HOOK(82987910, kBeginIndexedVertices, kNatPendingDraw);
FEAR_D3D12_HOOK(829872B0, kClear, kNatClear);
FEAR_D3D12_HOOK(82986F00, kClearF, kNatClear);
FEAR_D3D12_HOOK(82984718, kResolve, kNatResolve);
FEAR_D3D12_HOOK(82974598, kSwap, kNatSwap);
FEAR_D3D12_JUMP_HOOK(8298A988, kMakeSpace);
FEAR_D3D12_JUMP_HOOK(8298A620, kWrapSegment);
FEAR_D3D12_JUMP_HOOK(82989998, kRestoreSegment);
FEAR_D3D12_END_VERTICES_HOOK(82987890, kEndVertices);
FEAR_D3D12_END_VERTICES_HOOK(82987E60, kEndIndexedVertices);
FEAR_D3D12_TILING_HOOK(82979BD0, kBeginTiling, kBegin);
FEAR_D3D12_TILING_HOOK(8297A0C8, kEndTiling, kEnd);
#endif
