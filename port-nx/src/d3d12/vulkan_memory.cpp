// D3D12 renderer: memory for the Vulkan backend (vulkan_memory.h).
#include "vulkan_memory.h"

#include <rex/logging.h>
#include <rex/ui/vulkan/mem_alloc.h>
#include <rex/ui/vulkan/provider.h>

#include <algorithm>

namespace fear::d3d12 {

VulkanMemory::~VulkanMemory() {
  if (allocator_) vmaDestroyAllocator(allocator_);
}

bool VulkanMemory::Initialize(const VulkanDevice& device) {
  // The SDK's VMA (rex/ui/vulkan/mem_alloc.h), with the SDK device's functions; allocations come
  // from several threads (the guest thread's uploads, the texture workers).
  allocator_ = rex::ui::vulkan::CreateVmaAllocator(device.provider().vulkan_device(), false);
  if (!allocator_) {
    REXLOG_ERROR("D3D12 Vulkan: vmaCreateAllocator failed");
    return false;
  }
  return true;
}

bool VulkanMemory::CreateBuffer(uint64_t size, VkBufferUsageFlags usage, bool host_visible,
                                Buffer* out, const char* what) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo ai = {};
  ai.usage = VMA_MEMORY_USAGE_AUTO;
  if (host_visible)
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
  VmaAllocationInfo info = {};
  const VkResult r =
      vmaCreateBuffer(allocator_, &bi, &ai, &out->buffer, &out->allocation, &info);
  if (r != VK_SUCCESS) {
    REXLOG_ERROR("D3D12 Vulkan: {} ({} bytes) failed ({})", what, size, int(r));
    *out = Buffer{};
    return false;
  }
  out->mapped = static_cast<uint8_t*>(info.pMappedData);
  out->size = size;
  return true;
}

bool VulkanMemory::CreateReadbackBuffer(uint64_t size, Buffer* out, const char* what) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo ai = {};
  ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  VmaAllocationInfo info = {};
  const VkResult r =
      vmaCreateBuffer(allocator_, &bi, &ai, &out->buffer, &out->allocation, &info);
  if (r != VK_SUCCESS) {
    REXLOG_ERROR("D3D12 Vulkan: {} ({} bytes) failed ({})", what, size, int(r));
    *out = Buffer{};
    return false;
  }
  out->mapped = static_cast<uint8_t*>(info.pMappedData);
  out->size = size;
  return true;
}

void VulkanMemory::Invalidate(const Buffer& buffer) {
  if (buffer.allocation) vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

void VulkanMemory::Destroy(Buffer& buffer) {
  if (buffer.buffer) vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
  buffer = Buffer{};
}

bool VulkanMemory::CreateImage(const VkImageCreateInfo& info, Image* out, const char* what) {
  VmaAllocationCreateInfo ai = {};
  ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  const VkResult r = vmaCreateImage(allocator_, &info, &ai, &out->image, &out->allocation, nullptr);
  if (r != VK_SUCCESS) {
    REXLOG_ERROR("D3D12 Vulkan: {} failed ({})", what, int(r));
    *out = Image{};
    return false;
  }
  return true;
}

void VulkanMemory::Destroy(Image& image) {
  if (image.image) vmaDestroyImage(allocator_, image.image, image.allocation);
  image = Image{};
}

VulkanUploadPool::~VulkanUploadPool() {
  for (auto* list : {&active_, &retired_, &free_})
    for (auto& b : *list) Release(b);
}

bool VulkanUploadPool::Allocate(uint64_t size, uint64_t align, uint64_t fence, Alloc* out) {
  if (!current_ || ((current_->used + align - 1) & ~(align - 1)) + size > current_->size) {
    if (!NewBlock(std::max(size, block_size_), fence)) return false;
  }
  Block& b = *current_;
  b.used = (b.used + align - 1) & ~(align - 1);
  out->buffer = b.buffer.buffer;
  out->offset = b.used;
  out->cpu = b.buffer.mapped + b.used;
  b.used += size;
  b.fence = fence;
  return true;
}

void VulkanUploadPool::EndFrame(uint64_t completed) {
  for (auto& b : active_) retired_.push_back(std::move(b));
  active_.clear();
  current_ = nullptr;
  for (size_t i = 0; i < retired_.size();) {
    if (retired_[i]->fence <= completed) {
      retired_[i]->used = 0;
      if (retired_[i]->size == block_size_)
        free_.push_back(std::move(retired_[i]));
      else
        Release(retired_[i]);
      retired_[i] = std::move(retired_.back());
      retired_.pop_back();
    } else {
      ++i;
    }
  }
}

std::unique_ptr<VulkanUploadPool::Block> VulkanUploadPool::CreateBlock(uint64_t size) {
  auto b = std::make_unique<Block>();
  if (!memory_->CreateBuffer(size, usage_, true, &b->buffer, "upload block") || !b->buffer.mapped)
    return nullptr;
  b->size = size;
  return b;
}

void VulkanUploadPool::Release(std::unique_ptr<Block>& block) {
  if (block) memory_->Destroy(block->buffer);
  block.reset();
}

bool VulkanUploadPool::NewBlock(uint64_t size, uint64_t fence) {
  std::unique_ptr<Block> b;
  if (size == block_size_ && !free_.empty()) {
    b = std::move(free_.back());
    free_.pop_back();
  } else {
    b = CreateBlock(size);
    if (!b) return false;
  }
  b->used = 0;
  b->fence = fence;
  active_.push_back(std::move(b));
  current_ = active_.back().get();
  return true;
}

}  // namespace fear::d3d12
