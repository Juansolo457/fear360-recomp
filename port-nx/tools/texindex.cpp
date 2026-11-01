// fear_texindex: computes texture pack keys for the 360's textures, offline.
//
// Input: a bundle written by tools/texture_pack.py ("FTB1", count, then per texture the
// catalogue name and the decompressed .tex payload). Each payload is a 0x34-byte header with
// the Xenos texture fetch constant at 0x1C (big-endian) and then the texture data in the exact
// layout the GPU reads, so the base level's guest bytes are the payload bytes after the header.
//
// For every texture this computes, with the SDK's own code, the base level's guest extent
// (texture_util::GetGuestTextureLayout) and the pack key (fear::texture_pack::ReplacementKey,
// the function the renderer calls), and writes the base level untiled and endian-swapped into
// one linear blob (blocks in row order, as the renderer uploads them) for the quality gate.
//
//   fear_texindex <bundle> <out.tsv> <out_base.bin>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/math.h>

#include "texture_pack.h"

namespace xenos = rex::graphics::xenos;
namespace texture_util = rex::graphics::texture_util;
namespace texture_conversion = rex::graphics::texture_conversion;

namespace {

constexpr size_t kHeaderBytes = 0x34;
constexpr size_t kFetchOffset = 0x1C;

uint32_t ReadBE32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

bool ReadU32(std::ifstream& in, uint32_t& v) {
  return bool(in.read(reinterpret_cast<char*>(&v), 4));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: fear_texindex <bundle> <out.tsv> <out_base.bin>\n");
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  char magic[4];
  uint32_t count = 0;
  if (!in.read(magic, 4) || std::memcmp(magic, "FTB1", 4) || !ReadU32(in, count)) {
    std::fprintf(stderr, "not a texture bundle: %s\n", argv[1]);
    return 1;
  }
  std::ofstream tsv(argv[2]);
  std::ofstream blob(argv[3], std::ios::binary);
  tsv << "name\tkey\tformat\twidth\theight\tdimension\ttiled\tpitch\tpacked\tendian\tmip_max"
         "\tbase_size\tdata_size\tlinear_offset\tlinear_size\tblock_w\tblock_h\tblock_bytes"
         "\tstatus\n";
  uint64_t linear_offset = 0;
  uint32_t ok = 0, skipped = 0;
  std::vector<uint8_t> data, linear;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t name_len = 0, data_len = 0;
    if (!ReadU32(in, name_len)) break;
    std::string name(name_len, '\0');
    in.read(name.data(), name_len);
    ReadU32(in, data_len);
    data.resize(data_len);
    in.read(reinterpret_cast<char*>(data.data()), data_len);
    if (!in) break;

    auto skip = [&](const char* why) {
      tsv << name << "\t\t\t\t\t\t\t\t\t\t\t\t" << data_len << "\t\t\t\t\t\t" << why << "\n";
      ++skipped;
    };
    if (data_len < kHeaderBytes) {
      skip("short");
      continue;
    }
    xenos::xe_gpu_texture_fetch_t fetch;
    uint32_t words[6];
    for (uint32_t w = 0; w < 6; ++w) {
      words[w] = ReadBE32(data.data() + kFetchOffset + 4 * w);
    }
    static_assert(sizeof(fetch) == sizeof(words));
    std::memcpy(&fetch, words, sizeof(words));
    if (fetch.type != xenos::FetchConstantType::kTexture) {
      skip("not-a-texture-fetch");
      continue;
    }
    uint32_t width_minus_1, height_minus_1, depth_minus_1, base_page, mip_page, mip_max_level;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &width_minus_1, &height_minus_1,
                                                   &depth_minus_1, &base_page, &mip_page, nullptr,
                                                   &mip_max_level);
    const xenos::TextureFormat format = rex::graphics::GetBaseFormat(fetch.format);
    const uint32_t width = width_minus_1 + 1, height = height_minus_1 + 1;
    const uint32_t layers = depth_minus_1 + 1;
    const auto* info = rex::graphics::FormatInfo::Get(format);
    texture_util::TextureGuestLayout layout = texture_util::GetGuestTextureLayout(
        fetch.dimension, fetch.pitch, width, height, layers, fetch.tiled, format,
        fetch.packed_mips, true, mip_max_level);
    const uint32_t base_size = layout.base.level_data_extent_bytes;
    const uint8_t* base = data.data() + kHeaderBytes;
    const char* status = "ok";
    if (!base_size || kHeaderBytes + base_size > data_len) {
      status = "base-out-of-range";
    } else if (fetch.dimension != xenos::DataDimension::k2DOrStacked || layers != 1) {
      status = "not-2d";
    } else if (layout.packed_level == 0) {
      status = "packed-base";
    }
    uint64_t key = 0;
    if (base_size && kHeaderBytes + base_size <= data_len) {
      key = fear::texture_pack::ReplacementKey(base, base_size, uint32_t(format), width, height,
                                               fetch.tiled, fetch.pitch);
    }
    // The font atlases (interface\fonts\*.xpr) are linear in the file, but the game tiles them
    // when it loads them: the key is the tiled copy's, as the renderer sees it.
    std::string extension = name.size() > 4 ? name.substr(name.size() - 4) : "";
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool font = extension == ".xpr";
    if (font && !fetch.tiled && !std::strcmp(status, "ok")) {
      const texture_util::TextureGuestLayout tiled_layout = texture_util::GetGuestTextureLayout(
          fetch.dimension, fetch.pitch, width, height, layers, true, format, fetch.packed_mips,
          true, mip_max_level);
      const uint32_t bpb = info->bytes_per_block();
      const uint32_t bpb_log2 = rex::log2_floor(bpb);
      const uint32_t blocks_x = (width + info->block_width - 1) / info->block_width;
      const uint32_t blocks_y = (height + info->block_height - 1) / info->block_height;
      const uint32_t pitch_blocks = tiled_layout.base.row_pitch_bytes / bpb;
      std::vector<uint8_t> tiled(tiled_layout.base.level_data_extent_bytes, 0);
      bool in_range = true;
      for (uint32_t by = 0; by < blocks_y && in_range; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
          const uint64_t src = uint64_t(by) * layout.base.row_pitch_bytes + uint64_t(bx) * bpb;
          const uint64_t dst = uint64_t(
              texture_util::GetTiledOffset2D(int32_t(bx), int32_t(by), pitch_blocks, bpb_log2));
          if (src + bpb > base_size || dst + bpb > tiled.size()) {
            in_range = false;
            break;
          }
          std::memcpy(tiled.data() + dst, base + src, bpb);
        }
      }
      if (in_range) {
        key = fear::texture_pack::ReplacementKey(tiled.data(), uint32_t(tiled.size()),
                                                 uint32_t(format), width, height, true,
                                                 fetch.pitch);
      }
    }

    // The base level as the renderer uploads it: blocks in row order, endian-swapped.
    uint64_t linear_size = 0;
    const uint32_t bpb = info->bytes_per_block();
    if (!std::strcmp(status, "ok")) {
      const uint32_t blocks_x = (width + info->block_width - 1) / info->block_width;
      const uint32_t blocks_y = (height + info->block_height - 1) / info->block_height;
      const uint32_t bpb_log2 = rex::log2_floor(bpb);
      const uint32_t pitch_blocks = layout.base.row_pitch_bytes / bpb;
      linear.assign(size_t(blocks_x) * blocks_y * bpb, 0);
      bool in_range = true;
      for (uint32_t by = 0; by < blocks_y && in_range; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
          const uint64_t src = fetch.tiled
              ? uint64_t(texture_util::GetTiledOffset2D(int32_t(bx), int32_t(by), pitch_blocks,
                                                         bpb_log2))
              : uint64_t(by) * layout.base.row_pitch_bytes + uint64_t(bx) * bpb;
          if (src + bpb > base_size) {
            in_range = false;
            break;
          }
          texture_conversion::CopySwapBlock(fetch.endianness,
                                            linear.data() + (size_t(by) * blocks_x + bx) * bpb,
                                            base + src, bpb);
        }
      }
      if (in_range) {
        blob.write(reinterpret_cast<const char*>(linear.data()), std::streamsize(linear.size()));
        linear_size = linear.size();
      } else {
        status = "untile-out-of-range";
      }
    }
    char key_text[17];
    std::snprintf(key_text, sizeof(key_text), "%016llX", static_cast<unsigned long long>(key));
    tsv << name << '\t' << key_text << '\t' << uint32_t(format) << '\t' << width << '\t'
        << height << '\t' << uint32_t(fetch.dimension) << '\t' << uint32_t(fetch.tiled) << '\t'
        << uint32_t(fetch.pitch) << '\t' << uint32_t(fetch.packed_mips) << '\t'
        << uint32_t(fetch.endianness) << '\t' << mip_max_level << '\t' << base_size << '\t'
        << data_len << '\t' << (linear_size ? linear_offset : 0) << '\t' << linear_size << '\t'
        << info->block_width << '\t' << info->block_height << '\t' << bpb << '\t' << status
        << '\n';
    linear_offset += linear_size;
    ++ok;
  }
  std::printf("indexed %u textures (%u skipped), base blob %.1f MB\n", ok, skipped,
              double(linear_offset) / (1 << 20));
  return 0;
}
