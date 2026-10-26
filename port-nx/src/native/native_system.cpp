// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Project8Recomp's generic Xenos Vulkan renderer (GPL-3.0),
// itself based on nfsmw-nx. Title-specific hooks and shader replacements are excluded.
//
// Native Xenos graphics system for F.E.A.R. NX. It consumes the Xbox 360 GPU
// command ring and updates register and memory state: fences, write-backs, interrupts, swaps
// and their pacing. F.E.A.R.: the D3D12 renderer (port-nx/src/d3d12) draws the game from its
// Direct3D calls and hands each frame to ReXGlue's D3D12 presenter, which this system owns
// (with the window, the ImGui panels and the system dialogs). fear-nx's Vulkan renderer is gone
// (docs/NATIVE_RENDERER.md, D3D12 only).
#include "native_system.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/image_info.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/system/xvideo.h>
#include <rex/thread.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>

#include "display_vblank.h"

REXCVAR_DECLARE(int32_t, switch_cp_core);
REXCVAR_DECLARE(int32_t, query_occlusion_fake_sample_count);

// The game starts its next frame as soon as the swap-complete interrupt of the
// previous one arrives (its frame limiter waits for a counter that interrupt
// bumps). With the native renderer that interrupt came back immediately, so
// frames were paced only by how long they took: 16.7, 33 or 50 ms in turn,
// which feels choppy even at a good average. Holding the interrupt to vblank
// boundaries, at least this many vblanks after the previous one, gives the
// even cadence a console gets from vsync; 2 is a steady 30.
REXCVAR_DEFINE_BOOL(nx_pace_before_present, false, "F.E.A.R. NX",
                    "Hold a frame for the frame rate cap before presenting it rather than "
                    "after, so frames go on screen evenly spaced");
REXCVAR_DEFINE_UINT32(nx_min_frame_vblanks, 1, "F.E.A.R. NX",
                      "Native renderer: minimum 60 Hz vblanks between frames (1 = the game's "
                      "own pacing, 2 = an even 30 fps)");

// F.E.A.R.: the game's executable image as loaded (decrypted, decompressed, 0x82000000 on), written
// once, when the game sets its GPU interrupt callback (or at the first frame), for reading its
// code and constants offline. Game data: write it to private/, never commit it.
REXCVAR_DEFINE_STRING(fear_dump_image, "", "F.E.A.R.",
                      "Write the loaded executable image (0x82000000 on) to this file once it runs");
// The game's image layout, from the generated translation (fear_init.cpp).
extern const rex::PPCImageInfo PPCImageConfig;

// F.E.A.R. (docs/NATIVE_RENDERER.md, step 1): how much of each frame the ring runs more than once.
// With predicated tiling the game's Direct3D records the frame once and replays it for each EDRAM
// tile; a packet keeps its guest address in every replay, so counting the packets run against the
// distinct addresses gives the replay factor an API-level renderer would not pay.
REXCVAR_DEFINE_BOOL(fear_tile_stats, false, "F.E.A.R.",
                    "Count the draws and register writes the ring runs per frame against the "
                    "unique ones (tile replays), in the 10-second report");

// Adapted from Project8Recomp (GPL-3.0), src/game/src/native/; generic Xenos GPU code.

#if defined(__SWITCH__)
extern "C" {
#include <switch/kernel/svc.h>
}
extern "C" void RexGmSetReadOnlyMapped(bool enabled);
// The frame counter the console FPS overlay and the profiler read (the
// emulated path counts its swaps there too).
extern "C" void RexSwitchPerfCount(unsigned id);
#endif

namespace fear_nx::native {
namespace {

namespace xenos = rex::graphics::xenos;
using rex::X_STATUS;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kMmioBase = 0x7FC80000;
constexpr uint32_t kMmioMask = 0xFFFF0000;
constexpr uint32_t kMmioSize = 0x0000FFFF;
constexpr uint32_t kRegisterCount = 0x5003;  // RegisterFile::kRegisterCount

// Registers with a fixed value or an effect, as in the SDK's graphics_system.cpp.
constexpr uint32_t kRegCpRbWptr = 0x01C5;
constexpr uint32_t kRegRbEdramTiming = 0x0F00;
constexpr uint32_t kRegRbBcControl = 0x0F01;
constexpr uint32_t kRegD1ModeVCounter = 0x194C;
constexpr uint32_t kRegD1ModeVblankVlineStatus = 0x1951;
constexpr uint32_t kRegD1ModeViewportSize = 0x1961;

constexpr uint16_t kMaxExtent = 2048 >> 3;        // xenos::kTexture2DCubeMaxWidthHeight >> 3
constexpr int kMaxIndirectDepth = 4;
// Without a GPU some waits can never be satisfied; they are cut short so the
// ring cannot hang.
constexpr auto kWaitRegMemMax = std::chrono::milliseconds(200);

std::atomic<uint64_t> g_swaps_presented{0};
// The graphics system's gamma ramp, for GammaRamp256 (the D3D12 renderer's presenter).
std::atomic<const uint32_t*> g_gamma_ramp{nullptr};

// Big-endian words of a command buffer. In the ring the position wraps around
// (its size is a power of two); in an indirect buffer it does not.
struct Reader {
  const uint8_t* base = nullptr;
  uint32_t mask = 0;  // words - 1 in the ring; 0 in a linear buffer
  uint32_t pos = 0;
  uint32_t end = 0;

  uint32_t Pending() const { return mask ? ((end - pos) & mask) : (end - pos); }
  uint32_t Peek() const { return rex::memory::load_and_swap<uint32_t>(base + size_t(pos) * 4); }
  uint32_t Read() {
    const uint32_t value = Peek();
    Advance(1);
    return value;
  }
  void Advance(uint32_t words) { pos = mask ? ((pos + words) & mask) : (pos + words); }
};

bool Compare(uint32_t info, uint32_t value, uint32_t reference) {
  switch (info & 0x7) {
    case 0x0:
      return false;
    case 0x1:
      return value < reference;
    case 0x2:
      return value <= reference;
    case 0x3:
      return value == reference;
    case 0x4:
      return value != reference;
    case 0x5:
      return value >= reference;
    case 0x6:
      return value > reference;
    default:
      return true;
  }
}

class NativeGraphicsSystem final : public rex::system::IGraphicsSystem {
 public:
  NativeGraphicsSystem()
      : register_file_(std::make_unique<rex::graphics::RegisterFile>()),
        registers_(register_file_->values) {
#if defined(__SWITCH__)
    // Nothing watches guest pages for writes without the SDK's shared memory,
    // so the pages the game makes read-only can stay mapped. Otherwise every
    // read of them is an emulated fault: 200,000 a second at the menu, which
    // held it at 8 fps. The emulated path gets this from SharedMemory.
    RexGmSetReadOnlyMapped(true);
#endif
    std::memset(registers_, 0, sizeof(register_file_->values));
    // The linear 256-entry gamma ramp the SDK starts with.
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t value = i * 0x3FF / 0xFF;
      gamma_ramp_[i] = value | (value << 10) | (value << 20);
    }
    g_gamma_ramp.store(gamma_ramp_, std::memory_order_release);
  }
  ~NativeGraphicsSystem() override { Shutdown(); }

  X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override {
    if (presenter_) {
      return X_STATUS_SUCCESS;
    }
    app_context_ = app_context;
    if (!provider_) {
      provider_ = rex::ui::d3d12::D3D12Provider::Create();
      if (!provider_) {
        REXLOG_ERROR("[native] Could not create the Direct3D 12 device");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    auto create = [this]() { presenter_ = provider_->CreatePresenter(); };
    if (app_context_) {
      app_context_->CallInUIThreadSynchronous(create);
    } else {
      create();
    }
    if (!presenter_) {
      REXLOG_ERROR("[native] Could not create the presenter");
      return X_STATUS_UNSUCCESSFUL;
    }
    REXLOG_INFO("[native] Native Xenos graphics system initialized with the D3D12 presenter");
    return X_STATUS_SUCCESS;
  }

  X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                         rex::system::KernelState* kernel_state) override {
    dispatcher_ = dispatcher;
    kernel_state_ = kernel_state;
    memory_ = dispatcher->memory();
    if (!mmio_registered_) {
      mmio_registered_ = memory_->AddVirtualMappedRange(kMmioBase, kMmioMask, kMmioSize, this,
                                                        &ReadMmioThunk, &WriteMmioThunk);
      if (!mmio_registered_) {
        REXLOG_ERROR("[native] Could not register the GPU MMIO range");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    if (running_.exchange(true)) {
      return X_STATUS_SUCCESS;
    }
    display_pacing_ = fear::display_vblank::GetPlan().active;
    // F.E.A.R.: not under Proton, where DXGI's WaitForVBlank is a sleep-and-poll loop: half a
    // Steam Deck core for the pacing report's refresh counts (#40).
    if (!display_pacing_ && !fear::display_vblank::UnderWine()) {
      display_watch_ = std::thread([this]() { WatchDisplay(); });
    }
    vblank_thread_ = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(kernel_state_, 128 * 1024, 0, [this]() { return VblankLoop(); }));
    vblank_thread_->set_name("GPU VSync native");
    vblank_thread_->Create();
    ring_thread_ = rex::system::object_ref<rex::system::XHostThread>(
        new rex::system::XHostThread(kernel_state_, 128 * 1024, 0, [this]() { return RingLoop(); }));
    ring_thread_->set_name("GPU ring native");
    ring_thread_->Create();
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override { return presenter_ != nullptr; }
  rex::ui::GraphicsProvider* provider() const override { return provider_.get(); }
  rex::ui::Presenter* presenter() const override { return presenter_.get(); }

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    callback_data_.store(user_data, std::memory_order_release);
    callback_.store(callback, std::memory_order_release);
    REXLOG_INFO("[native] Interrupt callback {:08X} ({:08X})", callback, user_data);
    // The executable is loaded and running by now; a game that fails before its first frame
    // still gets its image written.
    DumpImage();
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    // As CommandProcessor::InitializeRingBuffer: (1 << (size_log2 + 3)) bytes.
    ring_words_.store(uint32_t(1) << (size_log2 + 1), std::memory_order_release);
    ring_base_.store(ptr, std::memory_order_release);
    ring_generation_.fetch_add(1, std::memory_order_acq_rel);
    REXLOG_INFO("[native] Ring at {:08X}, {} words", ptr, uint32_t(1) << (size_log2 + 1));
  }

  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {
    (void)block_size_log2;
    read_pointer_writeback_.store(ptr, std::memory_order_release);
  }

  void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id,
                               bool blocking) override {
    // The D3D12 renderer keeps its own pipeline store (port-nx/src/d3d12/pipeline_store.h).
    (void)cache_root;
    (void)title_id;
    (void)blocking;
  }

  void Shutdown() override {
    if (running_.exchange(false)) {
      {
        std::lock_guard<std::mutex> lock(ring_mutex_);
      }
      ring_cv_.notify_all();
      if (ring_thread_) {
        ring_thread_->Wait(0, 0, 0, nullptr);
        ring_thread_.reset();
      }
      if (vblank_thread_) {
        vblank_thread_->Wait(0, 0, 0, nullptr);
        vblank_thread_.reset();
      }
      if (display_watch_.joinable()) {
        display_watch_.join();
      }
    }
    if (presenter_) {
      if (app_context_) {
        app_context_->CallInUIThreadSynchronous([this]() { presenter_.reset(); });
      }
      presenter_.reset();
    }
    provider_.reset();
  }

 private:
  // --- MMIO registers ----------------------------------------------------------

  static uint32_t ReadMmioThunk(void* ppc_context, void* context, uint32_t address) {
    (void)ppc_context;
    return static_cast<NativeGraphicsSystem*>(context)->ReadMmio(address);
  }

  static void WriteMmioThunk(void* ppc_context, void* context, uint32_t address, uint32_t value) {
    (void)ppc_context;
    static_cast<NativeGraphicsSystem*>(context)->WriteMmio(address, value);
  }

  uint32_t ReadMmio(uint32_t address) {
    const uint32_t index = (address & 0xFFFF) / 4;
    switch (index) {
      case kRegRbEdramTiming:
        return 0x08100748;
      case kRegRbBcControl:
        return 0x0000200E;
      case kRegD1ModeVCounter: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        return std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
      }
      case kRegD1ModeVblankVlineStatus:
        return 1;  // in vblank
      case kRegD1ModeViewportSize: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        const uint32_t width = std::min(uint32_t(mode.display_width), uint32_t(0x0FFF));
        const uint32_t height = std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
        return (width << 16) | height;
      }
      default:
        break;
    }
    return Register(index);
  }

  void WriteMmio(uint32_t address, uint32_t value) {
    const uint32_t index = (address & 0xFFFF) / 4;
    if (index == kRegCpRbWptr) {
      write_pointer_.store(value, std::memory_order_release);
      // Taking the lock for an instant keeps the wakeup from being lost when
      // the ring thread is between checking and going to sleep.
      {
        std::lock_guard<std::mutex> lock(ring_mutex_);
      }
      ring_cv_.notify_one();
    }
    // No side effects through MMIO (graphics_system.cpp).
    if (index < kRegisterCount) {
      registers_[index] = value;
    }
  }

  uint32_t Register(uint32_t index) const {
    return index < kRegisterCount ? registers_[index] : 0;
  }

  // Consecutive registers from the stream. Blocks of shader constants, most of the register
  // traffic (every draw sets its matrices), skip the per-register call and switch.
  void WriteRegisters(uint32_t index, uint32_t count, Reader& data) {
    if (index >= 0x4000 && index + count <= 0x4928) {
      for (uint32_t i = 0; i < count; ++i) {
        registers_[index + i] = data.Read();
      }
      return;
    }
    for (uint32_t i = 0; i < count; ++i) {
      WriteRegister(index + i, data.Read());
    }
  }

  // A register write from the ring, with the side effects it has when the GPU
  // processes it (CommandProcessor::WriteRegister). The Direct3D writes a
  // SCRATCH_REG and then waits with WAIT_REG_MEM until it appears at
  // SCRATCH_ADDR, so those are copied to memory; COHER_STATUS_HOST stays
  // pending until polled.
  void WriteRegister(uint32_t index, uint32_t value) {
    if (index >= kRegisterCount) {
      return;
    }
    if (index == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST) {
      value |= UINT32_C(0x80000000);
    }
    registers_[index] = value;
    switch (index) {
      case rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX:
        gamma_ramp_component_ = 0;
        return;
      case rex::graphics::XE_GPU_REG_DC_LUT_SEQ_COLOR: {
        // CommandProcessor::WriteRegister: red, green, blue in sequence, the
        // write enable mask in blue, green, red order.
        uint32_t entry = registers_[rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX] & 0xFF;
        if (registers_[rex::graphics::XE_GPU_REG_DC_LUT_WRITE_EN_MASK] &
            (1u << (2 - gamma_ramp_component_))) {
          uint32_t color = (value & 0xFFFF) >> 6;
          uint32_t shift = 20 - 10 * gamma_ramp_component_;  // red is bits 20:29
          gamma_ramp_[entry] = (gamma_ramp_[entry] & ~(0x3FFu << shift)) | (color << shift);
        }
        if (++gamma_ramp_component_ >= 3) {
          gamma_ramp_component_ = 0;
          registers_[rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX] =
              (registers_[rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX] & ~0xFFu) |
              ((entry + 1) & 0xFF);
        }
        return;
      }
      case rex::graphics::XE_GPU_REG_DC_LUT_30_COLOR: {
        uint32_t entry = registers_[rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX] & 0xFF;
        uint32_t enable = registers_[rex::graphics::XE_GPU_REG_DC_LUT_WRITE_EN_MASK] & 0b111;
        uint32_t mask = ((enable & 0b001) ? 0x3FFu : 0) | ((enable & 0b010) ? 0x3FFu << 10 : 0) |
                        ((enable & 0b100) ? 0x3FFu << 20 : 0);
        gamma_ramp_[entry] = (gamma_ramp_[entry] & ~mask) | (value & mask);
        return;
      }
      default:
        break;
    }
    if (index >= rex::graphics::XE_GPU_REG_SCRATCH_REG0 &&
        index <= rex::graphics::XE_GPU_REG_SCRATCH_REG7) {
      const uint32_t n = index - rex::graphics::XE_GPU_REG_SCRATCH_REG0;
      if ((UINT32_C(1) << n) & registers_[rex::graphics::XE_GPU_REG_SCRATCH_UMSK]) {
        const uint32_t address = registers_[rex::graphics::XE_GPU_REG_SCRATCH_ADDR] + n * 4;
        rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(address), value);
      }
    }
  }

  void WriteMemory(uint32_t address, uint32_t value) {
    const uint32_t swapped = xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
    std::memcpy(memory_->TranslatePhysical(address & ~uint32_t(0x3)), &swapped, sizeof(swapped));
  }

  uint32_t ReadMemory(uint32_t address) {
    uint32_t value;
    std::memcpy(&value, memory_->TranslatePhysical(address & ~uint32_t(0x3)), sizeof(value));
    return xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
  }

  // --- Threads -------------------------------------------------------------------

  void Interrupt(uint32_t source, uint32_t cpu) {
    const uint32_t callback = callback_.load(std::memory_order_acquire);
    if (!callback || !dispatcher_) {
      return;
    }
    auto* thread = rex::system::XThread::GetCurrentThread();
    if (!thread) {
      return;
    }
    thread->SetActiveCpu(uint8_t(cpu));
    uint64_t args[] = {source, callback_data_.load(std::memory_order_acquire)};
    dispatcher_->ExecuteInterrupt(thread->thread_state(), callback, args, 2);
  }

  int VblankLoop() {
    const fear::display_vblank::Plan& plan = fear::display_vblank::GetPlan();
    if (display_pacing_ && DisplayVblankLoop(plan)) {
      return 0;
    }
    // F.E.A.R.: in place of the display's refreshes (fallen back from fear_vblank_source=display),
    // one every guest_vblanks_per_refresh of the timer's vblanks.
    const uint32_t per_refresh = display_pacing_ ? plan.guest_vblanks_per_refresh : 0;
    uint32_t in_refresh = 0;
    rex::system::X_VIDEO_MODE mode;
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    const double hz = std::max(1.0, double(float(mode.refresh_rate)));
    const auto interval =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
    // Not raised above the game's threads: the vblank interrupt runs the
    // game's handler, which takes a guest spinlock, and at a higher priority
    // this thread spun on it while the holder could not run on its core. The
    // vblanks stopped and the game never started another frame (black
    // screen).
    auto next = Clock::now() + interval;
    while (running_.load(std::memory_order_acquire)) {
      // To the deadline, not in 1 ms steps: those made every vblank up to a
      // millisecond or more late.
      rex::thread::SleepUntil(next);
      const auto now = Clock::now();
      // After a long pause (loading, a debugger) hundreds of vblanks are not
      // replayed.
      if (now - next > std::chrono::milliseconds(250)) {
        next = now;
      }
      while (now >= next) {
        if (per_refresh && in_refresh++ % per_refresh == 0) {
          DisplayRefresh();
        }
        GuestVblank();
        next += interval;
      }
    }
    return 0;
  }

  void GuestVblank() {
    vblanks_.fetch_add(1, std::memory_order_release);
    counter_.fetch_add(1, std::memory_order_relaxed);
    Interrupt(0, 2);
  }

  void DisplayRefresh() {
    {
      std::lock_guard<std::mutex> lock(display_mutex_);
      display_vblanks_.fetch_add(1, std::memory_order_release);
    }
    display_cv_.notify_all();
  }

  // F.E.A.R.: the guest's vblanks from the display's (fear_vblank_source=display, see
  // display_vblank.h). False, to go on with the timer, if there is no display to follow or its
  // vblanks stop coming at its refresh rate (variable refresh, or switched off).
  bool DisplayVblankLoop(const fear::display_vblank::Plan& plan) {
    if (fear::display_vblank::UnderWine()) {
      REXLOG_INFO("[native] Under Proton: pacing on a timer at the display's {} Hz (its "
                  "WaitForVBlank spins)",
                  plan.display_hz);
      return false;
    }
    fear::display_vblank::Waiter waiter;
    if (!waiter.Open()) {
      REXLOG_WARN("[native] No display vblank to follow; pacing on a timer instead");
      return false;
    }
    display_watched_ = true;
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / plan.display_hz));
    const uint32_t per_refresh = plan.guest_vblanks_per_refresh;
    auto last = Clock::now();
    auto window_start = last;
    uint32_t long_waits = 0;
    while (running_.load(std::memory_order_acquire)) {
      const auto wait_start = Clock::now();
      const bool waited = waiter.Wait();
      auto now = Clock::now();
      if (!waited || now - last < period / 2) {
        // A failed or spurious wait: the timer's pace this once.
        rex::thread::SleepUntil(last + period);
        now = Clock::now();
      } else if (now - wait_start > period * 3 / 2) {
        // The display let refreshes go by that it would make at its rate.
        if (now - window_start > std::chrono::seconds(2)) {
          window_start = now;
          long_waits = 0;
        }
        if (++long_waits >= 4) {
          REXLOG_WARN("[native] The display's vblanks are not coming at {} Hz (variable "
                      "refresh?); pacing on a timer instead",
                      plan.display_hz);
          display_watched_ = false;
          return false;
        }
      }
      // Refreshes missed while the game's vblank handler ran are made up, as the timer does,
      // but not after a long pause (loading, a debugger).
      int64_t refreshes = 1;
      if (now - last > period * 3 / 2 && now - last < std::chrono::milliseconds(250)) {
        refreshes = (now - last + period / 2) / period;
      }
      last = now;
      for (int64_t i = 0; i < refreshes; ++i) {
        DisplayRefresh();
        GuestVblank();
      }
      // With two guest vblanks a refresh, the second half a refresh later.
      for (uint32_t i = 1; i < per_refresh && running_.load(std::memory_order_acquire); ++i) {
        rex::thread::SleepUntil(now + period * i / per_refresh);
        GuestVblank();
      }
    }
    return true;
  }

  // F.E.A.R.: with the timer driving the game, the display's refreshes are still counted, for the
  // pacing report (how many each frame was on screen for).
  void WatchDisplay() {
    fear::display_vblank::Waiter waiter;
    if (!waiter.Open()) {
      return;
    }
    display_watched_ = true;
    while (running_.load(std::memory_order_acquire)) {
      if (!waiter.Wait()) {
        display_watched_ = false;
        return;
      }
      display_vblanks_.fetch_add(1, std::memory_order_release);
    }
  }

  int RingLoop() {
#if defined(__SWITCH__)
    // The same core the emulated path's command processor is pinned to: on a
    // core of its own it is not migrated around the game's threads.
    {
      const int32_t core = REXCVAR_GET(switch_cp_core);
      if (core >= 0 && core <= 2) {
        const Result result = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
        REXLOG_INFO("[native] Ring thread on core {}: result {:08X}", core, uint32_t(result));
      }
    }
#endif
    uint32_t read = 0;
    uint32_t generation = 0;
    auto last_report = Clock::now();
    while (running_.load(std::memory_order_acquire)) {
      {
        const auto idle_start = Clock::now();
        std::unique_lock<std::mutex> lock(ring_mutex_);
        ring_cv_.wait_for(lock, std::chrono::milliseconds(4), [&] {
          return !running_.load(std::memory_order_acquire) ||
                 write_pointer_.load(std::memory_order_acquire) != read ||
                 ring_generation_.load(std::memory_order_acquire) != generation;
        });
        ring_idle_ns_ += uint64_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - idle_start)
                .count());
      }
      if (!running_.load(std::memory_order_acquire)) {
        break;
      }
      if (Clock::now() - last_report > std::chrono::seconds(10)) {
        last_report = Clock::now();
        Report();
      }
      const uint32_t gen = ring_generation_.load(std::memory_order_acquire);
      if (gen != generation) {
        generation = gen;
        read = 0;  // InitializeRingBuffer leaves the read pointer at zero
      }
      const uint32_t base = ring_base_.load(std::memory_order_acquire);
      const uint32_t words = ring_words_.load(std::memory_order_acquire);
      if (!base || !words) {
        continue;
      }
      const uint32_t written = write_pointer_.load(std::memory_order_acquire) & (words - 1);
      if (written == read) {
        continue;
      }
      Reader reader;
      reader.base = memory_->TranslatePhysical(base);
      reader.mask = words - 1;
      reader.pos = read;
      reader.end = written;
      while (reader.Pending()) {
        if (!Packet(reader, 0)) {
          // A partial packet: the rest arrives with the next CP_RB_WPTR.
          break;
        }
      }
      read = reader.pos;
      const uint32_t writeback = read_pointer_writeback_.load(std::memory_order_acquire);
      if (writeback) {
        rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(writeback), read);
      }
    }
    return 0;
  }

  void Report() {
    REXLOG_INFO("[native] last 10 s: {} packets, {} draws, {} copies, {} swaps, {} waits cut short",
                packets_ - reported_packets_, draws_ - reported_draws_, copies_ - reported_copies_,
                swaps_ - reported_swaps_, waits_cut_short_);
    reported_packets_ = packets_;
    reported_draws_ = draws_;
    reported_copies_ = copies_;
    reported_swaps_before_ = reported_swaps_;
    reported_swaps_ = swaps_;
    REXLOG_INFO("[native] the game waited for the GPU: {} waits, {:.1f} ms",
                waits_ - reported_waits_, double(wait_ns_ - reported_wait_ns_) / 1e6);
    reported_waits_ = waits_;
    reported_wait_ns_ = wait_ns_;
    slow_frames_logged_ = 0;
    if (pacing_frames_) {
      const double mean = pacing_ms_sum_ / double(pacing_frames_);
      const double deviation =
          std::sqrt(std::max(0.0, pacing_ms_squares_ / double(pacing_frames_) - mean * mean));
      std::string refreshes;
      for (uint32_t i = 0; i < 8; ++i) {
        refreshes += fmt::format("{}{}", i ? "/" : "", pacing_refreshes_[i]);
      }
      // With the timer driving the game under Proton, nothing counts the display's refreshes.
      const std::string shown =
          display_pacing_ || display_watched_
              ? fmt::format("{} refreshes between presents 0/1/2/3/4/5/6/7+: {}",
                            display_watched_ ? "display" : "timer's", refreshes)
              : std::string("display refreshes not counted under Proton");
      REXLOG_INFO(
          "[native] pacing ({}): {} frames {:.2f} ms apart (deviation {:.2f}, longest {:.1f}); {}",
          display_pacing_ ? "display vblank" : "timer", pacing_frames_, mean, deviation,
          pacing_ms_max_, shown);
      pacing_frames_ = 0;
      pacing_ms_sum_ = pacing_ms_squares_ = pacing_ms_max_ = 0;
      std::fill(std::begin(pacing_refreshes_), std::end(pacing_refreshes_), 0);
    }
    if (const double frames = double(tile_sums_.frames)) {
      REXLOG_INFO(
          "[native] tiles (fear_tile_stats), per frame over {} frames: draws {:.0f} run / {:.0f} "
          "unique (x{:.2f}), {:.0f} skipped outside their tile; register writes {:.0f} run / "
          "{:.0f} unique (x{:.2f}); {:.1f} tiles",
          tile_sums_.frames, tile_sums_.draws / frames, tile_sums_.unique_draws / frames,
          double(tile_sums_.draws) / double(std::max<uint64_t>(tile_sums_.unique_draws, 1)),
          tile_sums_.skipped / frames, tile_sums_.registers / frames,
          tile_sums_.unique_registers / frames,
          double(tile_sums_.registers) / double(std::max<uint64_t>(tile_sums_.unique_registers, 1)),
          tile_sums_.tiles / frames);
      tile_sums_ = {};
    }
  }

  // fear_tile_stats: the packets this frame by guest location, folded into the sums at the swap.
  // Read again at every swap; off, nothing is counted.
  bool tile_stats_ = false;
  std::unordered_set<const void*> tile_draws_, tile_registers_;
  std::unordered_set<uint64_t> tile_selects_;
  struct TileSums {
    uint64_t frames = 0, draws = 0, unique_draws = 0, skipped = 0, registers = 0,
             unique_registers = 0, tiles = 0;
  };
  TileSums tile_frame_, tile_sums_;

  // The guest location of the packet whose header `data` follows (data is just past it).
  static const void* PacketLocation(const Reader& data) {
    const uint32_t header = data.mask ? ((data.pos - 1) & data.mask) : data.pos - 1;
    return data.base + size_t(header) * 4;
  }

  void CountRegisters(const Reader& data, uint32_t count) {
    tile_frame_.registers += count;
    if (tile_registers_.insert(PacketLocation(data)).second) {
      tile_frame_.unique_registers += count;
    }
  }

  void FoldTileFrame() {
    if (tile_stats_) {
      tile_frame_.frames = 1;
      tile_frame_.unique_draws = tile_draws_.size();
      tile_frame_.tiles = tile_selects_.size();
      tile_sums_.frames += tile_frame_.frames;
      tile_sums_.draws += tile_frame_.draws;
      tile_sums_.unique_draws += tile_frame_.unique_draws;
      tile_sums_.skipped += tile_frame_.skipped;
      tile_sums_.registers += tile_frame_.registers;
      tile_sums_.unique_registers += tile_frame_.unique_registers;
      tile_sums_.tiles += tile_frame_.tiles;
    }
    tile_frame_ = {};
    tile_draws_.clear();
    tile_registers_.clear();
    tile_selects_.clear();
    tile_stats_ = REXCVAR_GET(fear_tile_stats);
  }
  uint64_t reported_swaps_before_ = 0;

  // --- PM4 packets -----------------------------------------------------------------

  // Consumes a whole packet, or leaves the position untouched if it is not
  // complete yet.
  bool Packet(Reader& reader, int depth) {
    const uint32_t packet = reader.Peek();
    uint32_t count = 0;
    switch (packet >> 30) {
      case 0:
        count = packet ? ((packet >> 16) & 0x3FFF) + 1 : 0;
        break;
      case 1:
        count = 2;
        break;
      case 2:
        count = 0;
        break;
      default:
        count = ((packet >> 16) & 0x3FFF) + 1;
        break;
    }
    if (reader.Pending() < count + 1) {
      return false;
    }
    reader.Advance(1);
    ++packets_;
    Reader data = reader;
    reader.Advance(count);
    if (!packet) {
      return true;
    }
    if (tile_stats_ && (packet >> 30) < 2) {
      CountRegisters(data, count);
    }
    switch (packet >> 30) {
      case 0: {
        const uint32_t index = packet & 0x7FFF;
        const bool one_register = (packet >> 15) & 0x1;
        if (one_register) {
          for (uint32_t i = 0; i < count; ++i) {
            WriteRegister(index, data.Read());
          }
        } else {
          WriteRegisters(index, count, data);
        }
        break;
      }
      case 1: {
        const uint32_t value1 = data.Read();
        const uint32_t value2 = data.Read();
        WriteRegister(packet & 0x7FF, value1);
        WriteRegister((packet >> 11) & 0x7FF, value2);
        break;
      }
      case 2:
        break;
      default:
        Type3Packet(data, packet, count, depth);
        break;
    }
    return true;
  }

  void Type3Packet(Reader& data, uint32_t packet, uint32_t words, int depth) {
    const uint32_t opcode = (packet >> 8) & 0x7F;
    const bool draw = opcode == xenos::PM4_DRAW_INDX || opcode == xenos::PM4_DRAW_INDX_2;
    if ((packet & 0x1) && (!(bin_select_ & bin_mask_) || opcode == xenos::PM4_XE_SWAP)) {
      if (tile_stats_ && draw) {
        ++tile_frame_.skipped;
      }
      return;  // predicate fails; predicated swaps are always dropped
    }
    if (tile_stats_ && draw) {
      ++tile_frame_.draws;
      tile_draws_.insert(PacketLocation(data));
      if (packet & 0x1) {
        tile_selects_.insert(bin_select_);
      }
    }
    switch (opcode) {
      case xenos::PM4_INTERRUPT: {
        if (words < 1) break;
        const uint32_t cpus = data.Read();
        if (swap_interrupt_pending_) {
          // The swap-complete interrupt: right after the present, which the
          // frame rate cap already held (PM4_XE_SWAP), or held here if the
          // cap is not applied before presenting.
          swap_interrupt_pending_ = false;
          if (!PaceBeforePresent()) PaceFrame();
        }
        for (uint32_t cpu = 0; cpu < 6; ++cpu) {
          if (cpus & (1u << cpu)) {
            Interrupt(1, cpu);
          }
        }
        break;
      }
      case xenos::PM4_XE_SWAP:
        // VdSwap writes "SWAP", the physical address of the frontbuffer and
        // its size.
        if (words >= 4) {
          data.Read();
          swap_frontbuffer_ = data.Read();
          swap_width_ = data.Read();
          swap_height_ = data.Read();
        }
        if (PaceBeforePresent()) PaceFrame();
        Present();
        swap_interrupt_pending_ = true;
        ReportSlowFrame();
        FoldTileFrame();
        break;
      case xenos::PM4_INDIRECT_BUFFER:
      case xenos::PM4_INDIRECT_BUFFER_PFD: {
        if (words < 2) break;
        const uint32_t address = data.Read() & 0x1FFFFFFF;
        const uint32_t length = data.Read() & 0xFFFFF;
        if (depth < kMaxIndirectDepth) {
          IndirectBuffer(address, length, depth + 1);
        }
        break;
      }
      case xenos::PM4_WAIT_REG_MEM: {
        if (words < 5) break;
        const uint32_t info = data.Read();
        const uint32_t poll = data.Read();
        const uint32_t reference = data.Read();
        const uint32_t mask = data.Read();
        const auto wait_start = Clock::now();
        const auto limit = wait_start + kWaitRegMemMax;
        bool waited = false;
        for (uint32_t attempt = 0;; ++attempt) {
          if (!(info & 0x10) && poll == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST &&
              (Register(poll) & UINT32_C(0x80000000))) {
            registers_[poll] = 0;  // MakeCoherent: no shared memory to synchronise
          }
          const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
          if (Compare(info, value & mask, reference)) {
            break;
          }
          if (Clock::now() >= limit || !running_.load(std::memory_order_acquire)) {
            if (waits_cut_short_++ == 0) {
              REXLOG_WARN("[native] WAIT_REG_MEM not met ({} {:08X} ref {:08X} mask {:08X})",
                          (info & 0x10) ? "memory" : "register", poll, reference, mask);
            }
            break;
          }
          waited = true;
          // The game usually releases what the ring waits for within tens of
          // microseconds; yielding first avoids oversleeping each wait.
          // F.E.A.R.: then a real sleep. rex::thread::Sleep turns anything under 100 us into
          // a yield, so the 50 us "sleep" here was a yield too and the thread spun for the
          // whole wait (2-12 ms a frame): under Proton a syscall each, ~half a core on the
          // Steam Deck next to the game's thread (#28 profile). ShortSleep: a high-resolution
          // timer, or under Proton a sleep with no wineserver round trip (#40).
          if (attempt < 16) {
            std::this_thread::yield();
          } else {
            fear::display_vblank::ShortSleep(std::chrono::microseconds(200));
          }
        }
        if (waited) {
          ++waits_;
          wait_ns_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   Clock::now() - wait_start)
                                   .count());
        }
        break;
      }
      case xenos::PM4_REG_RMW: {
        if (words < 3) break;
        const uint32_t info = data.Read();
        const uint32_t and_value = data.Read();
        const uint32_t or_value = data.Read();
        uint32_t value = Register(info & 0x1FFF);
        value &= ((info >> 31) & 0x1) ? Register(and_value & 0x1FFF) : and_value;
        value |= ((info >> 30) & 0x1) ? Register(or_value & 0x1FFF) : or_value;
        WriteRegister(info & 0x1FFF, value);
        break;
      }
      case xenos::PM4_REG_TO_MEM: {
        if (words < 2) break;
        const uint32_t reg = data.Read();
        const uint32_t address = data.Read();
        WriteMemory(address, Register(reg));
        break;
      }
      case xenos::PM4_MEM_WRITE: {
        if (words < 1) break;
        uint32_t address = data.Read();
        for (uint32_t i = 1; i < words; ++i) {
          WriteMemory(address, data.Read());
          address += 4;
        }
        break;
      }
      case xenos::PM4_COND_WRITE: {
        if (words < 6) break;
        const uint32_t info = data.Read();
        const uint32_t poll = data.Read();
        const uint32_t reference = data.Read();
        const uint32_t mask = data.Read();
        const uint32_t destination = data.Read();
        const uint32_t value_to_write = data.Read();
        const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
        if (Compare(info, value & mask, reference)) {
          if (info & 0x100) {
            WriteMemory(destination, value_to_write);
          } else {
            WriteRegister(destination, value_to_write);
          }
        }
        break;
      }
      case xenos::PM4_EVENT_WRITE:
        if (words < 1) break;
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
        break;
      case xenos::PM4_EVENT_WRITE_SHD: {
        if (words < 3) break;
        const uint32_t initiator = data.Read();
        const uint32_t address = data.Read();
        const uint32_t value = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
        WriteMemory(address, ((initiator >> 31) & 0x1) ? counter_.load(std::memory_order_relaxed)
                                                       : value);
        break;
      }
      case xenos::PM4_EVENT_WRITE_EXT: {
        if (words < 2) break;
        const uint32_t initiator = data.Read();
        const uint32_t address = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
        // The screen extent of the previous draw: the maximum is faked.
        const uint16_t extent[] = {0, kMaxExtent, 0, kMaxExtent, 0, 1};
        uint8_t* destination = memory_->TranslatePhysical(address & ~uint32_t(0x3));
        for (size_t i = 0; i < std::size(extent); ++i) {
          rex::memory::store_and_swap<uint16_t>(destination + i * 2, extent[i]);
        }
        break;
      }
      case xenos::PM4_EVENT_WRITE_ZPD: {
        if (words < 1) break;
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
        const uint32_t address = Register(rex::graphics::XE_GPU_REG_RB_SAMPLE_COUNT_ADDR);
        if (address) {
          FakeOcclusionResult(address);
        }
        break;
      }
      case xenos::PM4_SET_CONSTANT: {
        if (words < 1) break;
        const uint32_t type_index = data.Read();
        const uint32_t base = ConstantBase(type_index);
        if (base != UINT32_MAX) {
          const uint32_t index = base + (type_index & 0x7FF);
          WriteRegisters(index, words - 1, data);
        }
        break;
      }
      case xenos::PM4_SET_CONSTANT2:
      case xenos::PM4_SET_SHADER_CONSTANTS: {
        // Same format: the first value is the register index, the rest are
        // written consecutively.
        if (words < 1) break;
        const uint32_t index = data.Read() & 0xFFFF;
        WriteRegisters(index, words - 1, data);
        break;
      }
      case xenos::PM4_LOAD_ALU_CONSTANT: {
        // Registers loaded from guest memory instead of from the ring.
        if (words < 3) break;
        const uint32_t address = data.Read() & 0x3FFFFFFF;
        const uint32_t type_index = data.Read();
        const uint32_t amount = data.Read() & 0xFFF;
        const uint32_t base = ConstantBase(type_index);
        if (base != UINT32_MAX) {
          const uint8_t* source = memory_->TranslatePhysical(address);
          const uint32_t index = base + (type_index & 0x7FF);
          for (uint32_t i = 0; i < amount; ++i) {
            WriteRegister(index + i, rex::memory::load_and_swap<uint32_t>(source + size_t(i) * 4));
          }
        }
        break;
      }
      case xenos::PM4_SET_BIN_MASK_LO:
        if (words < 1) break;
        bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | data.Read();
        break;
      case xenos::PM4_SET_BIN_MASK_HI:
        if (words < 1) break;
        bin_mask_ = (bin_mask_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
        break;
      case xenos::PM4_SET_BIN_SELECT_LO:
        if (words < 1) break;
        bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | data.Read();
        break;
      case xenos::PM4_SET_BIN_SELECT_HI:
        if (words < 1) break;
        bin_select_ = (bin_select_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
        break;
      case xenos::PM4_SET_BIN_MASK: {
        if (words < 2) break;
        const uint64_t high = data.Read();
        const uint64_t low = data.Read();
        bin_mask_ = (high << 32) | low;
        break;
      }
      case xenos::PM4_SET_BIN_SELECT: {
        if (words < 2) break;
        const uint64_t high = data.Read();
        const uint64_t low = data.Read();
        bin_select_ = (high << 32) | low;
        break;
      }
      case xenos::PM4_VIZ_QUERY: {
        // Like the emulation: when the query ends it is reported visible, in
        // case the game reads the result.
        if (words < 1) break;
        const uint32_t value = data.Read();
        const uint32_t id = value & 0x3F;
        if (!(value & 0x100)) {
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_START);
        } else {
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_END);
          const uint32_t reg = id < 32 ? uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0)
                                       : uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1);
          registers_[reg] |= UINT32_C(1) << (id < 32 ? id : id - 32);
        }
        break;
      }
      case xenos::PM4_DRAW_INDX:
      case xenos::PM4_DRAW_INDX_2: {
        // DRAW_INDX starts with the visibility query word.
        const uint32_t skip = opcode == xenos::PM4_DRAW_INDX ? 1 : 0;
        if (words < skip + 1) break;
        if (skip) {
          data.Read();
        }
        const uint32_t initiator = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator);
        // Indices in a buffer (DMA): its base and size come in the packet and
        // are stored in their registers, as ExecutePacketType3Draw does.
        if (((initiator >> 6) & 0x3) == uint32_t(xenos::SourceSelect::kDMA)) {
          if (words < skip + 3) break;
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_BASE, data.Read());
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_SIZE, data.Read());
        }
        // Indices in the packet itself.
        const uint32_t* immediate = nullptr;
        uint32_t immediate_words = 0;
        if (((initiator >> 6) & 0x3) == uint32_t(xenos::SourceSelect::kImmediate)) {
          immediate_words = words - skip - 1;
          immediate_indices_.resize(immediate_words);
          for (uint32_t i = 0; i < immediate_words; ++i) {
            immediate_indices_[i] = data.Read();
          }
          immediate = immediate_indices_.data();
        }
        // A draw in copy mode is a resolve and/or a clear.
        if ((Register(rex::graphics::XE_GPU_REG_RB_MODECONTROL) & 0x7) ==
            uint32_t(xenos::EdramMode::kCopy)) {
          ++copies_;
        } else {
          ++draws_;
        }
        break;
      }
      default:
        // Cache invalidations and the rest: nothing to do.
        break;
    }
  }

  static uint32_t ConstantBase(uint32_t type_index) {
    switch ((type_index >> 16) & 0xFF) {
      case 0:
        return 0x4000;  // ALU
      case 1:
        return 0x4800;  // fetch
      case 2:
        return 0x4900;  // bool
      case 3:
        return 0x4908;  // loop
      case 4:
        return 0x2000;  // registers
      default:
        return UINT32_MAX;
    }
  }

  // Match the SDK fallback: honor its configurable fake sample count, and
  // only replace the query result when the guest marks the query complete.
  void FakeOcclusionResult(uint32_t address) {
    const int32_t fake_sample_count = REXCVAR_GET(query_occlusion_fake_sample_count);
    if (fake_sample_count < 0) return;
    auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(address);
    const uint32_t kFinished = rex::byte_swap(UINT32_C(0xFFFFFEED));
    const bool end = counts->ZPass_A == kFinished || counts->ZPass_B == kFinished ||
                     counts->ZFail_A == kFinished || counts->ZFail_B == kFinished;
    std::memset(counts, 0, sizeof(*counts));
    if (end) {
      counts->ZPass_A = uint32_t(fake_sample_count);
      counts->Total_A = uint32_t(fake_sample_count);
    }
  }

  void IndirectBuffer(uint32_t address, uint32_t words, int depth) {
    Reader reader;
    reader.base = memory_->TranslatePhysical(address);
    reader.pos = 0;
    reader.end = words;
    while (reader.Pending()) {
      if (!Packet(reader, depth)) {
        break;
      }
    }
  }

  // --- Output presentation -----------------------------------------------------------

  // A frame longer than 50 ms on the ring thread, broken down, at most 12
  // per report period: the drops left are single long frames, which the 10 s
  // averages hide.
  void ReportSlowFrame() {
    const auto now = Clock::now();
    const uint64_t game_wait_ns = wait_ns_ - frame_start_wait_ns_;
    frame_start_wait_ns_ = wait_ns_;
    const uint64_t idle_ns = ring_idle_ns_ - frame_start_idle_ns_;
    frame_start_idle_ns_ = ring_idle_ns_;
    const uint64_t hold_ns = hold_ns_ - frame_start_hold_ns_;
    frame_start_hold_ns_ = hold_ns_;
    const auto previous = frame_start_;
    frame_start_ = now;
    if (previous.time_since_epoch().count() == 0) return;
    const double ms =
        double(std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count()) /
        1000.0;
    if (ms < 50.0 || slow_frames_logged_ >= 12) return;
    ++slow_frames_logged_;
    REXLOG_INFO(
        "[native] slow frame {:.0f} ms: ring idle {:.1f} ms, pacing hold {:.1f} ms",
        ms, double(idle_ns) / 1e6, double(hold_ns) / 1e6);
    REXLOG_INFO("[native] slow frame {:.0f} ms: waited for the game {:.1f} ms", ms,
                double(game_wait_ns) / 1e6);
  }

  // The frame rate cap (nx_min_frame_vblanks): a frame is presented at a
  // vblank at least that many after the previous present, or at once if it is
  // later than that. The hold used to come after the present, before the
  // swap-complete interrupt: an early frame went on screen at once and the
  // next one, if a little over budget, 50 ms later, a short gap then a long
  // one, felt as a hitch even at 30 fps on average. Held before it, presents
  // are evenly spaced, and the interrupt still follows the present.
  void PaceFrame() {
    if (display_pacing_) {
      PaceToDisplay();
      return;
    }
    const uint32_t min_vblanks = REXCVAR_GET(nx_min_frame_vblanks);
    if (min_vblanks > 1 && last_swap_vblank_) {
      const uint64_t target = last_swap_vblank_ + min_vblanks;
      const auto hold_start = Clock::now();
      const auto limit = hold_start + std::chrono::milliseconds(100);
      while (vblanks_.load(std::memory_order_acquire) < target &&
             running_.load(std::memory_order_acquire) && Clock::now() < limit) {
        rex::thread::Sleep(std::chrono::microseconds(250));
      }
      hold_ns_ += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - hold_start)
              .count());
    }
    last_swap_vblank_ = vblanks_.load(std::memory_order_acquire);
  }

  bool PaceBeforePresent() const {
    return display_pacing_ || REXCVAR_GET(nx_pace_before_present);
  }

  // F.E.A.R. (fear_vblank_source=display): a frame is presented right after the display refresh
  // that completes its refreshes_per_frame since the last, or at once if it took longer, so with
  // mailbox presentation each is shown at the next refresh and stays up for exactly that many.
  void PaceToDisplay() {
    const uint64_t target =
        last_present_refresh_ + fear::display_vblank::GetPlan().refreshes_per_frame;
    if (last_present_refresh_ && display_vblanks_.load(std::memory_order_acquire) < target) {
      const auto hold_start = Clock::now();
      {
        std::unique_lock<std::mutex> lock(display_mutex_);
        display_cv_.wait_until(lock, hold_start + std::chrono::milliseconds(100), [&] {
          return display_vblanks_.load(std::memory_order_acquire) >= target ||
                 !running_.load(std::memory_order_acquire);
        });
      }
      hold_ns_ += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - hold_start)
              .count());
    }
    last_present_refresh_ = display_vblanks_.load(std::memory_order_acquire);
  }

  // F.E.A.R.: for the report, the time between presents and the display refreshes between them
  // (with mailbox presentation, how many each frame was on screen for).
  void RecordPacing() {
    const auto now = Clock::now();
    const uint64_t refresh = display_vblanks_.load(std::memory_order_acquire);
    if (pacing_last_present_.time_since_epoch().count() != 0) {
      const double ms =
          std::chrono::duration<double, std::milli>(now - pacing_last_present_).count();
      if (ms < 250.0) {  // not across loading
        ++pacing_frames_;
        pacing_ms_sum_ += ms;
        pacing_ms_squares_ += ms * ms;
        pacing_ms_max_ = std::max(pacing_ms_max_, ms);
        ++pacing_refreshes_[std::min<uint64_t>(refresh - pacing_last_refresh_, 7)];
      }
    }
    pacing_last_present_ = now;
    pacing_last_refresh_ = refresh;
  }

  void DumpImage() {
    const std::string path = REXCVAR_GET(fear_dump_image);
    if (path.empty() || !memory_ || image_dumped_) return;
    image_dumped_ = true;
    const uint32_t kImageBase = PPCImageConfig.image_base, kImageSize = PPCImageConfig.image_size;
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
      REXLOG_WARN("[native] Could not write the image to {}", path);
      return;
    }
    std::fwrite(memory_->TranslateVirtual<const uint8_t*>(kImageBase), 1, kImageSize, file);
    std::fclose(file);
    REXLOG_INFO("[native] Wrote the image ({:08X}, {} bytes) to {}", kImageBase, kImageSize, path);
  }

  void Present() {
    RecordPacing();
    const uint64_t swap = ++swaps_;
    if (swap == 1) DumpImage();  // once: already done if the interrupt callback came first
    g_swaps_presented.fetch_add(1, std::memory_order_relaxed);
    counter_.fetch_add(1, std::memory_order_relaxed);
#if defined(__SWITCH__)
    RexSwitchPerfCount(0);
#endif
    // The D3D12 renderer handed this frame to the presenter at the game's Swap.
  }



  // --- State -------------------------------------------------------------------------

  rex::ui::WindowedAppContext* app_context_ = nullptr;
  std::unique_ptr<rex::ui::d3d12::D3D12Provider> provider_;
  std::unique_ptr<rex::ui::Presenter> presenter_;

  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;
  rex::memory::Memory* memory_ = nullptr;
  bool mmio_registered_ = false;

  std::atomic<bool> running_{false};
  rex::system::object_ref<rex::system::XHostThread> vblank_thread_;
  rex::system::object_ref<rex::system::XHostThread> ring_thread_;

  std::atomic<uint32_t> callback_{0};
  std::atomic<uint32_t> callback_data_{0};
  // Incremented by vblanks and swaps; EVENT_WRITE_SHD can write it (the SDK's
  // command processor counter).
  std::atomic<uint32_t> counter_{0};
  // Vblanks delivered, and the vblank of the last swap-complete interrupt.
  std::atomic<uint64_t> vblanks_{0};
  uint64_t last_swap_vblank_ = 0;
  // F.E.A.R.: the display's refreshes, which pace the frames with fear_vblank_source=display (the
  // timer's stand-ins after a fallback) and are otherwise counted for the report.
  bool display_pacing_ = false;
  bool image_dumped_ = false;  // fear_dump_image
  std::atomic<bool> display_watched_{false};
  std::atomic<uint64_t> display_vblanks_{0};
  std::mutex display_mutex_;
  std::condition_variable display_cv_;
  std::thread display_watch_;
  uint64_t last_present_refresh_ = 0;
  // Ring thread only: pacing since the last report.
  Clock::time_point pacing_last_present_{};
  uint64_t pacing_last_refresh_ = 0, pacing_frames_ = 0, pacing_refreshes_[8] = {};
  double pacing_ms_sum_ = 0, pacing_ms_squares_ = 0, pacing_ms_max_ = 0;
  Clock::time_point frame_start_{};
  uint64_t frame_start_wait_ns_ = 0;
  uint32_t slow_frames_logged_ = 0;
  uint64_t ring_idle_ns_ = 0, hold_ns_ = 0;
  uint64_t frame_start_idle_ns_ = 0, frame_start_hold_ns_ = 0;
  bool swap_interrupt_pending_ = false;

  std::atomic<uint32_t> ring_base_{0};
  std::atomic<uint32_t> ring_words_{0};
  std::atomic<uint32_t> ring_generation_{0};
  std::atomic<uint32_t> write_pointer_{0};
  std::atomic<uint32_t> read_pointer_writeback_{0};
  std::mutex ring_mutex_;
  std::condition_variable ring_cv_;

  // Touched by the ring thread and, through MMIO, by the game's threads; as in
  // the emulation, races on these integers are harmless.
  // The SDK's register file.
  std::unique_ptr<rex::graphics::RegisterFile> register_file_;
  uint32_t* registers_;
  // CommandProcessor's gamma ramp state (DC_LUT_*), 256-entry table only.
  uint32_t gamma_ramp_[256] = {};
  uint32_t gamma_ramp_component_ = 0;
  std::vector<uint32_t> immediate_indices_;
  uint64_t bin_mask_ = ~uint64_t(0);
  uint64_t bin_select_ = ~uint64_t(0);

  uint32_t swap_frontbuffer_ = 0;
  uint32_t swap_width_ = 0;
  uint32_t swap_height_ = 0;

  // Ring thread only.
  uint64_t packets_ = 0, draws_ = 0, copies_ = 0, swaps_ = 0, waits_cut_short_ = 0;
  uint64_t waits_ = 0, wait_ns_ = 0, reported_waits_ = 0, reported_wait_ns_ = 0;
  uint64_t reported_packets_ = 0, reported_draws_ = 0, reported_copies_ = 0,
           reported_swaps_ = 0;
};

}  // namespace

uint64_t SwapsPresented() { return g_swaps_presented.load(std::memory_order_relaxed); }

const uint32_t* GammaRamp256() { return g_gamma_ramp.load(std::memory_order_acquire); }

std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem() {
  return std::make_unique<NativeGraphicsSystem>();
}

}  // namespace fear_nx::native
