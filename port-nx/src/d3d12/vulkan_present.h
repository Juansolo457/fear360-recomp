// D3D12 renderer: the Vulkan backend's present pass (GRAW's docs/VULKAN_RENDERER.md, M5),
// present.cpp's blit without HDR (shaders/present.vert, present.frag): the front buffer drawn
// into ReXGlue's presenter's guest output at its own size (OutputExtent; the presenter scales it
// to the window, as with D3D12), through a view with its fetch constant's swizzle, every texel
// through the display controller's gamma ramp (gamma_ramp.h). An output of another size would be
// fitted with the aspect kept, black bars and bilinear filtering.
// The ramp goes into a uniform buffer slot per frame in flight; one descriptor set per slot.
//
// Threading: the recording thread.
#pragma once

#include "vulkan_device.h"  // first: the Vulkan platform defines

#include "gamma_ramp.h"
#include "vulkan_memory.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace fear::d3d12 {

class VulkanPresenter final : public VulkanDevice::PresentPass {
 public:
  VulkanPresenter(VulkanDevice& device, VulkanMemory& memory) : device_(device), memory_(memory) {}
  ~VulkanPresenter();

  bool Initialize();

  // The next frame's image: `view` of an image in SHADER_READ_ONLY_OPTIMAL (2D array, as the
  // texture cache's views of resolve images), shown from texel (0, 0), `width` x `height`;
  // `ramp`: the gamma ramp it goes through (kRampNone: as it is). Null view: a black frame.
  // `frame`: the frame index (the ramp's log).
  void SetImage(VkImageView view, uint32_t width, uint32_t height, RampMode ramp, uint64_t frame);

  // The front buffer's size; before the first image, the 360's 1280x720.
  VkExtent2D OutputExtent() const override {
    return width_ && height_ ? VkExtent2D{width_, height_} : VkExtent2D{1280, 720};
  }
  void Record(VkCommandBuffer cmd, const VulkanDevice::BackBuffer& back, uint32_t slot) override;

 private:
  struct Constants {
    float image_size[2];
    uint32_t ramp_mode, point_sampled;
  };
  static constexpr uint32_t kRampBytes = kPwlWords * 4;  // the shader's uvec4 ramp[96]

  // The pipeline into back buffers of `format`, created on first use (kept until destroyed).
  VkPipeline Pipeline(VkFormat format);

  VulkanDevice& device_;
  VulkanMemory& memory_;
  GammaRamp gamma_;
  VulkanMemory::Buffer ramp_buffer_;  // kFramesInFlight slots of slot_bytes_
  uint32_t slot_bytes_ = 0;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, VulkanDevice::kFramesInFlight> sets_{};
  VkShaderModule vs_ = VK_NULL_HANDLE, ps_ = VK_NULL_HANDLE;
  std::vector<std::pair<VkFormat, VkPipeline>> pipelines_;

  VkImageView view_ = VK_NULL_HANDLE;
  uint32_t width_ = 0, height_ = 0;
  RampMode ramp_ = kRampNone;
  uint64_t frame_ = 0;
};

}  // namespace fear::d3d12
