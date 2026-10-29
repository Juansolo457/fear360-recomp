// D3D12 renderer: the Vulkan backend's device, queue and frames in flight (GRAW's
// docs/VULKAN_RENDERER.md, M2), on ReXGlue's Vulkan provider: the counterpart of d3d12_device.h.
//
// Like the D3D12 backend, it records on the device of ReXGlue's presenter (fear-nx's graphics
// system creates the Vulkan one with --fear_d3d12_api=vulkan, ../native/native_system.cpp), and
// EndFrame hands each frame to that presenter (RefreshGuestOutput), which keeps the window, the
// ImGui panels and the system dialogs. Every function used is loaded into Functions from the
// SDK's instance and device (VK_NO_PROTOTYPES). Requires Vulkan 1.3 with scalarBlockLayout, which
// the SDK's SPIR-V translator's shaders need, and dynamic rendering; the SDK's device enables
// both where supported, and textureCompressionBC and shaderStorageImageWriteWithoutFormat with
// patches/rexglue-sdk-nx/0018. The SDK's --vulkan_validation_enabled turns the Khronos
// validation layer on.
#pragma once

// The SDK's Vulkan configuration (VK_NO_PROTOTYPES, beta extensions, the platform's WSI): every
// file that includes Vulkan has to see the same one, and the SPIR-V translator's headers include
// it too.
#include <rex/ui/vulkan/api.h>
#if defined(_WIN32)
#undef DrawState  // winuser.h maps it to DrawStateA/W; d3d_state.h's struct has that name
#endif

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace fear::d3d12 {
class GpuTimer;
}

namespace rex::ui::vulkan {
class VulkanProvider;
}

namespace fear::d3d12 {

#define FEAR_VK_INSTANCE_FUNCTIONS(X)         \
  X(vkGetPhysicalDeviceProperties)            \
  X(vkGetPhysicalDeviceFormatProperties)      \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetDeviceProcAddr)

#define FEAR_VK_DEVICE_FUNCTIONS(X) \
  X(vkDeviceWaitIdle)               \
  X(vkCreateCommandPool)            \
  X(vkDestroyCommandPool)           \
  X(vkResetCommandPool)             \
  X(vkAllocateCommandBuffers)       \
  X(vkBeginCommandBuffer)           \
  X(vkEndCommandBuffer)             \
  X(vkCmdPipelineBarrier)           \
  X(vkCmdClearColorImage)           \
  X(vkCreateFence)                  \
  X(vkDestroyFence)                 \
  X(vkWaitForFences)                \
  X(vkResetFences)                  \
  X(vkQueueSubmit)                  \
  X(vkCreateShaderModule)           \
  X(vkDestroyShaderModule)          \
  X(vkCmdCopyBufferToImage)         \
  X(vkCmdCopyImageToBuffer)         \
  X(vkCmdCopyBuffer)                \
  X(vkCreateDescriptorSetLayout)    \
  X(vkDestroyDescriptorSetLayout)   \
  X(vkCreatePipelineLayout)         \
  X(vkDestroyPipelineLayout)        \
  X(vkCreateDescriptorPool)         \
  X(vkDestroyDescriptorPool)        \
  X(vkResetDescriptorPool)          \
  X(vkAllocateDescriptorSets)       \
  X(vkUpdateDescriptorSets)         \
  X(vkCmdBindDescriptorSets)        \
  X(vkCreateImageView)              \
  X(vkDestroyImageView)             \
  X(vkCreateSampler)                \
  X(vkDestroySampler)               \
  X(vkCreateGraphicsPipelines)      \
  X(vkDestroyPipeline)              \
  X(vkCmdBindPipeline)              \
  X(vkCmdBeginRendering)            \
  X(vkCmdEndRendering)              \
  X(vkCmdSetViewport)               \
  X(vkCmdSetScissor)                \
  X(vkCmdSetDepthBias)              \
  X(vkCmdSetBlendConstants)         \
  X(vkCmdSetStencilCompareMask)     \
  X(vkCmdSetStencilWriteMask)       \
  X(vkCmdSetStencilReference)       \
  X(vkCmdBindIndexBuffer)           \
  X(vkCmdDraw)                      \
  X(vkCmdDrawIndexed)               \
  X(vkCmdClearDepthStencilImage)    \
  X(vkCmdClearAttachments)          \
  X(vkCmdBlitImage)                 \
  X(vkCmdResolveImage)              \
  X(vkCmdCopyImage)                 \
  X(vkCreateComputePipelines)       \
  X(vkCmdDispatch)                  \
  X(vkCmdPushConstants)             \
  X(vkCreatePipelineCache)          \
  X(vkDestroyPipelineCache)         \
  X(vkGetPipelineCacheData)         \
  X(vkCreateQueryPool)              \
  X(vkDestroyQueryPool)             \
  X(vkCmdResetQueryPool)            \
  X(vkCmdWriteTimestamp)            \
  X(vkGetQueryPoolResults)

class VulkanDevice {
 public:
  static constexpr uint32_t kFramesInFlight = 2;

  struct Functions {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define FEAR_VK_DECLARE(name) PFN_##name name = nullptr;
    FEAR_VK_INSTANCE_FUNCTIONS(FEAR_VK_DECLARE)
    FEAR_VK_DEVICE_FUNCTIONS(FEAR_VK_DECLARE)
#undef FEAR_VK_DECLARE
    // VK_EXT_depth_bias_control's, where the SDK's device enabled it (else null).
    PFN_vkCmdSetDepthBias2EXT vkCmdSetDepthBias2EXT = nullptr;
  };

  // What the device has enabled among what the SPIR-V translator can use (its Features).
  struct ShaderCaps {
    uint32_t api_version = 0;
    uint32_t max_storage_buffer_range = 0;
    uint64_t min_uniform_buffer_offset_alignment = 256;
    bool geometry_shader = false;
    // M4: samplers and textures.
    bool sampler_anisotropy = false;
    float max_sampler_anisotropy = 1.0f;
    bool sampler_mirror_clamp_to_edge = false;
    bool texture_compression_bc = false;
    bool storage_image_write_without_format = false;  // the color resolve shader
    bool full_draw_index_uint32 = false;
    bool vertex_pipeline_stores_and_atomics = false;
    bool fragment_stores_and_atomics = false;
    bool clip_distance = false, cull_distance = false;
    bool sample_rate_shading = false;
    bool demote_to_helper_invocation = false;
    // Float controls (VkPhysicalDeviceVulkan12Properties) for 32-bit floats.
    bool signed_zero_inf_nan_preserve_float32 = false;
    bool denorm_flush_to_zero_float32 = false;
    bool rounding_mode_rte_float32 = false;
    // VK_EXT_depth_bias_control's depthBiasExact: the constant depth bias in exactly the depth
    // format's r, as D3D does, not up to twice that as Vulkan otherwise allows.
    bool depth_bias_exact = false;
    // ... and its floatRepresentation: a bias in depth units (r = 1).
    bool depth_bias_float = false;
  };

  // What the last EndFrame did (the frame CSV's swap columns).
  struct EndFrameTimes {
    bool presented = false;  // the frame went to ReXGlue's presenter
    uint64_t presents = 0;   // frames handed over so far, this one included
    uint64_t submit_ns = 0, present_ns = 0;
  };

  ~VulkanDevice();

  // The provider's instance, device and graphics queue, our frame objects. Logs and returns false
  // when the device lacks what the backend needs.
  bool Initialize(rex::ui::vulkan::VulkanProvider& provider);

  void WaitIdle() {
    if (device_) fn_.vkDeviceWaitIdle(device_);
  }

  // The frame's command buffer, open from the previous EndFrame (BeginFrame waited for its
  // frame-in-flight slot).
  VkCommandBuffer command_buffer() const { return frames_[slot_].cmd; }
  // Frames submitted so far (= the number the current frame signals), and the frames the GPU
  // finished.
  uint64_t frame_fence() const { return submitted_ + 1; }
  uint64_t completed_fence() const { return completed_; }

  // The frame-in-flight slot of the frame being recorded: what the slot's previous frame used
  // is free (its fence was waited for before the command buffer opened).
  uint32_t frame_slot() const { return slot_; }

  // The image the present pass draws: the presenter's guest output, in COLOR_ATTACHMENT_OPTIMAL
  // with undefined contents, `extent` as the pass asked (PresentPass::OutputExtent).
  struct BackBuffer {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
  };
  // What draws the frame's output, every pixel of it (in its own rendering instance).
  class PresentPass {
   public:
    // The output's size: the front buffer's, which the presenter scales to the window.
    virtual VkExtent2D OutputExtent() const = 0;
    virtual void Record(VkCommandBuffer cmd, const BackBuffer& back, uint32_t slot) = 0;

   protected:
    ~PresentPass() = default;
  };
  // Submits the frame's command buffer. With `present`, `pass` first draws the presenter's guest
  // output, and the submission happens inside the presenter's refresh, as it requires; a held or
  // refused frame is submitted alone, and the window keeps the last one. False on a device
  // failure.
  bool EndFrame(bool present, PresentPass& pass);
  const EndFrameTimes& end_frame_times() const { return times_; }

  // GPU time per frame (#91): a timestamp where each frame's command buffer starts and one
  // where it ends, read back once BeginSlot has waited for that frame's fence (never a stall).
  // The span runs from the start of the frame's first command to the end of its last, so it
  // includes any wait inside the frame for the GPU to free up, but not the idle time between
  // frames, which is kept apart. NaN when the queue has no timestamps.
  double gpu_last_frame_ms() const { return gpu_last_ms_; }
  // Collected frames since the last TakeGpuWindow: their count, GPU time (sum, max) and the
  // GPU's idle time between consecutive frames; `ambiguous`: frames left out because the host
  // saw them take longer than the timestamp counter's range (a whole wrap can't be told).
  struct GpuWindow {
    uint64_t frames = 0, ambiguous = 0;
    double sum_ms = 0.0, max_ms = 0.0, idle_ms = 0.0;
  };
  GpuWindow TakeGpuWindow() { return std::exchange(gpu_window_, {}); }

  // Timestamps per pass in every frame from now on (gpu_timer.h, --fear_d3d12_gpu_timing),
  // starting with the open command buffer: its own query pool, kMaxQueries per slot. False
  // without timestamps on the queue.
  bool EnableGpuTimer();
  GpuTimer* gpu_timer() const { return gpu_timer_.get(); }

  const Functions& fn() const { return fn_; }
  VkDevice device() const { return device_; }
  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_; }
  const ShaderCaps& shader_caps() const { return caps_; }
  rex::ui::vulkan::VulkanProvider& provider() const { return *provider_; }

 private:
  struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t number = 0;  // the frame it last carried (0: none)
    bool timed = false;   // its command buffer wrote both timestamps
    uint64_t recorded_ns = 0;  // host time when its first timestamp was recorded
  };
  // The finished frame's timestamps (queries 2 * slot and 2 * slot + 1) into gpu_last_ms_.
  void CollectGpuTime(uint32_t slot);
  bool BeginSlot();  // waits for the slot's previous frame and opens its command buffer
  // Ends and submits the slot's command buffer on queue 0 (under the SDK's queue lock).
  bool Submit(Frame& f);
  bool Check(VkResult r, const char* what);

  rex::ui::vulkan::VulkanProvider* provider_ = nullptr;
  Functions fn_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  ShaderCaps caps_;
  std::array<Frame, kFramesInFlight> frames_{};
  uint32_t slot_ = 0;
  uint64_t submitted_ = 0, completed_ = 0;
  EndFrameTimes times_;

  VkQueryPool timestamps_ = VK_NULL_HANDLE;  // two per frame-in-flight slot
  double tick_ns_ = 0.0;                     // the device's timestampPeriod
  uint64_t tick_mask_ = 0;                   // the queue's timestampValidBits
  double range_ns_ = 0.0;                    // the counter's whole range
  double gpu_last_ms_ = std::numeric_limits<double>::quiet_NaN();
  uint64_t gpu_last_end_ = 0;  // end tick of the frame collected last (0: none yet)
  uint64_t gpu_last_recorded_ns_ = 0;  // that frame's Frame::recorded_ns
  GpuWindow gpu_window_;
  std::unique_ptr<GpuTimer> gpu_timer_;
};

}  // namespace fear::d3d12
