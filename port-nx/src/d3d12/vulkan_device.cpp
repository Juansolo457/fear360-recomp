// D3D12 renderer: the Vulkan device (vulkan_device.h).
#include "vulkan_device.h"

#include "clock.h"

#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xvideo.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>

#include <algorithm>
#include <vector>

namespace fear::d3d12 {

bool VulkanDevice::Check(VkResult r, const char* what) {
  if (r == VK_SUCCESS) return true;
  REXLOG_ERROR("D3D12 Vulkan: {} failed ({})", what, int(r));
  return false;
}

VulkanDevice::~VulkanDevice() {
  if (!device_) return;
  fn_.vkDeviceWaitIdle(device_);
  for (Frame& f : frames_) {
    if (f.fence) fn_.vkDestroyFence(device_, f.fence, nullptr);
    if (f.pool) fn_.vkDestroyCommandPool(device_, f.pool, nullptr);
  }
  if (timestamps_) fn_.vkDestroyQueryPool(device_, timestamps_, nullptr);
}

bool VulkanDevice::Initialize(rex::ui::vulkan::VulkanProvider& provider) {
  provider_ = &provider;
  const rex::ui::vulkan::VulkanInstance& vi = *provider.vulkan_instance();
  const rex::ui::vulkan::VulkanDevice& vd = *provider.vulkan_device();
  instance_ = vi.instance();
  physical_ = vd.physical_device();
  device_ = vd.device();
  queue_family_ = vd.queue_family_graphics_compute();
  const rex::ui::vulkan::VulkanDevice::Properties& p = vd.properties();
  REXLOG_INFO("D3D12 Vulkan: {} (Vulkan {}.{}.{}, driver {:08X}), ReXGlue's device",
              p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
              VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
  if (p.apiVersion < VK_API_VERSION_1_3 || !p.scalarBlockLayout || !p.dynamicRendering) {
    REXLOG_ERROR("D3D12 Vulkan: the device lacks Vulkan 1.3, scalarBlockLayout or "
                 "dynamicRendering");
    return false;
  }
  fn_.vkGetInstanceProcAddr = vi.functions().vkGetInstanceProcAddr;
#define FEAR_VK_LOAD(name)                                                                     \
  fn_.name = reinterpret_cast<PFN_##name>(fn_.vkGetInstanceProcAddr(instance_, #name));       \
  if (!fn_.name) {                                                                             \
    REXLOG_ERROR("D3D12 Vulkan: {} missing", #name);                                          \
    return false;                                                                              \
  }
  FEAR_VK_INSTANCE_FUNCTIONS(FEAR_VK_LOAD)
#undef FEAR_VK_LOAD
#define FEAR_VK_LOAD(name)                                                                     \
  fn_.name = reinterpret_cast<PFN_##name>(fn_.vkGetDeviceProcAddr(device_, #name));           \
  if (!fn_.name) {                                                                             \
    REXLOG_ERROR("D3D12 Vulkan: {} missing", #name);                                          \
    return false;                                                                              \
  }
  FEAR_VK_DEVICE_FUNCTIONS(FEAR_VK_LOAD)
#undef FEAR_VK_LOAD
  // What the SPIR-V translator can use, as the SDK's device enabled it (its Properties hold the
  // enabled features).
  caps_.api_version = p.apiVersion;
  caps_.max_storage_buffer_range = p.maxStorageBufferRange;
  caps_.min_uniform_buffer_offset_alignment = p.minUniformBufferOffsetAlignment;
  caps_.full_draw_index_uint32 = p.fullDrawIndexUint32;
  caps_.geometry_shader = p.geometryShader;
  caps_.sampler_anisotropy = p.samplerAnisotropy;
  caps_.max_sampler_anisotropy = p.maxSamplerAnisotropy;
  caps_.sampler_mirror_clamp_to_edge = p.samplerMirrorClampToEdge;
  caps_.texture_compression_bc = p.textureCompressionBC;
  caps_.storage_image_write_without_format = p.shaderStorageImageWriteWithoutFormat;
  caps_.vertex_pipeline_stores_and_atomics = p.vertexPipelineStoresAndAtomics;
  caps_.fragment_stores_and_atomics = p.fragmentStoresAndAtomics;
  caps_.clip_distance = p.shaderClipDistance;
  caps_.cull_distance = p.shaderCullDistance;
  caps_.sample_rate_shading = p.sampleRateShading;
  caps_.demote_to_helper_invocation = p.shaderDemoteToHelperInvocation;
  caps_.signed_zero_inf_nan_preserve_float32 = p.shaderSignedZeroInfNanPreserveFloat32;
  caps_.denorm_flush_to_zero_float32 = p.shaderDenormFlushToZeroFloat32;
  caps_.rounding_mode_rte_float32 = p.shaderRoundingModeRTEFloat32;
  if (p.depthBiasControl && p.depthBiasExact) {
    fn_.vkCmdSetDepthBias2EXT = reinterpret_cast<PFN_vkCmdSetDepthBias2EXT>(
        fn_.vkGetDeviceProcAddr(device_, "vkCmdSetDepthBias2EXT"));
    caps_.depth_bias_exact = fn_.vkCmdSetDepthBias2EXT != nullptr;
    caps_.depth_bias_float = caps_.depth_bias_exact && p.depthBiasFloatRepresentation;
  }
  REXLOG_INFO(
      "D3D12 Vulkan: shader caps: clip {} cull {} vertex stores {} fragment stores {} "
      "sample rate {} demote {} | float32 signed-zero/inf/nan {} denorm-ftz {} rte {} | max "
      "storage buffer {} MB | BC {} storage write without format {} | exact depth bias {} (float {})",
      caps_.clip_distance, caps_.cull_distance, caps_.vertex_pipeline_stores_and_atomics,
      caps_.fragment_stores_and_atomics, caps_.sample_rate_shading,
      caps_.demote_to_helper_invocation, caps_.signed_zero_inf_nan_preserve_float32,
      caps_.denorm_flush_to_zero_float32, caps_.rounding_mode_rte_float32,
      caps_.max_storage_buffer_range >> 20, caps_.texture_compression_bc,
      caps_.storage_image_write_without_format, caps_.depth_bias_exact,
      caps_.depth_bias_float);
  if (!caps_.texture_compression_bc) {
    REXLOG_ERROR("D3D12 Vulkan: the device has no BC texture compression (the game's textures)");
    return false;
  }
  // Frame timestamps, where the queue has them.
  uint32_t families = 0;
  fn_.vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, nullptr);
  std::vector<VkQueueFamilyProperties> family(families);
  fn_.vkGetPhysicalDeviceQueueFamilyProperties(physical_, &families, family.data());
  const uint32_t valid_bits =
      queue_family_ < families ? family[queue_family_].timestampValidBits : 0;
  if (valid_bits && p.timestampPeriod > 0.0f) {
    VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qp.queryCount = 2 * kFramesInFlight;
    if (!Check(fn_.vkCreateQueryPool(device_, &qp, nullptr, &timestamps_), "vkCreateQueryPool"))
      return false;
    tick_ns_ = p.timestampPeriod;
    tick_mask_ = valid_bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << valid_bits) - 1;
  } else {
    REXLOG_WARN("D3D12 Vulkan: no GPU timestamps on this queue (frame GPU time unavailable)");
  }
  for (Frame& f : frames_) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = queue_family_;
    if (!Check(fn_.vkCreateCommandPool(device_, &pool, nullptr, &f.pool), "vkCreateCommandPool"))
      return false;
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = f.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (!Check(fn_.vkAllocateCommandBuffers(device_, &alloc, &f.cmd), "vkAllocateCommandBuffers"))
      return false;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!Check(fn_.vkCreateFence(device_, &fence, nullptr, &f.fence), "vkCreateFence"))
      return false;
  }
  return BeginSlot();
}

bool VulkanDevice::BeginSlot() {
  Frame& f = frames_[slot_];
  if (f.number) {
    if (!Check(fn_.vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
      return false;
    completed_ = std::max(completed_, f.number);
    fn_.vkResetFences(device_, 1, &f.fence);
    if (f.timed) CollectGpuTime(slot_);
  }
  fn_.vkResetCommandPool(device_, f.pool, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!Check(fn_.vkBeginCommandBuffer(f.cmd, &bi), "vkBeginCommandBuffer")) return false;
  f.timed = false;
  if (timestamps_) {
    fn_.vkCmdResetQueryPool(f.cmd, timestamps_, 2 * slot_, 2);
    fn_.vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_, 2 * slot_);
  }
  return true;
}

void VulkanDevice::CollectGpuTime(uint32_t slot) {
  frames_[slot].timed = false;
  uint64_t ticks[2];
  // The frame's fence was waited for, so both are available; without WAIT_BIT a result that
  // isn't (a driver that disagrees) is skipped, never waited for.
  if (fn_.vkGetQueryPoolResults(device_, timestamps_, 2 * slot, 2, sizeof(ticks), ticks,
                                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
    return;
  const uint64_t begin = ticks[0] & tick_mask_, end = ticks[1] & tick_mask_;
  // Masked, the counter may wrap between the two: the difference stays in range.
  const double ms = double((end - begin) & tick_mask_) * tick_ns_ / 1e6;
  gpu_last_ms_ = ms;
  GpuWindow& w = gpu_window_;
  ++w.frames;
  w.sum_ms += ms;
  w.max_ms = std::max(w.max_ms, ms);
  if (gpu_last_end_) w.idle_ms += double((begin - gpu_last_end_) & tick_mask_) * tick_ns_ / 1e6;
  gpu_last_end_ = end;
}

bool VulkanDevice::Submit(Frame& f) {
  if (timestamps_) {
    const uint32_t slot = uint32_t(&f - frames_.data());
    fn_.vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_,
                            2 * slot + 1);
    f.timed = true;
  }
  if (!Check(fn_.vkEndCommandBuffer(f.cmd), "vkEndCommandBuffer")) return false;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  // The presenter submits on the same queue, from the UI thread too.
  auto queue = provider_->vulkan_device()->AcquireQueue(queue_family_, 0);
  if (!Check(fn_.vkQueueSubmit(queue.queue(), 1, &si, f.fence), "vkQueueSubmit")) return false;
  f.number = ++submitted_;
  return true;
}

bool VulkanDevice::EndFrame(bool present, PresentPass& pass) {
  times_.presented = false;
  times_.submit_ns = times_.present_ns = 0;
  Frame& f = frames_[slot_];
  const uint64_t t0 = NowNs();
  bool submitted = false, failed = false;
  rex::Runtime* runtime = rex::Runtime::instance();
  rex::system::IGraphicsSystem* graphics = runtime ? runtime->graphics_system() : nullptr;
  // fear-nx's graphics system (../native/native_system.h) runs ReXGlue's Vulkan presenter.
  auto* presenter =
      graphics ? static_cast<rex::ui::vulkan::VulkanPresenter*>(graphics->presenter()) : nullptr;
  if (present && presenter) {
    using rex::ui::vulkan::VulkanPresenter;
    const VkExtent2D extent = pass.OutputExtent();
    rex::system::X_VIDEO_MODE mode;
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    const uint32_t aspect_x = std::max(1u, uint32_t(mode.display_width));
    const uint32_t aspect_y = std::max(1u, uint32_t(mode.display_height));
    presenter->RefreshGuestOutput(
        extent.width, extent.height, aspect_x, aspect_y,
        [&](rex::ui::Presenter::GuestOutputRefreshContext& context) {
          auto& vk_context =
              static_cast<VulkanPresenter::VulkanGuestOutputRefreshContext&>(context);
          VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b.image = vk_context.image();
          b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
          // Every pixel is drawn: the old contents go.
          b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
          b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          const bool written = vk_context.image_ever_written_previously();
          b.srcAccessMask = written ? VulkanPresenter::kGuestOutputInternalAccessMask : 0;
          b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          fn_.vkCmdPipelineBarrier(
              f.cmd,
              written ? VkPipelineStageFlags(VulkanPresenter::kGuestOutputInternalStageMask)
                      : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
          pass.Record(f.cmd,
                      {vk_context.image(), vk_context.image_view(),
                       VulkanPresenter::kGuestOutputFormat, extent},
                      slot_);
          b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          b.newLayout = VulkanPresenter::kGuestOutputInternalLayout;
          b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          b.dstAccessMask = VulkanPresenter::kGuestOutputInternalAccessMask;
          fn_.vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VulkanPresenter::kGuestOutputInternalStageMask, 0, 0, nullptr,
                                   0, nullptr, 1, &b);
          // 10 bits per channel: the gamma ramp's entries are.
          context.SetIs8bpc(false);
          // The presenter reads the image after this submission, on the same queue.
          submitted = true;
          failed = !Submit(f);
          return !failed;
        });
  }
  if (failed) return false;
  if (submitted) {
    times_.presented = true;
    ++times_.presents;
  } else if (!Submit(f)) {
    return false;
  }
  times_.submit_ns = NowNs() - t0;
  slot_ = (slot_ + 1) % kFramesInFlight;
  return BeginSlot();
}

}  // namespace fear::d3d12
