// D3D12 renderer (ported from pgr4-native 8ea20d5, MIT; see README.md): F.E.A.R.'s only renderer
// (docs/NATIVE_RENDERER.md).
//
// The guest D3D hooks in fear_d3d_hooks.cpp forward every call to the guest D3D, then call the
// NativeRenderer entry points below with the guest device and the call's arguments. Only the
// outermost draw/clear/resolve/Swap call is passed on (the D3D's internal clear, copy and Swap
// quads are not game draws); a top-level BeginVertices reaches OnDraw at the entry of the next
// hooked call, once the game has written its vertices. The renderer records on ReXGlue's D3D12
// presenter's device and queue and hands each frame to that presenter, which keeps the window
// (d3d12_device.h, present.h).
//
// Threading: the guest D3D device is used by one guest thread at a time; all On* entry points
// are called on that thread, after the original guest function returned, and are serialized
// by the renderer. Modules may assume single-threaded access from the On* calls.
//
// With --fear_d3d12_render_thread (default on; false keeps everything on the guest thread as
// above) the On* calls only capture, on the guest thread, what each call reads from guest
// memory: the register changes since the last draw (which the tracker records straight into the
// batch, GuestStateTracker::RecordInto), the DrawState, DrawRenderer::Prepare's shader lookup and
// vertex/index copies, and the decoded Clear/Resolve/Swap arguments. They queue it in batches to
// the render thread
// ("D3D12 render"), which replays it into the modules with its own copy of the register
// file. From then on the command list, the D3D12 objects and every module's state belong to
// that thread (DrawRenderer's vertex upload pool and shader lookup stay on the guest thread).
// The guest thread blocks at Swap while the render thread is more than one frame behind, and
// waits for the render thread to finish the Swaps around a capture (present.h).
#pragma once

#include "hook_args.h"

#include <rex/cvar.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12CommandList;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;

REXCVAR_DECLARE(bool, fear_d3d12_render_thread);  // record on a render thread (see above)

namespace fear::d3d12 {

class D3D12Device;
class DrawRenderer;
class FramePacer;
class GpuTimer;
class Presenter;
class RenderTargets;
class Resolver;
class TextureCache;
struct ClearCall;
struct DrawPrep;
struct DrawState;
struct ResolveParams;
struct SwapCall;

// Per-frame counters of the native path (also the frame CSV's columns).
struct FrameStats {
  uint64_t draws = 0, clears = 0, resolves = 0;
  uint64_t state_fail = 0;  // draws ReadDrawState rejected
  // CPU time inside ExecuteDraw, ExecuteClear and ExecuteResolve, summed in ticks and converted
  // at the Swap: the render thread's time (the guest thread's without it).
  uint64_t draw_ns = 0, clear_ns = 0, resolve_ns = 0;
  uint64_t swap_ns = 0;  // ExecuteSwap: record, submit, present
  // Guest thread: time in the On* calls (render thread mode: capturing and queueing) and the
  // part of it spent waiting for the render thread.
  uint64_t hook_ns = 0, hook_wait_ns = 0;
  uint64_t hook_max_ns = 0;  // render thread mode: the longest single On* call, without waits
  uint64_t reg_writes = 0;   // render thread mode: register values replayed
  // Parts of swap_ns: waiting for the texture untile workers, then D3D12Device::EndFrameTimes.
  uint64_t swap_untile_ns = 0, swap_submit_ns = 0, swap_present_ns = 0, swap_fence_ns = 0;
  uint64_t frames_held = 0;  // Swaps not presented (DrawRenderer::FrameIncomplete)
  uint64_t submits = 0;      // lists submitted before the Swap's (--fear_d3d12_submit_draws)
  // The guest's pad reads since the Swap before (rex_xam_take_frame_pad, taken at the Swap).
  uint64_t pad = 0;
  // Pacing (NowNs clock): when the guest thread began the frame (its previous Swap returned) and
  // reached its Swap, how long FramePacer::WaitToBegin held it before it began, and the frame's
  // present number (D3D12Device::EndFrameTimes::presents; 0: not presented).
  uint64_t guest_begin_ns = 0, guest_swap_ns = 0, pace_wait_ns = 0, present = 0;
};

class NativeRenderer {
 public:
  // The renderer, created on first use; nullptr when D3D12 initialization failed (logged once)
  // and from a failed frame on (Fail).
  static NativeRenderer* Get();
  // The renderer while it draws the game: Get(), until a frame failed. The hooks use this one, a
  // single load while the renderer draws; while it is null the guest D3D runs unchanged and
  // nothing is tracked.
  static NativeRenderer* Live() {
    if (NativeRenderer* r = live_.load(std::memory_order_acquire)) [[likely]]
      return r;
    Get();  // creates the renderer on first use
    return live_.load(std::memory_order_acquire);
  }

  // True while the renderer presents the game's frames and paces them on the display
  // (frame_pacer.h): the guest D3D then presents without waiting for a vblank
  // (Pgr4PresentationInterval in fear_d3d_hooks.cpp). Any thread.
  static bool PacesFrames() { return paces_frames_.load(std::memory_order_relaxed); }

  // Hook entry points. `base` is the guest memory base the hook received (guest virtual
  // address a is at base + a, plus 0x1000 for a >= 0xE0000000 like the generated code).
  void OnDraw(const uint8_t* base, const HookArgs& args);
  void OnClear(const uint8_t* base, const HookArgs& args);
  void OnResolve(const uint8_t* base, const HookArgs& args);
  void OnSwap(uint8_t* base, const HookArgs& args);

  // For modules.
  D3D12Device& device() { return *device_; }
  ID3D12Device* d3d12() const;
  ID3D12CommandQueue* command_queue() const;
  // The frame's direct command list: open from the previous Swap until OnSwap closes and
  // submits it. Record into it from the On* calls only (the render thread, in that mode).
  ID3D12GraphicsCommandList* command_list() const;
  // Swaps recorded so far (= index of the frame being recorded).
  uint64_t frame_index() const { return frame_index_; }
  // Fence value OnSwap will signal for the frame being recorded; a resource last used in this
  // frame may be freed once device().completed_fence() >= this.
  uint64_t frame_fence() const;
  // Modules owned by the renderer.
  TextureCache& textures() { return *textures_; }
  RenderTargets& render_targets() { return *rt_; }
  Resolver& resolver() { return *resolver_; }

  ~NativeRenderer();

 private:
  NativeRenderer() = default;
  bool Initialize();
  // At the end of each frame's ExecuteSwap: its row of the frame CSV, its share of the stats
  // log (every --fear_d3d12_stats_frames frames), then the per-frame counters start over.
  void WriteCsvRow(const FrameStats& s, uint64_t frame_ns);
  // The frame CSV's columns, their names (header) or this frame's values: those of every frame,
  // with --fear_d3d12_profile the stage times and texture counters, with
  // --fear_d3d12_luma_probe the probe's.
  void AppendCsvColumns(const FrameStats& s, uint64_t frame_ns, bool header, std::string* line);
  void AppendCsvProfileColumns(bool header, std::string* line);
  void AppendCsvLumaColumns(bool header, std::string* line);
  void AddToStatsLog(const FrameStats& s, uint64_t frame_ns);
  void ResetFrameCounters();
  // Starts a frame's module state (RenderTargets/Resolver BeginFrame) at its first On* call.
  void BeginFrame() {
    if (!frame_begun_) BeginFrameModules();
  }
  void BeginFrameModules();
  // The recording half of the On* calls (on the guest thread, or the render thread).
  void ExecuteDraw(const DrawState& state, const DrawPrep* prep);
  void ExecuteClear(const ClearCall& clear);
  void ExecuteResolve(const ResolveParams& p);
  void ExecuteSwap(const SwapCall& swap);
  // After a draw: submits the frame so far once --fear_d3d12_submit_draws draws were recorded
  // since the last submission.
  void SubmitEarly();
  // Before each submission, once the untiles finished: the texture upload lists to submit first
  // (TextureCache::CloseUploadLists), into upload_lists_. False on failure (logged).
  bool CloseUploadLists();
  // A failed submission: logs and stops the renderer for the rest of the run (nothing takes over;
  // the window keeps the last frame).
  void Fail();
  // Render thread mode.
  struct Queue;
  void QueueDraw(const uint8_t* base, const HookArgs& args);
  void QueueClear(const uint8_t* base, const HookArgs& args);
  void QueueResolve(const uint8_t* base, const HookArgs& args);
  void QueueSwap(uint8_t* base, const HookArgs& args);
  void RenderThreadMain();

  static inline std::atomic<bool> paces_frames_{false};       // pacer_->pacing(), for any thread
  static inline std::atomic<NativeRenderer*> live_{nullptr};  // Live()

  std::unique_ptr<Queue> queue_;  // render thread mode only

  std::unique_ptr<D3D12Device> device_;
  std::unique_ptr<RenderTargets> rt_;
  std::unique_ptr<Resolver> resolver_;
  std::unique_ptr<TextureCache> textures_;
  std::unique_ptr<DrawRenderer> draw_;
  std::unique_ptr<Presenter> present_;
  // Never created in F.E.A.R. (ReXGlue's presenter paces the window; frame_pacer.h is pgr4-native's
  // pacer for a swapchain of its own).
  std::unique_ptr<FramePacer> pacer_;
  GpuTimer* gpu_timer_ = nullptr;  // device_'s, with --fear_d3d12_gpu_timing
  bool frame_begun_ = false;
  uint64_t frame_index_ = 0;
  uint64_t frame_start_ns_ = 0;
  FrameStats stats_;
  // Ticks inside ExecuteDraw, ExecuteClear and ExecuteResolve, into stats_ at the Swap.
  uint64_t draw_ticks_ = 0, clear_ticks_ = 0, resolve_ticks_ = 0;
  uint64_t draws_submitted_ = 0;                  // stats_.draws at the frame's last SubmitEarly
  std::vector<ID3D12CommandList*> upload_lists_;  // CloseUploadLists'
  struct Csv;
  std::unique_ptr<Csv> csv_;
};

}  // namespace fear::d3d12
