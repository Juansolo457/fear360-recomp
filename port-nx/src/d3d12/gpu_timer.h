// D3D12 renderer (ported from pgr4-native 8ea20d5, MIT; see native/README.md): GPU time per pass of the native frame
// (--fear_d3d12_gpu_timing).
//
// The renderer names the pass each command it records belongs to: draws on one set of render
// targets, a clear, a resolve, the present blit (Key*). Whenever the pass changes, a timestamp
// goes into the frame's command list, so the span between two timestamps is the GPU time of
// the pass recorded in it. EndFrame resolves the frame's timestamps into the readback slot of
// its command allocator; BeginFrame reads them once that allocator comes round again
// (D3D12Device::OpenList, after the slot's fence), so timing never stalls the GPU or the
// recording thread.
//
// Timestamps are written when the preceding work has finished at the bottom of the pipe:
// a span runs from the end of the previous pass to the end of its own. Neighbouring passes
// overlap on the GPU, so one span is approximate; a frame's spans add up to its GPU time.
// Spans are grouped by pass key plus the key's occurrence in the frame (the second run of
// draws on the scene targets after a resolve is its own group), and Log prints the costliest
// groups in frame order. A frame far slower than the recent ones is logged on its own as a
// spike: its costliest spans next to their groups' mean, so a heavier scene can be told from a
// stall inside one pass. Frames are numbered as in the frame CSV (the first one is frame 0).
//
// Threading: the thread that records the frames (the render thread by default).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fear::d3d12 {

struct ClearCall;
struct ResolveParams;

// The API's timestamp queries, which the timer is otherwise free of: the D3D12 device's (a
// query heap and a readback buffer, d3d12_device.cpp) and the Vulkan device's (a query pool,
// vulkan_device.cpp). `list` is the API's command list or buffer the frame records into.
// Queries are numbered per slot: slot * GpuTimer::kMaxQueries + the frame's count.
class GpuTimestamps {
 public:
  virtual ~GpuTimestamps() = default;
  // A timestamp when the work recorded before it has finished.
  virtual void Write(void* list, uint32_t query) = 0;
  // At the frame's start, before any Write: queries that must be reset first (Vulkan).
  virtual void Reset(void* list, uint32_t first, uint32_t count) {}
  // At the frame's end: queries that must be copied somewhere readable (D3D12).
  virtual void Resolve(void* list, uint32_t first, uint32_t count) {}
  // The ticks of a finished frame's queries (its fence was waited for); nullptr if unavailable.
  // Valid until Unmap.
  virtual const uint64_t* Map(uint32_t first, uint32_t count) = 0;
  virtual void Unmap() {}
};

class GpuTimer {
 public:
  // Spans per frame; passes past it are charged to the last span.
  static constexpr uint32_t kMaxSpans = 2047;
  // Queries per slot: one more than spans.
  static constexpr uint32_t kMaxQueries = kMaxSpans + 1;

  // `slots`: the frames-in-flight the device rotates through (each a command allocator or
  // command buffer); `frequency`: timestamp ticks per second.
  bool Initialize(std::unique_ptr<GpuTimestamps> stamps, uint64_t frequency, uint32_t slots);

  // The frame's command list was opened on `slot`: collects the frame that slot held last
  // (complete, its fence was waited for) and writes the new frame's first timestamp.
  void BeginFrame(void* list, uint32_t slot);
  // The commands recorded next belong to pass `key`.
  void Pass(void* list, uint64_t key) {
    if (key != current_) Switch(list, key);
    ++frames_[slot_].spans.back().commands;
  }
  // Before the list is closed: the last timestamp, and the copy into the readback slot.
  void EndFrame(void* list);

  // Pass keys. Draws: render target registers and which targets the draw writes.
  static uint64_t KeyDraw(const uint32_t* regs);
  static uint64_t KeyClear(const ClearCall& c);
  // Resolver::Resolve splits its pass into these phases (kSource is the key the renderer
  // passes before calling it): the source's transition and EDRAM fills, the copy dispatch, the
  // destination's transition back to shader reads, and the clear of the source rectangle.
  enum ResolvePhase : uint32_t { kResolveSource, kResolveCopy, kResolveRelease, kResolveClear };
  static uint64_t KeyResolve(const ResolveParams& p, ResolvePhase phase = kResolveSource);
  static uint64_t KeyPresent();
  // The end of a list submitted mid-frame (D3D12Device::Submit): its span runs to the first
  // timestamp in the next list, the GPU's wait for that list.
  static uint64_t KeySubmit();

  // --fear_d3d12_gpu_timing_shaders: draws are timed per (vertex shader, pixel shader) pair on
  // a surface pitch and MSAA mode instead of per render target set (KeyDrawShaders), and all
  // of a pair's spans in a frame form one group.
  void set_per_shader(bool on) { per_shader_ = on; }
  bool per_shader() const { return per_shader_; }
  uint64_t KeyDrawShaders(const uint32_t* regs, uint64_t vs_hash, uint64_t ps_hash);
  // Ucode hashes of the shaders in the costliest groups of the last Log not returned before.
  std::vector<uint64_t> TakeNewTopShaders() { return std::exchange(new_top_shaders_, {}); }

  // Logs the frames collected since the last call: GPU time per frame, the GPU's idle time
  // between frames, and the costliest pass groups. Resets the window.
  void Log();

  // GPU time of the frame collected last, in ms: after D3D12Device::EndFrame, the frame before
  // the one just submitted (its slot's fence was waited for when the next list opened).
  double last_frame_ms() const { return double(last_frame_ticks_) * 1000.0 / double(frequency_); }

 private:
  struct Span {
    uint64_t key;
    uint32_t commands;
  };
  struct Frame {
    std::vector<Span> spans;
    uint64_t number = 0;
    uint32_t queries = 0;
    bool pending = false;  // resolved into the readback slot, not collected yet
  };
  struct SpikeSpan {
    uint64_t ticks;
    uint32_t index;
  };
  struct Group {
    uint64_t ticks = 0, spans = 0, commands = 0, position = 0;
  };

  void Switch(void* list, uint64_t key);
  void Stamp(void* list);
  void Collect(uint32_t slot);
  bool IsSpike(uint64_t ticks);
  void LogSpike(const Frame& f, uint64_t ticks);
  std::string Describe(uint64_t key) const;

  std::unique_ptr<GpuTimestamps> stamps_;
  uint64_t frequency_ = 1;
  std::vector<Frame> frames_;
  uint32_t slot_ = 0;
  uint64_t current_ = 0;
  std::unordered_map<uint64_t, uint32_t> occurrences_;  // this frame's spans per key

  bool per_shader_ = false;
  std::vector<std::pair<uint64_t, uint64_t>> pairs_;  // (VS, PS) hashes by KeyDrawShaders id
  std::unordered_map<uint64_t, uint32_t> pair_ids_;
  std::unordered_set<uint64_t> shaders_reported_;
  std::vector<uint64_t> new_top_shaders_;

  // The window Log reports.
  std::unordered_map<uint64_t, Group> groups_;
  uint64_t frames_collected_ = 0, frame_ticks_ = 0, frame_ticks_max_ = 0, idle_ticks_ = 0;
  uint64_t spans_collected_ = 0;
  uint64_t last_end_ = 0;  // end timestamp of the last collected frame
  uint64_t last_frame_ticks_ = 0;

  uint64_t next_number_ = 0;
  uint64_t typical_ticks_ = 0;  // running mean of frame GPU time, spikes clamped
  uint32_t spikes_logged_ = 0;  // in the current Log window
  std::vector<SpikeSpan> spike_spans_;
};

}  // namespace fear::d3d12
