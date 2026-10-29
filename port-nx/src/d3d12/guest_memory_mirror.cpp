// D3D12 renderer: guest physical memory mirrored in the GPU's shared memory
// (guest_memory_mirror.h). Moved out of draw.cpp (DrawRenderer::Impl) unchanged.
#include "guest_memory_mirror.h"

#include "clock.h"
#include "log.h"

#include <rex/cvar.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>

#include <fmt/format.h>

#include <bit>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#undef DrawState
#endif

REXCVAR_DECLARE(bool, fear_vertex_index_range);
REXCVAR_DECLARE(int32_t, fear_d3d12_hot_exact_kb);

namespace fear::d3d12 {

bool GuestMemoryMirror::Initialize(rex::memory::Memory* memory_in, Sink* sink,
                                   const std::string& trace_path, bool coherency_check) {
  memory = memory_in;
  membase = memory->physical_membase();
  sink_ = sink;
  // Zeroed: make_unique value-initializes, and std::atomic's default constructor value-initializes
  // its value (C++20).
  valid = std::make_unique<std::atomic<uint64_t>[]>(kPageCount / 64);
  hot = std::make_unique<std::atomic<uint64_t>[]>(kPageCount / 64);
  armed_frame = std::make_unique<std::atomic<uint32_t>[]>(kPageCount);
  write_frame = std::make_unique<std::atomic<uint32_t>[]>(kPageCount);
  quick_rewrites = std::make_unique<std::atomic<uint8_t>[]>(kPageCount);
  copy_hash = std::make_unique<std::atomic<uint64_t>[]>(kPageCount);
  hot_hashes = std::make_unique<uint16_t[]>(kPageCount);
  exact_hash = std::make_unique<uint64_t[]>(kPageCount);
  pending = std::make_unique<std::atomic<uint64_t>[]>(kPageCount / 64);
  segment_word_epoch = std::make_unique<uint32_t[]>(kPageCount / 64);
  segment_pages = std::make_unique<uint64_t[]>(kPageCount / 64);
  if (!trace_path.empty()) trace.file = std::fopen(trace_path.c_str(), "wb");
  if (coherency_check) {
    coherency = std::make_unique<Coherency>();
    REXLOG_INFO("D3D12 draw: coherency check on");
  }
  invalidation_handle = memory->RegisterPhysicalMemoryInvalidationCallback(InvalidationThunk, this);
  arm_thread = std::thread([this] { WatchThread(); });
  index_ranges = REXCVAR_GET(fear_vertex_index_range);
  if (index_ranges) REXLOG_INFO("D3D12 draw: vertex ranges from the index bounds");
  hot_exact_bytes = uint32_t(std::max(0, REXCVAR_GET(fear_d3d12_hot_exact_kb))) << 10;
  if (hot_exact_bytes)
    REXLOG_INFO("D3D12 draw: ranges up to {} KB on hot pages copied to the byte",
                hot_exact_bytes >> 10);
#if defined(_WIN32)
  SetThreadDescription(arm_thread.native_handle(), L"D3D12 watches");
#endif
  return true;
}

void GuestMemoryMirror::Shutdown() {
  if (arm_thread.joinable()) {
    {
      std::lock_guard<std::mutex> lock(arm_mutex);
      arm_stopping = true;
    }
    arm_cv.notify_one();
    arm_thread.join();
  }
  if (invalidation_handle && memory) {
    memory->UnregisterPhysicalMemoryInvalidationCallback(invalidation_handle);
    invalidation_handle = nullptr;
  }
  if (trace.file) {
    trace.Flush();
    std::fclose(trace.file);
    trace.file = nullptr;
  }
}

bool GuestMemoryMirror::InSharedMemory(uint32_t start, uint32_t length) {
  if (start < kSharedMemorySize && kSharedMemorySize - start >= length) return true;
  D3D12_WARN_ONCE("D3D12 draw: range {:08X}+{:X} outside physical memory", start, length);
  return false;
}

bool GuestMemoryMirror::RequestVertices(const DrawState& s, const rex::graphics::Shader& vs,
                                        std::vector<VertexCopy>& out, bool fans_converted) {
  namespace xenos = rex::graphics::xenos;
  const auto& regs = *reinterpret_cast<const rex::graphics::RegisterFile*>(s.regs);
  const rex::graphics::Shader::ConstantRegisterMap& vmap = vs.constant_register_map();
  // With index_ranges, the bytes of each fetch constant the draw's vertices reach, for the
  // bindings whose fetches all take the vertex index the shader starts with (r0.x, not
  // rewritten before them, SDK patch 0025): vertex lo..hi, each vertex's attributes. Any other
  // binding of a fetch constant keeps it whole.
  struct Reach {
    uint32_t index;
    int64_t begin, end;  // bytes from the fetch constant's address
    bool whole;
  };
  Reach reach[8];
  uint32_t reach_count = 0;
  uint32_t lo = 0, hi = 0;
  if (index_ranges && vs.is_ucode_analyzed() && !vs.vertex_index_rewritten() &&
      IndexBounds(s, lo, hi)) {
    using rex::graphics::InstructionStorageAddressingMode;
    using rex::graphics::InstructionStorageSource;
    using rex::graphics::SwizzleSource;
    for (const rex::graphics::Shader::VertexBinding& b : vs.vertex_bindings()) {
      bool plain = true;
      int64_t first = 0, last = b.stride_words;  // dwords around one vertex's start
      for (const auto& a : b.attributes) {
        const auto& op = a.fetch_instr.operands[0];
        if (op.storage_source != InstructionStorageSource::kRegister || op.storage_index != 0 ||
            op.storage_addressing_mode != InstructionStorageAddressingMode::kAbsolute ||
            op.components[0] != SwizzleSource::kX ||
            a.fetch_instr.attributes.stride != b.stride_words) {
          plain = false;
          break;
        }
        const int64_t offset = a.fetch_instr.attributes.offset;
        const uint32_t words =
            xenos::GetVertexFormatNeededWords(a.fetch_instr.attributes.data_format, 0b1111);
        first = std::min(first, offset);
        last = std::max(last, offset + int64_t(std::bit_width(words)));
      }
      Reach* r = nullptr;
      for (uint32_t i = 0; i < reach_count; ++i)
        if (reach[i].index == b.fetch_constant) r = &reach[i];
      if (!r) {
        // Full: this constant stays whole; later bindings of recorded ones still merge.
        if (reach_count == std::size(reach)) continue;
        r = &reach[reach_count++];
        *r = {b.fetch_constant, INT64_MAX, INT64_MIN, false};
      }
      if (!plain) {
        r->whole = true;
        continue;
      }
      r->begin = std::min(r->begin, (int64_t(lo) * b.stride_words + first) * 4);
      r->end = std::max(r->end, (int64_t(hi) * b.stride_words + last) * 4);
    }
  }
  for (uint32_t w = 0; w < 3; ++w) {
    uint32_t bits = vmap.vertex_fetch_bitmap[w];
    while (bits) {
      uint32_t b = uint32_t(std::countr_zero(bits));
      bits &= bits - 1;
      const uint32_t index = w * 32 + b;
      xenos::xe_gpu_vertex_fetch_t f = regs.GetVertexFetch(index);
      if (f.type != xenos::FetchConstantType::kVertex) {
        D3D12_WARN_ONCE("D3D12 draw: vertex fetch {} has type {} ({:08X} {:08X})", index,
                       uint32_t(f.type), f.dword_0, f.dword_1);
        if (f.type != xenos::FetchConstantType::kInvalidVertex) return false;
      }
      uint32_t start = f.address << 2, size = f.size << 2;
      const bool inline_data = s.inline_vertices && start == s.inline_vertices;
      if (!inline_data)
        for (uint32_t i = 0; i < reach_count; ++i) {
          const Reach& r = reach[i];
          if (r.index != index || r.whole || r.begin >= r.end) continue;
          const int64_t begin = std::clamp<int64_t>(r.begin, 0, size);
          const int64_t end = std::clamp<int64_t>(r.end, begin, size);
          if (end - begin < size) narrowed_ranges.fetch_add(1, std::memory_order_relaxed);
          start += uint32_t(begin);
          size = uint32_t(end - begin);
        }
      if (coherency) coherency->slot = uint8_t(index);
      if (!RequestRange(start, size, !inline_data, out)) return false;
    }
  }
  if (coherency) coherency->slot = 0xFF;
  if (!s.indexed) return true;
  const uint32_t index_bytes = s.count * (s.index_32bit ? 4u : 2u);
  if (fans_converted && xenos::PrimitiveType(s.primitive) == xenos::PrimitiveType::kTriangleFan)
    return InSharedMemory(s.index_address, index_bytes);
  return RequestRange(s.index_address, index_bytes, true, out);
}

bool GuestMemoryMirror::IndexBounds(const DrawState& s, uint32_t& lo, uint32_t& hi) const {
  namespace xenos = rex::graphics::xenos;
  namespace reg = rex::graphics::reg;
  using xenos::PrimitiveType;
  if (!s.count) return false;
  const auto& regs = *reinterpret_cast<const rex::graphics::RegisterFile*>(s.regs);
  const PrimitiveType prim = PrimitiveType(s.primitive);
  switch (prim) {
    case PrimitiveType::kPointList:
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kTriangleFan:
    case PrimitiveType::kTriangleStrip:
    case PrimitiveType::kRectangleList:
    case PrimitiveType::kQuadList:
      break;
    default:
      return false;
  }
  uint32_t raw_lo = 0, raw_hi = s.count - 1;
  if (s.indexed) {
    const uint32_t size = s.index_32bit ? 4u : 2u;
    if (s.index_address >= kSharedMemorySize ||
        (kSharedMemorySize - s.index_address) / size < s.count)
      return false;
    // As the draw's own index handling (draw.cpp): 16-bit indices swap within half words.
    xenos::Endian endian = xenos::Endian(s.index_endian);
    if (!s.index_32bit && endian != xenos::Endian::kNone && endian != xenos::Endian::k8in16)
      endian = endian == xenos::Endian::k8in32 ? xenos::Endian::k8in16 : xenos::Endian::kNone;
    // The reset index, in either byte order, is no vertex.
    const bool strip = prim == PrimitiveType::kLineStrip || prim == PrimitiveType::kTriangleStrip;
    const bool reset_on = strip && regs.Get<reg::PA_SU_SC_MODE_CNTL>().multi_prim_ib_ena;
    uint32_t reset = regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
    uint32_t reset_swapped = xenos::GpuSwap(reset, endian);
    if (!s.index_32bit) {
      reset &= 0xFFFF;
      reset_swapped &= 0xFFFF;
    }
    uint32_t mn = UINT32_MAX, mx = 0;
    const uint8_t* p = membase + s.index_address;
    if (s.index_32bit) {
      for (uint32_t i = 0; i < s.count; ++i) {
        uint32_t v;
        std::memcpy(&v, p + i * 4, 4);
        v = xenos::GpuSwap(v, endian);
        if (reset_on && (v == reset || v == reset_swapped)) continue;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
    } else if (!reset_on && endian == xenos::Endian::k8in16) {
      // The common case, simple enough to vectorize.
      uint16_t mn16 = 0xFFFF, mx16 = 0;
      for (uint32_t i = 0; i < s.count; ++i) {
        uint16_t v;
        std::memcpy(&v, p + i * 2, 2);
        v = uint16_t((v << 8) | (v >> 8));
        mn16 = std::min(mn16, v);
        mx16 = std::max(mx16, v);
      }
      mn = mn16;
      mx = mx16;
    } else {
      for (uint32_t i = 0; i < s.count; ++i) {
        uint16_t v16;
        std::memcpy(&v16, p + i * 2, 2);
        const uint32_t v = xenos::GpuSwap(v16, endian);
        if (reset_on && (v == reset || v == reset_swapped)) continue;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
    }
    if (mn > mx) return false;  // only reset indices
    raw_lo = mn;
    raw_hi = mx;
  }
  // The shaders add the offset, keep 24 bits and clamp; past 24 bits the order breaks.
  const uint32_t offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  if (uint64_t(raw_hi) + offset > xenos::kVertexIndexMask) return false;
  const uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  const uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  if (min_index > max_index) return false;
  lo = std::clamp(raw_lo + offset, min_index, max_index);
  hi = std::clamp(raw_hi + offset, min_index, max_index);
  return true;
}

bool GuestMemoryMirror::RequestRange(uint32_t start, uint32_t length, bool watch,
                                      std::vector<VertexCopy>& out) {
  if (!length) return true;
  if (!InSharedMemory(start, length)) return false;
  bool exact = !watch || upload_every_draw.load(std::memory_order_relaxed);
  // --fear_d3d12_hot_exact_kb: a short range on a hot page (the game rewrites it, like the
  // particles' per-draw vertices) is copied to the byte as an exact range, so the copy segment
  // tests it byte for byte: the chunks the game appends to one page don't overlap, where their
  // whole pages would.
  bool hot_exact = false;
  if (!exact && hot_exact_bytes && length <= hot_exact_bytes) {
    const uint32_t first = start >> kPageShift, last = (start + length - 1) >> kPageShift;
    for (uint32_t p = first; p <= last && !hot_exact; ++p)
      hot_exact = (hot[p >> 6].load(std::memory_order_relaxed) >> (p & 63)) & 1;
    exact = hot_exact;
  }
  if (preparing) draw_reads.emplace_back(start, start + length, exact);
  const uint64_t fence = upload_fence;
  PrepareDiag& pd = *diag_;
  auto copy = [&](uint32_t from, uint32_t size) {
    if (preparing && !segment_conflict) {
      segment_conflict = SegmentReads(from, from + size);
      segment_conflict_exact = segment_conflict && exact;
    }
    if (coherency && preparing) CheckSegmentCopy(from, size);
    if (!sink_->MapShared(from, size)) return false;
    Upload a;
    if (!sink_->AllocateUpload(size, fence, &a)) return false;
    if (coherency) {
      // The upload is made from the shadow, so the two hold the same bytes.
      std::memcpy(coherency->shadow.get() + from, membase + from, size);
      std::memcpy(a.cpu, coherency->shadow.get() + from, size);
      coherency->frame_copies.emplace_back(from, size);
    } else {
      std::memcpy(a.cpu, membase + from, size);
    }
    out.emplace_back(a.buffer, a.offset, from, size);
    *upload_bytes_ += size;
    ++*uploads_;
    return true;
  };
  if (exact && !hot_exact) {
    ++pd.unwatched;
    return copy(start, length);
  }
  ++pd.ranges;
  const uint32_t first = start >> kPageShift, last = (start + length - 1) >> kPageShift;
  pd.range_pages += last - first + 1;
  const uint32_t trace_request =
      trace.file ? trace.request.fetch_add(1, std::memory_order_relaxed) : 0;
  const uint32_t frame = watch_frame.load(std::memory_order_relaxed);
  if (hot_exact) {
    // Aging as for hashed hot pages below, on exact_hash (copy_hash is cleared here): a hot
    // page unchanged over kHotMaxHashes requests is watched again, and its ranges go back to
    // whole-page copies that are made only when it changes.
    uint64_t hashed = 0;
    for (uint32_t q = first; q <= last; ++q) {
      const uint32_t w = q >> 6;
      const uint64_t bit = 1ull << (q & 63);
      ++hashed;
      const uint64_t hash = PageHash(q);
      if (hash != exact_hash[q]) {
        exact_hash[q] = hash;
        hot_hashes[q] = 0;
      } else if ((hot[w].load(std::memory_order_relaxed) & bit) &&
                 !(pending[w].load(std::memory_order_relaxed) & bit) &&
                 ++hot_hashes[q] >= kHotMaxHashes) {
        hot[w].fetch_and(~bit, std::memory_order_relaxed);
        quick_rewrites[q].store(0, std::memory_order_relaxed);
        armed_frame[q].store(frame, std::memory_order_relaxed);
        pending[w].fetch_or(bit, std::memory_order_relaxed);
        QueueArm(q, q + 1, trace_request);
      }
      // Shared memory now holds part of the page newer than its copy_hash says: a later
      // whole-page request copies the page again instead of trusting that hash (if the guest
      // wrote the old bytes back, it would match and leave these newer ones in place).
      copy_hash[q].store(0, std::memory_order_relaxed);
    }
    SetPageBits(valid.get(), first, last + 1, false);
    hash_pages.fetch_add(hashed, std::memory_order_relaxed);
    ++pd.unwatched;
    if (!copy(start, length)) return false;
    if (coherency) CheckRequest(start, length);
    return true;
  }
  // Hashed pages whose content changed, copied in runs of adjacent pages.
  uint32_t changed_first = 0, changed_end = 0;
  auto copy_changed = [&] {
    if (changed_first == changed_end ||
        copy(changed_first << kPageShift, (changed_end - changed_first) << kPageShift))
      return true;
    // Hashed as changed but not copied: the next request copies them. The watch thread may
    // have found a pending one unchanged meanwhile, hence the valid bits.
    for (uint32_t q = changed_first; q < changed_end; ++q)
      copy_hash[q].store(0, std::memory_order_relaxed);
    SetPageBits(valid.get(), changed_first, changed_end, false);
    return false;
  };
  uint64_t hashed = 0, changed = 0;
  uint32_t p = first;
  while (p <= last) {
    // Skip valid pages that are neither hot nor pending a 64-page word at a time (whole vertex
    // buffers are requested per draw).
    const uint32_t w = p >> 6;
    // Pending before valid: a pending bit seen clear makes the valid bit the watch thread left
    // visible.
    const uint64_t pending_bits = pending[w].load(std::memory_order_acquire);
    const uint64_t hot_bits = hot[w].load(std::memory_order_relaxed);
    uint64_t work =
        (~valid[w].load(std::memory_order_relaxed) | hot_bits | pending_bits) & (~0ull << (p & 63));
    if (w == last >> 6) work &= ~0ull >> (63 - (last & 63));
    if (!work) {
      p = (w + 1) << 6;
      continue;
    }
    p = (w << 6) + uint32_t(std::countr_zero(work));
    const uint64_t bit = 1ull << (p & 63);
    if ((hot_bits | pending_bits) & bit) {
      ++hashed;
      const uint64_t hash = PageHash(p);
      if (hash != copy_hash[p].load(std::memory_order_relaxed)) {
        copy_hash[p].store(hash, std::memory_order_relaxed);
        hot_hashes[p] = 0;
        ++changed;
        if (changed_end != p) {
          if (!copy_changed()) return false;
          changed_first = p;
        }
        changed_end = p + 1;
        ++p;
        continue;
      }
      if ((pending_bits & bit) || ++hot_hashes[p] < kHotMaxHashes) {
        ++p;
        continue;
      }
      // Hot, but read far more often than written: watched again. Its copy is current.
      hot[w].fetch_and(~bit, std::memory_order_relaxed);
      quick_rewrites[p].store(0, std::memory_order_relaxed);
      armed_frame[p].store(frame, std::memory_order_relaxed);
      pending[w].fetch_or(bit, std::memory_order_relaxed);
      QueueArm(p, p + 1, trace_request);
      ++p;
      continue;
    }
    uint32_t run_end = p + 1;
    while (run_end <= last) {
      const uint32_t rw = run_end >> 6;
      if ((valid[rw].load(std::memory_order_relaxed) | hot[rw].load(std::memory_order_relaxed) |
           pending[rw].load(std::memory_order_relaxed)) &
          (1ull << (run_end & 63)))
        break;
      ++run_end;
    }
    // Hashed before the copy, so a write between the two shows as a change.
    for (uint32_t q = p; q < run_end; ++q)
      copy_hash[q].store(PageHash(q), std::memory_order_relaxed);
    hashed += run_end - p;
    ++pd.range_runs;
    // Not copied: neither valid nor pending, so the next request copies them.
    if (!copy(p << kPageShift, (run_end - p) << kPageShift)) return false;
    for (uint32_t q = p; q < run_end; ++q) armed_frame[q].store(frame, std::memory_order_relaxed);
    SetPageBits(pending.get(), p, run_end, true);
    QueueArm(p, run_end, trace_request);
    p = run_end;
  }
  if (hashed) {
    hash_pages.fetch_add(hashed, std::memory_order_relaxed);
    hash_copies.fetch_add(changed, std::memory_order_relaxed);
  }
  if (!copy_changed()) return false;
  if (coherency) CheckRequest(start, length);
  return true;
}

void GuestMemoryMirror::QueueArm(uint32_t first, uint32_t end, uint32_t request) {
  bool wake;
  {
    std::lock_guard<std::mutex> lock(arm_mutex);
    arm_queue.push_back({first, end, request});
    wake = std::exchange(arm_idle, false);
  }
  if (wake) arm_cv.notify_one();
}

void GuestMemoryMirror::WatchThread() {
  std::vector<ArmRun> runs;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(arm_mutex);
      while (arm_queue.empty() && !arm_stopping) {
        arm_idle = true;
        arm_cv.wait(lock);
      }
      if (arm_queue.empty()) return;
      runs.swap(arm_queue);
    }
    const uint64_t t0 = NowNs();
    std::sort(runs.begin(), runs.end(),
              [](const ArmRun& a, const ArmRun& b) { return a.first < b.first; });
    uint64_t arms = 0, pages = 0, changed = 0;
    for (size_t i = 0; i < runs.size();) {
      const uint32_t first = runs[i].first;
      uint32_t end = runs[i].end;
      size_t j = i + 1;
      while (j < runs.size() && runs[j].first == end) end = runs[j++].end;
      // Valid before the arm: a write after it faults and clears the bit, which stays clear.
      SetPageBits(valid.get(), first, end, true);
      memory->EnablePhysicalMemoryAccessCallbacks(first << kPageShift, (end - first) << kPageShift,
                                                  true, false);
      for (uint32_t q = first; q < end; ++q) {
        if (PageHash(q) == copy_hash[q].load(std::memory_order_relaxed)) continue;
        // Written between its copy and the arm: the next request copies it again.
        SetPageBits(valid.get(), q, q + 1, false);
        ++changed;
      }
      SetPageBits(pending.get(), first, end, false);
      if (trace.file) trace.Add(WatchTrace::kArm, first, end - first, runs[i].request);
      ++arms;
      pages += end - first;
      i = j;
    }
    runs.clear();
    watch_arms.fetch_add(arms, std::memory_order_relaxed);
    watch_arm_pages.fetch_add(pages, std::memory_order_relaxed);
    watch_arm_changed.fetch_add(changed, std::memory_order_relaxed);
    watch_arm_ns.fetch_add(NowNs() - t0, std::memory_order_relaxed);
  }
}

void GuestMemoryMirror::CheckRequest(uint32_t start, uint32_t length) {
  Coherency& c = *coherency;
  const uint32_t stamp = watch_frame.load(std::memory_order_relaxed) + 1;
  if (c.ranges.empty() || c.ranges.back().start != start || c.ranges.back().length != length)
    c.ranges.push_back({start, length, c.slot});
  const uint32_t end = start + length;
  for (uint32_t p = start >> kPageShift; p <= (end - 1) >> kPageShift; ++p) {
    if (c.checked[p] == stamp) continue;
    const uint32_t lo = std::max(start, p << kPageShift), hi = std::min(end, (p + 1) << kPageShift);
    if (!std::memcmp(membase + lo, c.shadow.get() + lo, hi - lo)) {
      if (hi - lo == 1u << kPageShift) c.checked[p] = stamp;
      continue;
    }
    const uint64_t bit = 1ull << (p & 63);
    const bool is_valid = valid[p >> 6].load(std::memory_order_relaxed) & bit;
    const bool hashed = (hot[p >> 6].load(std::memory_order_relaxed) |
                         pending[p >> 6].load(std::memory_order_relaxed)) &
                        bit;
    // A write after this request copied or trusted the page: it faulted (valid cleared) or, on a
    // hot or pending page, changed the hash since. Either way the next request copies it.
    const bool written_since =
        hashed ? PageHash(p) != copy_hash[p].load(std::memory_order_relaxed) : !is_valid;
    if (written_since) continue;
    ++c.stale;
    uint32_t differ = 0, first = UINT32_MAX;
    for (uint32_t i = lo; i < hi; ++i)
      if (membase[i] != c.shadow[i]) {
        ++differ;
        first = std::min(first, i);
      }
    D3D12_LOG_FIRST(200, WARN,
                   "D3D12 coherency: frame {} stale page {:05X} (request {:08X}+{:X}, slot "
                   "{}): {} of {} bytes differ from {:08X}, {} armed at frame {}, last write fault "
                   "frame {}",
                   stamp - 1, p, start, length, c.slot, differ, hi - lo, first,
                   hashed ? "hashed" : "valid", armed_frame[p].load(std::memory_order_relaxed),
                   int64_t(write_frame[p].load(std::memory_order_relaxed)) - 1);
  }
}

void GuestMemoryMirror::CheckSegmentCopy(uint32_t from, uint32_t size) {
  Coherency& c = *coherency;
  const uint32_t end = from + size;
  for (const auto& [b, e] : c.segment_reads) {
    if (!(b < end && from < e)) continue;
    if (segment_conflict) return;
    D3D12_LOG_FIRST(
        200, WARN,
        "D3D12 coherency: frame {} copy {:08X}+{:X} overwrites {:08X}-{:08X}, read "
        "earlier in its copy segment, without a new segment",
        watch_frame.load(std::memory_order_relaxed), from, size, b, e);
    return;
  }
}

void GuestMemoryMirror::CheckLateWrites() {
  Coherency& c = *coherency;
  const uint32_t stamp = watch_frame.load(std::memory_order_relaxed) + 1;
  uint64_t late = 0, stale = 0;
  uint32_t late_ranges = 0, slot_pages[256] = {};
  const Coherency::Range* first_late = nullptr;
  for (const Coherency::Range& r : c.ranges) {
    const uint32_t end = r.start + r.length;
    bool any = false;
    for (uint32_t p = r.start >> kPageShift; p <= (end - 1) >> kPageShift; ++p) {
      if (c.swap_checked[p] == stamp) continue;
      c.swap_checked[p] = stamp;
      const uint32_t lo = std::max(r.start, p << kPageShift);
      const uint32_t hi = std::min(end, (p + 1) << kPageShift);
      if (!std::memcmp(membase + lo, c.shadow.get() + lo, hi - lo)) continue;
      const uint64_t bit = 1ull << (p & 63);
      if ((valid[p >> 6].load(std::memory_order_relaxed) & bit) &&
          !((hot[p >> 6].load(std::memory_order_relaxed) |
             pending[p >> 6].load(std::memory_order_relaxed)) &
            bit)) {
        // Valid and watched, yet guest memory differs: a write the watch missed.
        ++stale;
        D3D12_LOG_FIRST(200, WARN,
                       "D3D12 coherency: frame {} stale page {:05X} at the Swap (range "
                       "{:08X}+{:X}, slot {}), armed at frame {}",
                       stamp - 1, p, r.start, r.length, r.slot,
                       armed_frame[p].load(std::memory_order_relaxed));
        continue;
      }
      ++late;
      ++slot_pages[r.slot];
      any = true;
    }
    if (any) {
      ++late_ranges;
      if (!first_late) first_late = &r;
    }
  }
  c.late += late;
  c.stale += stale;
  if (late) {
    auto slots = [&] {
      std::string list;
      for (uint32_t s = 0; s < 256; ++s)
        if (slot_pages[s]) list += fmt::format(" {}:{}", s == 0xFF ? -1 : int(s), slot_pages[s]);
      return list;
    };
    D3D12_LOG_FIRST(400, INFO,
                   "D3D12 coherency: frame {} {} pages of {} ranges rewritten after a draw "
                   "read them (first {:08X}+{:X}); pages per slot{}",
                   stamp - 1, late, late_ranges, first_late->start, first_late->length, slots());
  }
  c.ranges.clear();
}

}  // namespace fear::d3d12
