// D3D12 renderer (ported from pgr4-native 8ea20d5, MIT; see README.md): the frame command lists,
// fence and output image, on ReXGlue's D3D12 presenter's device and direct queue.
//
// F.E.A.R.: the renderer draws the game alone (docs/NATIVE_RENDERER.md, D3D12 only), and
// ReXGlue's D3D12 presenter keeps the window: it shows each frame the renderer hands it
// (present.h, HandToPresenter) with the ImGui panels and the system dialogs on top, scaled to the
// window and paced on the display. So there is no swapchain or window here, only the output image
// each frame's last pass draws into, in the presenter's guest output format so that the hand-over
// is a copy. pgr4-native's and GRAW's renderer owned a device, queue, swapchain and window.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#undef DrawState  // winuser.h maps it to DrawStateA/W; d3d_state.h's struct has that name

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "win_handle.h"

namespace fear::d3d12 {

using Microsoft::WRL::ComPtr;

class GpuTimer;

class D3D12Device {
 public:
  static constexpr uint32_t kFramesInFlight = 2;
  // The output image's format: ReXGlue's D3D12Presenter::kGuestOutputFormat, which the hand-over
  // copies into (a copy needs the same format). The HDR output pgr4-native has is not used: the
  // presenter outputs SDR (the name stays for present.cpp's HDR pipeline, never chosen).
  static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
  static constexpr DXGI_FORMAT kHdrBackBufferFormat = kBackBufferFormat;

  ~D3D12Device();

  // Creates the fence, allocators and frame list on `device` (ReXGlue's D3D12 provider's) and
  // opens the first frame's list. Everything is submitted to `queue`, the provider's direct queue,
  // which the presenter's guest output refresh requires. Logs and returns false on failure.
  bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

  // Between frames, before the frame's last pass records into back_buffer(): the output image
  // at width x height, created again (once the GPU is done with the old one) when the size
  // changed. Logs and returns false on failure.
  bool EnsureOutput(uint32_t width, uint32_t height);

  ID3D12Device* device() const { return device_.Get(); }
  ID3D12CommandQueue* command_queue() const { return queue_.Get(); }
  ID3D12GraphicsCommandList* command_list() const { return list_.Get(); }

  // The output image (state COMMON, which is PRESENT, outside the pass that draws into it) and its
  // RTV; null before EnsureOutput.
  ID3D12Resource* back_buffer() const { return output_.Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv() const {
    return rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  // The adapter's PCI vendor ID (DXGI_ADAPTER_DESC1::VendorId).
  uint32_t vendor_id() const { return vendor_id_; }
  // No HDR output: the presenter's guest output is SDR.
  bool hdr() const { return false; }
  float hdr_peak_nits() const { return 0.0f; }

  // Fence value EndFrame signals for the frame being recorded; last value the GPU reached.
  uint64_t frame_fence() const { return next_fence_; }
  uint64_t completed_fence() const { return fence_->GetCompletedValue(); }
  ID3D12Fence* fence() const { return fence_.Get(); }
  // The frame being recorded's slot in [0, kFramesInFlight): its previous frame has completed,
  // so per-frame upload memory indexed by it is free to write.
  uint32_t frame_slot() const { return slot_; }
  void WaitFence(uint64_t value);

  // Closes and submits the frame's command list, signals the frame fence, then waits for the
  // slot kFramesInFlight frames back and opens the next frame's list. `shown`: the frame went to
  // the presenter (counted in end_frame_times).
  // `first`: closed lists that run ahead of the frame's in the same ExecuteCommandLists, for
  // work that would otherwise split its render passes (TextureCache::CloseUploadLists); the
  // frame's fence covers them, GpuTimer passes do not.
  // Returns false once the device failed (logged once); the caller stops.
  bool EndFrame(bool shown, std::span<ID3D12CommandList* const> first = {});
  // Submits what the frame recorded so far (after `first`, as in EndFrame) and continues it in
  // the same list, reset on the frame's allocator, so the GPU starts on it while the rest of the
  // frame is recorded. The frame fence and slot stay the frame's (EndFrame signals once for all
  // of its submissions). Nothing bound on the list carries over. Returns false on failure
  // (logged).
  bool Submit(std::span<ID3D12CommandList* const> first = {});
  // Where the last EndFrame spent its time.
  struct EndFrameTimes {
    uint64_t submit_ns = 0;   // Close + ExecuteCommandLists
    uint64_t present_ns = 0;  // (none: the presenter presents)
    uint64_t fence_ns = 0;    // OpenList: waiting for the GPU to finish the slot's last frame
    bool presented = false;   // the frame went to the presenter
    uint64_t presents = 0;    // frames handed over so far, this one included
  };
  const EndFrameTimes& end_frame_times() const { return end_frame_times_; }

  // Waits for the GPU to finish everything submitted so far, mid-frame Submits included; the
  // frame being recorded keeps its frame_fence.
  void WaitIdle();

  // Timestamps per pass in every frame from now on (gpu_timer.h), starting with the open list.
  // Logs and returns false on failure.
  bool EnableGpuTimer();
  // nullptr unless EnableGpuTimer succeeded.
  GpuTimer* gpu_timer() const { return gpu_timer_.get(); }

 private:
  bool OpenList();
  bool CloseAndExecute(std::span<ID3D12CommandList* const> first);

  uint32_t width_ = 0, height_ = 0;
  uint32_t vendor_id_ = 0;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12Fence> fence_;
  UniqueHandle fence_event_;
  uint64_t next_fence_ = 1;
  ComPtr<ID3D12Fence> idle_fence_;  // WaitIdle's
  UniqueHandle idle_event_;
  uint64_t idle_fence_value_ = 0;
  ComPtr<ID3D12CommandAllocator> allocators_[kFramesInFlight];
  uint64_t slot_fence_[kFramesInFlight] = {};
  uint32_t slot_ = 0;
  ComPtr<ID3D12GraphicsCommandList> list_;
  std::vector<ID3D12CommandList*> submit_lists_;  // CloseAndExecute's
  EndFrameTimes end_frame_times_;

  ComPtr<ID3D12Resource> output_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;

  std::unique_ptr<GpuTimer> gpu_timer_;
};

// Logs a failed HRESULT with the call text; returns true when it failed.
bool Failed(HRESULT hr, const char* what);

// A barrier moving every subresource of `resource` from `before` to `after`.
inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
                                                D3D12_RESOURCE_STATES before,
                                                D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = resource;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = before;
  b.Transition.StateAfter = after;
  return b;
}

// A buffer of `size` bytes.
constexpr D3D12_RESOURCE_DESC BufferDesc(uint64_t size,
                                         D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  return {D3D12_RESOURCE_DIMENSION_BUFFER, 0,    size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
          D3D12_TEXTURE_LAYOUT_ROW_MAJOR,  flags};
}

// A committed buffer of `size` bytes on a heap of `type`, created in `state`. Logs `what` and
// returns false on failure.
bool CreateBuffer(ID3D12Device* device, D3D12_HEAP_TYPE type, uint64_t size,
                  D3D12_RESOURCE_STATES state, const char* what, ComPtr<ID3D12Resource>* out);

// D3DCompile from d3dcompiler_47.dll (Windows 10/11 and Proton ship it), loaded on the first call;
// nullptr (logged) without it.
pD3DCompile D3DCompiler();

}  // namespace fear::d3d12
