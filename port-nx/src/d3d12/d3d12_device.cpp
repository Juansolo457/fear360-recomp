// D3D12 renderer (ported from pgr4-native 8ea20d5, MIT; see README.md): frame command lists, fence
// and output image on ReXGlue's presenter's device (d3d12_device.h).
#include "d3d12_device.h"

#include "clock.h"
#include "gpu_timer.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <simde/x86/sse.h>

#include <algorithm>

REXCVAR_DEFINE_BOOL(fear_d3d12_gpu_serialize, false, "F.E.A.R. D3D12",
                    "Debug: wait for the GPU at the end of every frame, so no frame is in flight "
                    "while the next one is recorded (slow)");

namespace fear::d3d12 {

bool Failed(HRESULT hr, const char* what) {
  if (SUCCEEDED(hr)) return false;
  REXLOG_ERROR("D3D12: {} failed: HRESULT {:08X}", what, uint32_t(hr));
  return true;
}

bool CreateBuffer(ID3D12Device* device, D3D12_HEAP_TYPE type, uint64_t size,
                  D3D12_RESOURCE_STATES state, const char* what, ComPtr<ID3D12Resource>* out) {
  const D3D12_HEAP_PROPERTIES heap = {type};
  const D3D12_RESOURCE_DESC desc = BufferDesc(size);
  return !Failed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                 IID_PPV_ARGS(out->ReleaseAndGetAddressOf())),
                 what);
}

pD3DCompile D3DCompiler() {
  static const pD3DCompile compile = [] {
    const HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto fn =
        dll ? reinterpret_cast<pD3DCompile>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!fn) REXLOG_ERROR("D3D12: d3dcompiler_47.dll / D3DCompile not found");
    return fn;
  }();
  return compile;
}

D3D12Device::~D3D12Device() { WaitIdle(); }

bool D3D12Device::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
  device_ = device;
  queue_ = queue;
  // The adapter, for its name and vendor (shader workarounds key on it).
  char name[128] = "(unknown adapter)";
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()))) &&
      SUCCEEDED(factory->EnumAdapterByLuid(device_->GetAdapterLuid(),
                                           IID_PPV_ARGS(adapter.GetAddressOf())))) {
    DXGI_ADAPTER_DESC1 ad = {};
    adapter->GetDesc1(&ad);
    vendor_id_ = ad.VendorId;
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
  }
  if (Failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf())),
             "CreateFence") ||
      Failed(
          device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(idle_fence_.GetAddressOf())),
          "CreateFence(idle)"))
    return false;
  fence_event_ = CreateAutoResetEvent();
  idle_event_ = CreateAutoResetEvent();
  if (!fence_event_ || !idle_event_) {
    REXLOG_ERROR("D3D12: CreateEvent failed: error {}", GetLastError());
    return false;
  }
  for (auto& a : allocators_)
    if (Failed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(a.GetAddressOf())),
               "CreateCommandAllocator"))
      return false;
  if (Failed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(),
                                        nullptr, IID_PPV_ARGS(list_.GetAddressOf())),
             "CreateCommandList"))
    return false;
  list_->SetName(L"fear d3d12 frame");
  D3D12_DESCRIPTOR_HEAP_DESC hd = {};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  hd.NumDescriptors = 1;
  if (Failed(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(rtv_heap_.GetAddressOf())),
             "CreateDescriptorHeap(RTV)"))
    return false;
  REXLOG_INFO("D3D12: D3D12 on {}, on ReXGlue's presenter's device and queue", name);
  return true;
}

bool D3D12Device::EnsureOutput(uint32_t width, uint32_t height) {
  width = std::max(1u, width);
  height = std::max(1u, height);
  if (output_ && width == width_ && height == height_) return true;
  // The hand-over copied the old image on the queue; it is released once the GPU is done.
  if (output_) WaitIdle();
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = width;
  d.Height = height;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.Format = kBackBufferFormat;
  d.SampleDesc.Count = 1;
  d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  const D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
  if (Failed(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(output_.ReleaseAndGetAddressOf())),
             "CreateCommittedResource(output)"))
    return false;
  output_->SetName(L"fear d3d12 output");
  device_->CreateRenderTargetView(output_.Get(), nullptr, back_buffer_rtv());
  if (width_ != width || height_ != height)
    REXLOG_INFO("D3D12: output image {}x{}", width, height);
  width_ = width;
  height_ = height;
  return true;
}

void D3D12Device::WaitFence(uint64_t value) {
  if (fence_->GetCompletedValue() >= value) return;
  fence_->SetEventOnCompletion(value, fence_event_.get());
  WaitForSingleObject(fence_event_.get(), INFINITE);
}

void D3D12Device::WaitIdle() {
  // A fence of its own: the frame fence's next value belongs to the frame still being recorded,
  // whose resources would count as done before it is submitted.
  if (!queue_ || !idle_fence_ || !idle_event_) return;
  const uint64_t v = ++idle_fence_value_;
  if (FAILED(queue_->Signal(idle_fence_.Get(), v)) || idle_fence_->GetCompletedValue() >= v) return;
  idle_fence_->SetEventOnCompletion(v, idle_event_.get());
  WaitForSingleObject(idle_event_.get(), INFINITE);
}

bool D3D12Device::EnableGpuTimer() {
  auto timer = std::make_unique<GpuTimer>();
  if (!timer->Initialize(device_.Get(), queue_.Get(), kFramesInFlight)) return false;
  timer->BeginFrame(list_.Get(), slot_);
  gpu_timer_ = std::move(timer);
  return true;
}

bool D3D12Device::OpenList() {
  const uint64_t t0 = NowNs();
  WaitFence(slot_fence_[slot_]);
  end_frame_times_.fence_ns = NowNs() - t0;
  if (Failed(allocators_[slot_]->Reset(), "CommandAllocator::Reset") ||
      Failed(list_->Reset(allocators_[slot_].Get(), nullptr), "CommandList::Reset"))
    return false;
  if (gpu_timer_) gpu_timer_->BeginFrame(list_.Get(), slot_);
  return true;
}

bool D3D12Device::CloseAndExecute(std::span<ID3D12CommandList* const> first) {
  if (Failed(list_->Close(), "CommandList::Close")) return false;
  submit_lists_.assign(first.begin(), first.end());
  submit_lists_.push_back(list_.Get());
  // The constant uploads are streaming stores, which are weakly ordered: make them visible
  // before the GPU can read them.
  simde_mm_sfence();
  queue_->ExecuteCommandLists(UINT(submit_lists_.size()), submit_lists_.data());
  return true;
}

bool D3D12Device::Submit(std::span<ID3D12CommandList* const> first) {
  if (gpu_timer_) gpu_timer_->Pass(list_.Get(), GpuTimer::KeySubmit());
  // A list may be reset while its submission runs; the allocator only once the frame's fence
  // completed (OpenList).
  return CloseAndExecute(first) &&
         !Failed(list_->Reset(allocators_[slot_].Get(), nullptr), "CommandList::Reset");
}

bool D3D12Device::EndFrame(bool shown, std::span<ID3D12CommandList* const> first) {
  const uint64_t t0 = NowNs();
  if (gpu_timer_) gpu_timer_->EndFrame(list_.Get());
  if (!CloseAndExecute(first)) return false;
  end_frame_times_.submit_ns = NowNs() - t0;
  end_frame_times_.present_ns = 0;
  end_frame_times_.presented = shown;
  end_frame_times_.presents += shown;
  const uint64_t v = next_fence_++;
  if (Failed(queue_->Signal(fence_.Get(), v), "CommandQueue::Signal")) {
    Failed(device_->GetDeviceRemovedReason(), "device removed, reason");
    return false;
  }
  slot_fence_[slot_] = v;
  if (REXCVAR_GET(fear_d3d12_gpu_serialize)) WaitFence(v);
  slot_ = (slot_ + 1) % kFramesInFlight;
  return OpenList();
}

}  // namespace fear::d3d12
