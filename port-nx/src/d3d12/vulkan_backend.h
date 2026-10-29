// D3D12 renderer: the Vulkan backend (backend.h; docs/VULKAN_RENDERER.md), GRAW's, on ReXGlue's
// Vulkan presenter's device. Its milestones, as GRAW's docs/VULKAN_RENDERER.md has them:
//
// M2: the device and the frame loop (F.E.A.R.: frames handed to ReXGlue's presenter, as the D3D12
// backend's, vulkan_device.h).
// M3a: each draw's shaders are translated to SPIR-V for the variant its registers select, and
// get shader modules (vulkan_shaders.h).
// M3c-1: guest memory mirrored in a shared-memory storage buffer through GuestMemoryMirror (this
// backend is its Sink): each draw's vertex and index ranges are copied there; the draws aren't
// recorded yet.
// M3c-2: each draw's descriptor sets (vulkan_bindings.h): shared memory, the five constant
// buffers with the system constants, placeholder textures, bound for its pipeline layout.
// M3c-3: the draws recorded: host render targets (vulkan_render_targets.h, dynamic rendering),
// pipelines from the render state (vulkan_pipelines.h), the guest's clears. Points, rectangles
// and quads (geometry shaders) not yet.
// M3d: the frame shown at Swap: each color Resolve copies its source rectangle (MSAA resolved to
// 1x) into an image kept per destination address, as the D3D12 Resolver does (the guest may
// clear the source right after); the Swap's front buffer address picks the image.
// M4: guest textures and samplers (vulkan_textures.h), resolve images bound as textures, the
// resolve shaders (vulkan_resolve.h: exponent bias, 7e3, red/blue swap), geometry shaders,
// background pipeline compiles and the pipeline cache.
// M5: the front buffer shown through the present pass (vulkan_present.h): its fetch
// constant's swizzle and the display controller's gamma ramp.
// F.E.A.R.: the only backend on Linux, and on Windows with --fear_d3d12_api=vulkan (renderer.h).
#pragma once

#include "vulkan_device.h"  // first: the Vulkan platform defines

#include "backend.h"
#include "guest_memory_mirror.h"
#include "surfaces.h"  // SurfaceRegs

#include <rex/graphics/xenos.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rex::graphics {
class SpirvShader;
}

namespace fear::d3d12 {

class GpuTimer;
class VulkanDevice;
class PipelineStore;
struct VulkanPipelineKey;
struct VulkanRenderTarget;
class VulkanShaderCache;
struct ShaderRef;

class VulkanBackend final : public Backend, public GuestMemoryMirror::Sink {
 public:
  VulkanBackend();
  ~VulkanBackend() override;

  bool Initialize(const Options& options) override;

  void BeginCopySegment() override {
    mirror_.NewSegment();
    segments_batch_.fetch_add(1, std::memory_order_relaxed);
  }
  void Prepare(const DrawState& state, uint64_t upload_tag, DrawPrep* prep,
               std::vector<VertexCopy>* copies, std::vector<AuditRange>* audit) override;
  void StartAudit(uint32_t frames) override {}
  PrepareCounters EndFramePrepare(uint64_t done) override;
  bool CaptureRequested(uint64_t frame) override { return false; }
  bool capture_holds_guest() const override { return false; }
  bool capture_holding_guest() const override { return false; }

  void AddPrepareCounters(const PrepareCounters& c) override;
  void LogFrames(uint32_t frames) override {}
  void BeginFrame(uint64_t frame_index) override;
  void Draw(const DrawState& state, const DrawPrep* prep) override;
  void Clear(const ClearCall& clear) override;
  void Resolve(const ResolveParams& params) override;
  SubmitResult SubmitEarly() override { return SubmitResult::kNotReady; }
  SwapResult Swap(const SwapCall& swap) override;
  void AfterSwap(uint64_t frame_index, uint64_t completed) override;

  const DrawStats& draw_stats() const override { return stats_; }
  void ResetDrawStats() override { stats_ = DrawStats{}; }
  const TextureStats& texture_stats() const override { return texture_stats_; }
  uint64_t texture_resident_bytes() const override { return 0; }
  uint64_t texture_count() const override { return 0; }
  bool front_buffer_found() const override { return front_buffer_found_; }
  const LumaProbe& luma_probe() const override { return luma_; }
  // The device's frame timestamps (#91): measured whether or not --fear_d3d12_gpu_timing is on
  // (two timestamps a frame); NaN, an empty CSV cell, where the queue has none.
  double gpu_last_frame_ms() const override { return device_->gpu_last_frame_ms(); }
  // --fear_d3d12_gpu_timing: GPU time per frame and idle time between frames, per window.
  void LogGpuTiming() override;

  // GuestMemoryMirror::Sink. The shared buffer is allocated whole (no sparse binding yet).
  bool MapShared(uint32_t start, uint32_t length) override { return true; }
  bool AllocateUpload(uint32_t size, uint64_t tag, GuestMemoryMirror::Upload* out) override;

 private:
  // Records count copies into shared memory, between the barriers that order them after the
  // draws before and ahead of the draws after.
  void RecordCopies(const VertexCopy* copies, size_t count);
  // The shader a draw binds, from guest memory (or the tracker's patched copy).
  rex::graphics::SpirvShader* LoadShader(const ShaderRef& ref,
                                         rex::graphics::xenos::ShaderType type);

  std::unique_ptr<VulkanDevice> device_;
  GpuTimer* gpu_timer_ = nullptr;  // device_'s, with --fear_d3d12_gpu_timing
  std::unique_ptr<VulkanShaderCache> shaders_;
  struct Gpu;  // memory, the shared buffer, upload pools (vulkan_backend.cpp)
  struct ResolvedImage;  // a resolve destination (vulkan_backend.cpp)
  std::unique_ptr<Gpu> gpu_;
  GuestMemoryMirror mirror_;
  bool threaded_ = false;  // --fear_d3d12_render_thread: Prepare runs on the guest thread
  // Prepare's counters on the guest thread, handed over at EndFramePrepare (with the render
  // thread); diag_ is the recording thread's without it.
  uint64_t prep_upload_bytes_ = 0, prep_uploads_ = 0;
  PrepareDiag prep_diag_, diag_;
  std::vector<VertexCopy> copies_;  // without the render thread: the draw's copies
  const uint8_t* membase_ = nullptr;  // guest physical memory
  uint64_t frames_logged_ = 0;        // Swap: the shader summary every 150 frames
  uint64_t frame_index_ = 0;
  // M4d: pipelines that survive runs (pipeline_store.h, in <cache_root>/fear-d3d12-vulkan):
  // recorded as first created, compiled at start-up in later runs (Prewarm, a slice per
  // frame). A frame whose draws waited for pipelines is held (pipeline_wait_).
  void RecordPipeline(const VulkanPipelineKey& key, const rex::graphics::SpirvShader& vs,
                      uint64_t vs_mod, const rex::graphics::SpirvShader* ps, uint64_t ps_mod,
                      uint32_t gs_key);
  // Translates and queues stored pipelines from earlier runs, for up to `budget` per call.
  void Prewarm(std::chrono::milliseconds budget = std::chrono::milliseconds(2));
  // --fear_d3d12_warmup, once before the first draw: every stored pipeline translated and built
  // on all the pipeline threads while src/pipeline_warmup.h's panel shows the progress, as the
  // D3D12 draws' WarmUp (draw.cpp).
  void WarmUp();
  std::unique_ptr<PipelineStore> store_;
  size_t prewarm_next_ = 0;
  uint32_t prewarm_failed_ = 0, prewarm_frames_ = 0;
  double prewarm_ms_ = 0;
  bool pipeline_wait_ = false;
  // --fear_d3d12_vulkan_skip_draws: this frame's draw count, and the range skipped.
  uint32_t frame_draw_ = 0;
  uint64_t skip_vs_ = 0;  // --fear_d3d12_vulkan_skip_vs
  uint64_t dump_after_vs_ = 0;  // --fear_d3d12_vulkan_dump_after_vs
  std::unordered_set<uint64_t> coverage_ps_;  // --fear_d3d12_vulkan_coverage_ps (empty: off)
  bool coverage_parsed_ = false;
  // Logs a counted draw's textures, samplers, blend and depth state the first time that
  // combination is seen (coverage_described_: the combinations logged, capped).
  void DescribeCoverageDraw(const rex::graphics::SpirvShader& ps, const DrawState& state,
                            const VulkanPipelineKey& key, const VulkanRenderTarget& rt);
  std::unordered_set<std::string> coverage_described_;
  // Why copy segments start (logged with the GPU timing): a new batch, a draw's copy
  // overwriting memory the segment reads, or kSegmentExactLimit exact intervals. Counted on
  // the guest thread (Prepare, BeginCopySegment), read on the render thread.
  std::atomic<uint64_t> segments_batch_{0}, segments_conflict_{0}, segments_limit_{0};
  std::atomic<uint64_t> segments_conflict_exact_{0};  // of the conflicts, on an exact range
  int64_t dump_at_ = -1;         // the frame it picked (-1: none yet)
  uint32_t dump_after_count_ = 0;
  uint32_t skip_first_ = UINT32_MAX, skip_last_ = 0;
  DrawStats stats_;
  bool front_buffer_found_ = false;
  bool front_buffer_logged_ = false;
  // --fear_d3d12_anisotropy: the least anisotropy of linearly filtered textures with mips.
  rex::graphics::xenos::AnisoFilter min_aniso_ = rex::graphics::xenos::AnisoFilter::kDisabled;
  uint64_t resolves_ = 0, resolves_skipped_ = 0, resolve_clears_ = 0, depth_resolves_ = 0;
  uint64_t shader_resolves_ = 0;  // color resolves through the shader (the rest: copies, blits)
  // Resolve bounds (#100): where the game resolves the same rectangle of a color target into
  // the same destination over and over (an effect drawn, the frame copied, the next effect
  // drawn from the copy...), each copy is cut to what the draws since the previous one reached.
  // A tracked target's draws run their vertex shaders with SpirvShaderTranslator's
  // kSysFlag_DrawBounds, which records each vertex's pixel position in the target's slot of
  // the draw bounds buffer (gpu_->draw_bounds, kDrawBoundsSlots x 16 bytes); the resolve
  // shader then copies only the tiles the slot's box (plus a margin) touches, the destination
  // keeping the rest from the previous copy, and the slot is reset. Tracking starts once a
  // resolve repeats the one before it and stops when the pattern breaks or a draw count is
  // passed. --no-fear_d3d12_vulkan_resolve_bounds turns it off.
  static constexpr uint32_t kDrawBoundsSlots = 16;
  static constexpr uint32_t kDrawBoundsSlotBytes = 16;
  // After the slots, each slot's indirect dispatch (bounds_dispatch.comp): 8 words.
  static constexpr uint32_t kDrawBoundsArgsBytes = 32;
  static constexpr uint64_t kDrawBoundsBufferBytes =
      uint64_t(kDrawBoundsSlots) * (kDrawBoundsSlotBytes + kDrawBoundsArgsBytes);
  // The shaders find the dispatch words at kArgsBase = 64 (bounds_dispatch.comp).
  static_assert(kDrawBoundsSlots * kDrawBoundsSlotBytes == 64 * 4);
  static constexpr uint32_t kDrawBoundsMaxDraws = 64;  // tracked draws between two resolves
  // A color resolve's copy, with what its source and destination held: the same again means
  // the destination holds the previous copy, except where the draws since changed the source.
  struct ResolveCopy {
    const VulkanRenderTarget* rt = nullptr;
    uint64_t content_seq = 0;  // rt->content_seq
    int32_t x0 = 0, y0 = 0, w = 0, h = 0, dx = 0, dy = 0;
    uint32_t dest_base = 0, dest_format = 0, sample_select = 0, dest_swap = 0, dest_number = 0;
    int32_t exp_bias = 0;
    bool operator==(const ResolveCopy&) const = default;
  };
  struct BoundsTrack {
    uint32_t slot = 0;
    bool tracking = false;      // the draws to the target record their bounds
    bool bounds_valid = false;  // the slot saw every draw to the target since its last resolve
    bool expand = false;        // a rectangle list drew since (VulkanResolveShaders::kBoundsExpand)
    uint32_t draws = 0;         // draws to the target since its last resolve
    bool has_last = false;
    ResolveCopy last;           // the target's last color resolve
    VkImage last_dest = VK_NULL_HANDLE;  // its destination image, and that image's copies
    uint64_t last_dest_seq = 0;          // (ResolvedImage::resolve_seq) after it
    // The pattern broke (a copy the shader couldn't make): nothing is known any more.
    void Break() {
      tracking = bounds_valid = expand = has_last = false;
      draws = 0;
    }
  };
  std::unordered_map<const VulkanRenderTarget*, BoundsTrack> bounds_tracks_;
  const VulkanRenderTarget* bounds_slot_owner_[kDrawBoundsSlots] = {};
  uint32_t bounds_next_slot_ = 0;
  bool bounds_buffer_ready_ = false;  // filled once before its first use
  uint64_t resolves_partial_ = 0, resolves_repeated_ = 0;
  uint64_t resolve_reads_[4] = {};  // CountResolveReads: read by 0, 1, 2-3, 4+ draws
  void CountResolveReads(ResolvedImage* r);
  BoundsTrack& BoundsTrackOf(const VulkanRenderTarget* rt);
  // Resets the slot (every slot the first time): a fill of 0xFFFFFFFF between the barriers the
  // vertex shaders' atomics and the resolve shader's reads need. Outside rendering.
  void ResetDrawBounds(VkCommandBuffer cmd, uint32_t slot);
  // Copies rt's (x0, y0, w, h) to (dx, dy) of the dest_w x dest_h image kept under `key` (a
  // resolve's destination address), resolving MSAA; the image is then ready to be shown.
  // dest_format: the image's format (VK_FORMAT_UNDEFINED: rt's); another one is blitted.
  // dest_format_signed: its signed views' format (a MUTABLE image), or UNDEFINED.
  bool CopyTarget(VulkanRenderTarget* rt, int32_t x0, int32_t y0, int32_t w, int32_t h,
                  int32_t dx, int32_t dy, uint32_t dest_w, uint32_t dest_h, uint32_t key,
                  VkFormat dest_format = VK_FORMAT_UNDEFINED,
                  VkFormat dest_format_signed = VK_FORMAT_UNDEFINED);
  // Resolve shaders (vulkan_resolve.h): rt's (x0, y0, w, h) into the image at p.dest_base, at
  // (dx, dy): depth targets into R32_SFLOAT, color targets as the destination format's texel
  // bits. False: not done (a color format the shader can't write: CopyTarget instead).
  bool CopyDepth(VulkanRenderTarget* rt, int32_t x0, int32_t y0, int32_t w, int32_t h,
                 int32_t dx, int32_t dy, const ResolveParams& p);
  // `track` (the resolve bounds, or null) with `copy`, the resolve as ResolveCopy describes it:
  // the copy is cut to the bounds, or skipped, when the destination already holds the rest.
  bool CopyColor(VulkanRenderTarget* rt, int32_t x0, int32_t y0, int32_t w, int32_t h,
                 int32_t dx, int32_t dy, const ResolveParams& p, BoundsTrack* track,
                 const ResolveCopy& copy);
  // --fear_d3d12_vulkan_resolve_dump: reads back the image of a resolve of the dumped frame,
  // texels (0, 0) to (width, height); AfterSwap writes them once the GPU finished the frame.
  void DumpRecord(const ResolveParams& p, uint32_t width, uint32_t height);
  void DumpWrite(uint64_t completed);
  bool DumpingFrame() const;  // the frame being recorded is dumped
  // --fear_d3d12_vulkan_dump_target_draws: reads back color target rt after draw `index` of
  // the dumped frame.
  void DumpTarget(VulkanRenderTarget* rt, uint32_t index);
  uint32_t dump_target_first_ = UINT32_MAX, dump_target_last_ = 0;
  // Records a resolve shader reading rt (through `src_view`, and for depth `stencil_view`) and
  // writing r's storage views (vulkan_resolve.h's bindings): `record` dispatches with the
  // descriptor set. `bounds_lean` (--fear_d3d12_vulkan_resolve_lean, a bounded color copy):
  // its barriers also order the draw bounds buffer, the draws' atomics before the bounds
  // shader and its reset after, in place of separate barriers.
  bool RunResolveShader(VulkanRenderTarget* rt, VkImageView src_view, VkImageView stencil_view,
                        ResolvedImage* r, const std::function<void(VkDescriptorSet)>& record,
                        bool bounds_lean = false);
  // r, (re)created for the formats and size (the old image retired), with a storage view of
  // `storage_format` (UNDEFINED: none needed; a MUTABLE image when it is another format) and
  // for a depth resolve the R32_SFLOAT image beside it; null: failed.
  ResolvedImage* EnsureImage(ResolvedImage& r, VkFormat format, VkFormat format_signed,
                             uint32_t width, uint32_t height, const char* what,
                             VkFormat storage_format = VK_FORMAT_UNDEFINED, bool depth = false);
  void DestroyImage(ResolvedImage& r);
  // M5: the image Swap shows (the front buffer, or --fear_d3d12_vulkan_show_target's), given
  // to the present pass; false: a black frame.
  bool SetPresentImage(const SwapCall& swap);
  bool front_format_logged_ = false;  // a front buffer format the present pass doesn't show
  uint32_t front_logged_ = 0;         // changes of the front buffer's format, swizzle, size...
  uint64_t front_seen_ = 0;           // ...last logged
  bool bias_dropped_logged_ = false;  // a resolve blit without its exponent bias
  bool depth_format_logged_ = false;  // a depth resolve into a format it can't write
  bool CanBlit(VkFormat src, VkFormat dst);
  std::unordered_map<uint64_t, bool> blit_support_;
  // --fear_d3d12_vulkan_show_target: this frame's draws and lowest drawn row per color target.
  struct TargetUse {
    uint32_t draws = 0, rows = 0;
  };
  std::unordered_map<VulkanRenderTarget*, TargetUse> target_use_;
  // M4: the views, samplers and signs of a shader's texture bindings (uploading what changed,
  // which ends the rendering instance); appended to texture_views_ / samplers_.
  void GetShaderTextures(const rex::graphics::SpirvShader& shader, const DrawState& state,
                      uint8_t signs[32]);
  std::vector<VkImageView> texture_views_;
  std::vector<VkSampler> samplers_;
  // Reasons draws were unsupported, logged once each.
  std::unordered_set<const char*> unsupported_seen_;
  uint32_t unsupported_logged_ = 0;
  TextureStats texture_stats_;
  LumaProbe luma_;
};

}  // namespace fear::d3d12
