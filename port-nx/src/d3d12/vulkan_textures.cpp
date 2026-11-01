// D3D12 renderer: the Vulkan backend's texture cache (vulkan_textures.h).
#include "vulkan_textures.h"

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <cstring>

#include "../texture_pack.h"

namespace fear::d3d12 {

namespace xenos = rex::graphics::xenos;
namespace texture_util = rex::graphics::texture_util;
using rex::graphics::FormatInfo;

namespace {

constexpr uint32_t kMemorySize = 512u << 20;
constexpr uint32_t kPageMask = 4095;
// The memory unwatches at most the 4 MB-aligned block around a write fault
// (PhysicalHeap::TriggerCallbacks).
constexpr uint32_t kUnwatchWindow = 4u << 20;

// A host component swizzle (3 bits per component: 0-3 a host channel, 4 zero, 5 one) from the
// fetch constant's guest swizzle and the host format's channel order, as D3D12's
// GuestToHostComponentMapping (guest 6 and 7 are 0 and 1 too).
uint32_t HostSwizzle(uint32_t guest_swizzle, uint32_t host_swizzle) {
  return GuestToHostComponentMapping(guest_swizzle, host_swizzle) & 0xFFF;
}

VkComponentSwizzle ComponentOf(uint32_t c) {
  switch (c) {
    case 0:
      return VK_COMPONENT_SWIZZLE_R;
    case 1:
      return VK_COMPONENT_SWIZZLE_G;
    case 2:
      return VK_COMPONENT_SWIZZLE_B;
    case 3:
      return VK_COMPONENT_SWIZZLE_A;
    case 4:
      return VK_COMPONENT_SWIZZLE_ZERO;
    default:
      return VK_COMPONENT_SWIZZLE_ONE;
  }
}

VkComponentMapping Mapping(uint32_t swizzle) {
  return {ComponentOf(swizzle & 7), ComponentOf((swizzle >> 3) & 7),
          ComponentOf((swizzle >> 6) & 7), ComponentOf((swizzle >> 9) & 7)};
}

}  // namespace

VkFormat VulkanFormatOf(DXGI_FORMAT f) {
  switch (f) {
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
      return VK_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R8_SNORM:
      return VK_FORMAT_R8_SNORM;
    case DXGI_FORMAT_B5G6R5_UNORM:  // B in bits 0-4: Vulkan's R5G6B5 has R in the top bits
      return VK_FORMAT_R5G6B5_UNORM_PACK16;
    // F.E.A.R.'s k_4_4_4_4 (texture_layout.cpp): A, R, G, B from the top nibble, as Vulkan 1.3's
    // A4R4G4B4 (core, but optional: where a GPU lacks it the texture fails to create, logged).
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      return VK_FORMAT_A4R4G4B4_UNORM_PACK16;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_SNORM:
      return VK_FORMAT_R8G8B8A8_SNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
      return VK_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R8G8_SNORM:
      return VK_FORMAT_R8G8_SNORM;
    case DXGI_FORMAT_BC1_UNORM:
      return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case DXGI_FORMAT_BC2_UNORM:
      return VK_FORMAT_BC2_UNORM_BLOCK;
    case DXGI_FORMAT_BC3_UNORM:
      return VK_FORMAT_BC3_UNORM_BLOCK;
    case DXGI_FORMAT_BC5_UNORM:
      return VK_FORMAT_BC5_UNORM_BLOCK;
    case DXGI_FORMAT_R32_FLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_UNORM:
      return VK_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16_SNORM:
      return VK_FORMAT_R16_SNORM;
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_UNORM:
      return VK_FORMAT_R16G16_UNORM;
    case DXGI_FORMAT_R16G16_SNORM:
      return VK_FORMAT_R16G16_SNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      return VK_FORMAT_R16G16B16A16_UNORM;
    case DXGI_FORMAT_R16G16B16A16_SNORM:
      return VK_FORMAT_R16G16B16A16_SNORM;
    case DXGI_FORMAT_R16_FLOAT:
      return VK_FORMAT_R16_SFLOAT;
    case DXGI_FORMAT_R16G16_FLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R32G32_FLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

VulkanTextureCache::VulkanTextureCache(VulkanDevice& device, VulkanMemory& memory,
                                       rex::memory::Memory* guest)
    : device_(device), memory_(memory), guest_(guest) {
  membase_ = guest_->physical_membase();
  upload_.SetMemory(&memory_);
  watch_buckets_.resize(kMemorySize >> kWatchBucketShift);
  invalidation_handle_ = guest_->RegisterPhysicalMemoryInvalidationCallback(InvalidationThunk, this);
}

VulkanTextureCache::~VulkanTextureCache() {
  if (invalidation_handle_) guest_->UnregisterPhysicalMemoryInvalidationCallback(invalidation_handle_);
  VkDevice d = device_.device();
  for (auto& [key, view] : resolved_views_) device_.fn().vkDestroyImageView(d, view, nullptr);
  for (const RetiredView& r : retired_views_) device_.fn().vkDestroyImageView(d, r.view, nullptr);
  for (RetiredImage& r : retired_images_) memory_.Destroy(r.image);
  for (auto& [key, t] : textures_) {
    for (auto& [vk, view] : t->views) device_.fn().vkDestroyImageView(d, view, nullptr);
    memory_.Destroy(t->image);
  }
}

std::pair<uint32_t, uint32_t> VulkanTextureCache::InvalidationThunk(void* context, uint32_t start,
                                                                    uint32_t length, bool exact) {
  return static_cast<VulkanTextureCache*>(context)->Invalidate(start, length, exact);
}

std::pair<uint32_t, uint32_t> VulkanTextureCache::Invalidate(uint32_t start, uint32_t length,
                                                             bool exact) {
  if (!length || start >= kMemorySize) return {start, length};
  const uint32_t end = start + std::min(length, kMemorySize - start);
  uint32_t lo = start, hi = end;
  std::lock_guard<std::mutex> lock(watch_mutex_);
  for (uint32_t b = start >> kWatchBucketShift; b <= (end - 1) >> kWatchBucketShift; ++b)
    for (const Watch& w : watch_buckets_[b])
      if (w.start < end && start < w.end) w.texture->dirty.store(true, std::memory_order_release);
  if (!exact) {
    // As textures.cpp: out to the nearest clean texture on each side. The pages of stale
    // textures need no watch (their next upload re-arms them), so a texture being rewritten
    // (a video's planes, every frame) faults once rather than once per 4 KB page. Any texture
    // overlapping [start, end) is stale now, and a watch lies in every bucket it touches, so
    // each scan stops at the first bucket holding a clean one.
    auto clean = [](const Watch& w) { return !w.texture->dirty.load(std::memory_order_acquire); };
    const uint32_t window_lo = start & ~(kUnwatchWindow - 1);
    const uint32_t window_hi = ((end - 1) | (kUnwatchWindow - 1)) + 1;
    lo = window_lo;
    for (uint32_t b = start >> kWatchBucketShift;; --b) {
      for (const Watch& w : watch_buckets_[b])
        if (w.end <= start && clean(w)) lo = std::max(lo, w.end);
      if (lo != window_lo || b == window_lo >> kWatchBucketShift) break;
    }
    hi = window_hi;
    for (uint32_t b = (end - 1) >> kWatchBucketShift;; ++b) {
      for (const Watch& w : watch_buckets_[b])
        if (w.start >= end && clean(w)) hi = std::min(hi, w.start);
      if (hi != window_hi || b == (window_hi - 1) >> kWatchBucketShift) break;
    }
    // The page holding a clean texture's last byte stays watched.
    lo = std::min(start, (lo + kPageMask) & ~kPageMask);
    hi = std::max(end, hi & ~kPageMask);
  }
  return {lo, hi - lo};
}

void VulkanTextureCache::WatchRange(Texture* t, uint32_t start, uint32_t length) {
  if (!length || start >= kMemorySize) return;
  const uint32_t end = start + std::min(length, kMemorySize - start);
  for (uint32_t b = start >> kWatchBucketShift; b <= (end - 1) >> kWatchBucketShift; ++b)
    watch_buckets_[b].push_back({start, end, t});
}

void VulkanTextureCache::ForgetResolvedImage(VkImage image, uint64_t frame) {
  for (auto it = resolved_views_.begin(); it != resolved_views_.end();) {
    if (it->first.image == image) {
      if (it->second) retired_views_.push_back({it->second, frame});
      it = resolved_views_.erase(it);
    } else {
      ++it;
    }
  }
}

bool VulkanTextureCache::MakeImage(Texture& t, const fear::texture_pack::Entry* pack,
                                   uint64_t frame) {
  const TextureKey& key = t.key;
  const VkFormat guest_unsigned = VulkanFormatOf(t.host->unorm);
  const VkFormat guest_signed = t.host->snorm == DXGI_FORMAT_UNKNOWN ? VK_FORMAT_UNDEFINED
                                                                     : VulkanFormatOf(t.host->snorm);
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  VkFormat format_signed = guest_signed;
  if (pack) {
    // In the 360 texture's own host format when that is the replacement's, its signed view
    // included (textures.cpp's PackHostFormat); otherwise the replacement's, unsigned only.
    ii.format = pack->format;
    if (pack->format != guest_unsigned) format_signed = VK_FORMAT_UNDEFINED;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = {pack->width, pack->height, 1};
    ii.mipLevels = pack->mip_count;
    ii.arrayLayers = 1;
  } else {
    ii.format = guest_unsigned;
    if (key.dimension == xenos::DataDimension::kCube)
      ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = key.is_3d() ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ii.extent = {key.width, key.height, key.depth()};
    ii.mipLevels = key.mip_levels();
    ii.arrayLayers = key.array_size();
  }
  if (ii.format == VK_FORMAT_UNDEFINED) return false;
  if (format_signed != VK_FORMAT_UNDEFINED && format_signed != ii.format)
    ii.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VulkanMemory::Image image;
  if (!memory_.CreateImage(ii, &image, pack ? "texture pack image" : "texture")) return false;
  // The frames up to this one may still read the old image through its views.
  if (t.image.image) {
    for (auto& [vk, view] : t.views)
      if (view) retired_views_.push_back({view, frame});
    t.views.clear();
    retired_images_.push_back({t.image, frame});
  }
  t.image = image;
  t.format_unsigned = ii.format;
  t.format_signed = format_signed;
  t.pack = pack;
  t.host_swizzle = pack && pack->swizzle != fear::texture_pack::kSameSwizzle
                       ? pack->swizzle
                       : t.host->host_swizzle;
  t.initialized = false;
  return true;
}

VulkanTextureCache::Texture* VulkanTextureCache::Create(const TextureKey& key,
                                                        const HostFormat* host) {
  auto t = std::make_unique<Texture>();
  t->key = key;
  t->host = host;
  if (!MakeImage(*t, nullptr, 0)) return nullptr;
  // The guest memory it is read from, watched from its first upload on.
  uint32_t base_bytes = 0, mips_bytes = 0;
  GetGuestExtents(key, &base_bytes, &mips_bytes);
  Texture* raw = t.get();
  {
    std::lock_guard<std::mutex> lock(watch_mutex_);
    WatchRange(raw, key.base_page << 12, base_bytes);
    WatchRange(raw, key.mip_page << 12, mips_bytes);
  }
  ++stats_.created;
  if (key.mip_levels() > 1) ++stats_.with_mips;
  textures_.emplace(key, std::move(t));
  return raw;
}

const fear::texture_pack::Entry* VulkanTextureCache::PackReplacement(const Texture& t,
                                                                     uint64_t* key) {
  namespace pack = fear::texture_pack;
  const bool log_keys = pack::LogKeys();
  if (!pack::Enabled() && !log_keys) return nullptr;
  const TextureKey& k = t.key;
  if (k.dimension != xenos::DataDimension::k2DOrStacked || k.array_size() != 1 || !k.base_page)
    return nullptr;
  // The base level's guest bytes, as the offline indexer (tools/texindex.cpp) hashes them.
  uint32_t base_bytes = 0, mips_bytes = 0;
  GetGuestExtents(k, &base_bytes, &mips_bytes);
  const uint32_t base = k.base_page << 12;
  if (!base_bytes || base >= kMemorySize || base_bytes > kMemorySize - base) return nullptr;
  // A resolve destination holds what the GPU drew, not a file's texels.
  if (resolved_ && resolved_(base).image) return nullptr;
  *key = pack::ReplacementKey(membase_ + base, base_bytes, uint32_t(k.format), k.width, k.height,
                              k.tiled, k.pitch);
  if (log_keys && keys_logged_ < 65536) {
    ++keys_logged_;
    REXLOG_INFO("[texpack] key={:016X} format={} size={}x{} tiled={} pitch={} base={} mips={} "
                "address={:08X}",
                *key, uint32_t(k.format), k.width, k.height, uint32_t(k.tiled), k.pitch,
                base_bytes, k.mip_levels(), base);
  }
  const pack::Entry* e = pack::Find(*key);
  if (pack::Enabled()) pack::CountLookup(e != nullptr);
  return e;
}

void VulkanTextureCache::RecordCopies(Texture& t, VkCommandBuffer cmd, VkBuffer buffer,
                                      const std::vector<VkBufferImageCopy>& regions) {
  const VulkanDevice::Functions& fn = device_.fn();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = t.image.image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                        VK_REMAINING_ARRAY_LAYERS};
  b.oldLayout = t.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  constexpr VkPipelineStageFlags kShaders =
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  fn.vkCmdPipelineBarrier(cmd, kShaders, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                          1, &b);
  fn.vkCmdCopyBufferToImage(cmd, buffer, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            uint32_t(regions.size()), regions.data());
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, kShaders, 0, 0, nullptr, 0, nullptr,
                          1, &b);
  t.initialized = true;
}

VulkanTextureCache::PackUpload VulkanTextureCache::UploadPack(Texture& t, VkCommandBuffer cmd,
                                                              uint64_t frame) {
  // The DDS mips, tight, level after level, each copied to an offset Vulkan takes (a multiple
  // of the block size and of 4).
  const fear::texture_pack::Entry& e = *t.pack;
  scratch_.resize(size_t(e.data_size));
  if (!fear::texture_pack::ReadData(e, scratch_.data(), scratch_.size()))
    return PackUpload::kUnreadable;
  struct Level {
    uint64_t src, dst, bytes;
    uint32_t width, height;
  };
  std::vector<Level> levels;
  uint64_t src = 0, total = 0;
  for (uint32_t level = 0; level < e.mip_count; ++level) {
    const uint32_t w = std::max(e.width >> level, 1u), h = std::max(e.height >> level, 1u);
    const uint64_t bytes = uint64_t((w + e.block_width - 1) / e.block_width) *
                           ((h + e.block_height - 1) / e.block_height) * e.block_bytes;
    total = (total + 15) & ~uint64_t(15);
    levels.push_back({src, total, bytes, w, h});
    src += bytes;
    total += bytes;
  }
  VulkanUploadPool::Alloc a;
  if (!upload_.Allocate(total, 16, frame, &a)) return PackUpload::kRetry;
  std::vector<VkBufferImageCopy> regions;
  regions.reserve(levels.size());
  for (uint32_t level = 0; level < levels.size(); ++level) {
    const Level& l = levels[level];
    std::memcpy(a.cpu + l.dst, scratch_.data() + l.src, size_t(l.bytes));
    VkBufferImageCopy r{};
    r.bufferOffset = a.offset + l.dst;
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    r.imageExtent = {l.width, l.height, 1};
    regions.push_back(r);
  }
  RecordCopies(t, cmd, a.buffer, regions);
  ++stats_.uploads;
  ++stats_.pack_uploads;
  stats_.upload_bytes += total;
  return PackUpload::kOk;
}

bool VulkanTextureCache::Upload(Texture& t, VkCommandBuffer cmd, uint64_t frame) {
  const TextureKey& key = t.key;
  const texture_util::TextureGuestLayout layout = GetGuestLayout(key);
  uint32_t base_bytes = 0, mips_bytes = 0;
  GetGuestExtents(key, layout, &base_bytes, &mips_bytes);
  const uint32_t base = key.base_page << 12, mips = key.mip_page << 12;
  // Watched before it is read: a write from here on makes it dirty again. Mips from the page
  // after the base's last: one protection change for both.
  t.dirty.store(false, std::memory_order_release);
  if (base_bytes && mips_bytes && mips == base + ((base_bytes + kPageMask) & ~kPageMask)) {
    guest_->EnablePhysicalMemoryAccessCallbacks(base, mips - base + mips_bytes, true, false);
  } else {
    if (base_bytes) guest_->EnablePhysicalMemoryAccessCallbacks(base, base_bytes, true, false);
    if (mips_bytes) guest_->EnablePhysicalMemoryAccessCallbacks(mips, mips_bytes, true, false);
  }
  // The texture pack (texture_pack.h): the new content may have the PC's copy, in an image of its
  // own size, format and mips; content without one goes back to an image for the guest's data.
  uint64_t pack_key = 0;
  const fear::texture_pack::Entry* pack = PackReplacement(t, &pack_key);
  if (pack != t.pack && !MakeImage(t, pack, frame)) {
    if (!pack) {
      t.dirty.store(true, std::memory_order_release);
      return false;
    }
    fear::texture_pack::MarkBad(pack_key);
    pack = nullptr;
    if (t.pack && !MakeImage(t, nullptr, frame)) {
      t.dirty.store(true, std::memory_order_release);
      return false;
    }
  }
  if (pack) {
    const PackUpload result = UploadPack(t, cmd, frame);
    if (result == PackUpload::kOk) return true;
    if (result == PackUpload::kRetry) {
      // No upload memory this frame: still dirty, so a later Get tries again.
      t.dirty.store(true, std::memory_order_release);
      return false;
    }
    // Unreadable: the 360's texture from now on.
    fear::texture_pack::MarkBad(pack_key);
    if (!MakeImage(t, nullptr, frame)) {
      t.dirty.store(true, std::memory_order_release);
      return false;
    }
  }
  if (!GetHostFootprints(key, false, &subresources_)) {
    t.dirty.store(true, std::memory_order_release);
    return false;
  }
  // Vulkan buffer copies: offsets aligned to the block size and 4 (16 covers both).
  uint64_t total = 0;
  for (HostSubresource& s : subresources_) {
    total = (total + 15) & ~uint64_t(15);
    s.offset = total;
    total += s.size();
  }
  VulkanUploadPool::Alloc a;
  // A failed upload leaves it dirty, so the next Get tries again instead of keeping the old
  // contents of an initialized image.
  if (!upload_.Allocate(total, 16, frame, &a)) {
    t.dirty.store(true, std::memory_order_release);
    return false;
  }
  GuestTextureMemory mem;
  if (base_bytes && base < kMemorySize) {
    mem.base = membase_ + base;
    mem.base_size = std::min<size_t>(base_bytes, kMemorySize - base);
  }
  if (mips_bytes && mips < kMemorySize) {
    mem.mips = membase_ + mips;
    mem.mips_size = std::min<size_t>(mips_bytes, kMemorySize - mips);
  }
  if (!UntileTexture(key, layout, mem, subresources_, a.cpu, &scratch_)) {
    t.dirty.store(true, std::memory_order_release);
    return false;
  }

  const FormatInfo* info = FormatInfo::Get(key.format);
  const uint32_t bw = t.host->block_compressed ? info->block_width : 1;
  const uint32_t bh = t.host->block_compressed ? info->block_height : 1;
  std::vector<VkBufferImageCopy> regions;
  regions.reserve(subresources_.size());
  for (const HostSubresource& s : subresources_) {
    VkBufferImageCopy r{};
    r.bufferOffset = a.offset + s.offset;
    r.bufferRowLength = s.block_columns * bw;
    r.bufferImageHeight = s.block_rows * bh;
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, s.level, key.is_3d() ? 0 : s.slice, 1};
    r.imageExtent = {s.width, s.height, s.depth};
    regions.push_back(r);
  }
  RecordCopies(t, cmd, a.buffer, regions);
  ++stats_.uploads;
  stats_.upload_bytes += total;
  return true;
}

VkImageView VulkanTextureCache::View(Texture& t, bool is_signed, uint32_t swizzle) {
  const uint32_t vk = (uint32_t(is_signed) << 12) | swizzle;
  auto it = t.views.find(vk);
  if (it != t.views.end()) return it->second;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = t.image.image;
  switch (t.key.dimension) {
    case xenos::DataDimension::k3D:
      vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case xenos::DataDimension::kCube:
      vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;
    default:
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      break;
  }
  vi.format = is_signed ? t.format_signed : t.format_unsigned;
  vi.components = Mapping(swizzle);
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                         VK_REMAINING_ARRAY_LAYERS};
  VkImageView view = VK_NULL_HANDLE;
  if (device_.fn().vkCreateImageView(device_.device(), &vi, nullptr, &view) != VK_SUCCESS)
    view = VK_NULL_HANDLE;
  t.views.emplace(vk, view);
  return view;
}

VkImageView VulkanTextureCache::ResolvedView(VkImage image, VkFormat format, uint32_t swizzle) {
  const ResolvedViewKey key{image, format, swizzle};
  auto it = resolved_views_.find(key);
  if (it != resolved_views_.end()) return it->second;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  vi.format = format;
  vi.components = Mapping(swizzle);
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view = VK_NULL_HANDLE;
  if (device_.fn().vkCreateImageView(device_.device(), &vi, nullptr, &view) != VK_SUCCESS)
    view = VK_NULL_HANDLE;
  resolved_views_.emplace(key, view);
  return view;
}

bool VulkanTextureCache::Get(const uint32_t fetch[6], VkCommandBuffer cmd, uint64_t frame,
                             const std::function<void()>& end_rendering,
                             VulkanTextureBinding* out) {
  TextureKey key;
  if (!TextureKeyFromFetch(fetch, &key)) return false;
  const HostFormat* host = GetHostFormat(key.format);
  if (!host || VulkanFormatOf(host->unorm) == VK_FORMAT_UNDEFINED) {
    ++stats_.unsupported;
    const uint32_t f = uint32_t(key.format) & 63;
    if (!(unsupported_formats_logged_ >> f & 1)) {
      unsupported_formats_logged_ |= uint64_t(1) << f;
      REXLOG_WARN("D3D12 Vulkan textures: format {} ({}) unsupported", f,
                  FormatInfo::Get(key.format)->name);
    }
    return false;
  }
  xenos::xe_gpu_texture_fetch_t f;
  std::memcpy(&f, fetch, sizeof(f));
  const uint32_t swizzle = HostSwizzle(f.swizzle, host->host_swizzle);
  out->dimension = key.dimension;
  out->swizzled_signs = texture_util::SwizzleSigns(f);
  // A resolve destination: bind the image the resolve made (guest memory doesn't have it).
  if (resolved_ && key.dimension == xenos::DataDimension::k2DOrStacked && key.array_size() == 1 &&
      key.mip_levels() == 1) {
    VulkanResolvedImage r = resolved_(key.base_page << 12);
    if (r.depth_image && VulkanFormatOf(host->unorm) == VK_FORMAT_R32_SFLOAT) {
      r.image = r.depth_image;
      r.format = VK_FORMAT_R32_SFLOAT;
      r.format_signed = VK_FORMAT_UNDEFINED;
    }
    if (r.image && r.width == key.width && r.height == key.height &&
        r.format == VulkanFormatOf(host->unorm)) {
      out->view_unsigned = ResolvedView(r.image, r.format, swizzle);
      // Float and depth formats: signed components read the unsigned view's values.
      const VkFormat sf = VulkanFormatOf(host->snorm);
      out->view_signed = sf == r.format ? out->view_unsigned
                         : sf != VK_FORMAT_UNDEFINED && sf == r.format_signed
                             ? ResolvedView(r.image, sf, swizzle)
                             : VK_NULL_HANDLE;
      ++stats_.resolved_binds;
      out->resolved = true;
      return out->view_unsigned != VK_NULL_HANDLE;
    }
    if (r.image) {
      // A resolve destination read in another format or size: the resolve's conversion (M5).
      ++stats_.resolved_mismatches;
      if (resolved_mismatches_logged_ < 8) {
        ++resolved_mismatches_logged_;
        REXLOG_WARN("D3D12 Vulkan textures: resolve image at {:08X} is {}x{} format {}, "
                    "read as {}x{} {} (format {})",
                    key.base_page << 12, r.width, r.height, int(r.format), key.width, key.height,
                    FormatInfo::Get(key.format)->name, int(VulkanFormatOf(host->unorm)));
      }
    }
  }
  auto it = textures_.find(key);
  Texture* t = it != textures_.end() ? it->second.get() : Create(key, host);
  if (!t) {
    ++stats_.failed;
    return false;
  }
  if (t->dirty.load(std::memory_order_acquire) || !t->initialized) {
    end_rendering();
    if (!Upload(*t, cmd, frame)) {
      ++stats_.failed;
      if (!t->initialized) return false;
    }
  }
  // A pack replacement may hold the guest's components in another channel order.
  const uint32_t texture_swizzle = t->pack ? HostSwizzle(f.swizzle, t->host_swizzle) : swizzle;
  out->view_unsigned = View(*t, false, texture_swizzle);
  out->view_signed = t->format_signed != VK_FORMAT_UNDEFINED ? View(*t, true, texture_swizzle)
                                                             : VK_NULL_HANDLE;
  return out->view_unsigned != VK_NULL_HANDLE;
}

void VulkanTextureCache::EndFrame(uint64_t completed) {
  upload_.EndFrame(completed);
  for (size_t i = 0; i < retired_views_.size();) {
    if (retired_views_[i].fence <= completed) {
      device_.fn().vkDestroyImageView(device_.device(), retired_views_[i].view, nullptr);
      retired_views_[i] = retired_views_.back();
      retired_views_.pop_back();
    } else {
      ++i;
    }
  }
  std::erase_if(retired_images_, [&](RetiredImage& r) {
    if (r.fence > completed) return false;
    memory_.Destroy(r.image);
    return true;
  });
  fear::texture_pack::ReportStats();
}

}  // namespace fear::d3d12
