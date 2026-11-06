// F.E.A.R. 360 - the loaded executable against the hooks' fingerprints. See image_check.h.

#include "image_check.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fear_title.h"

namespace {

constexpr uint32_t kSiteBytes = 32;  // 8 instructions, as tools/hook_sites.py hashes

struct Site {
  uint32_t address;
  uint64_t hash;
  const char* name;
};

constexpr Site kSites[] = {
#define FEAR_HOOK_SITE(address, hash, name) {address, hash, name},
#if defined(FEAR_TITLE_FILES)
#include "hook_sites_files.inc"
#else
#include "hook_sites_fear.inc"
#endif
#undef FEAR_HOOK_SITE
};

// FNV-1a, 64 bits: tools/hook_sites.py's fnv1a64.
uint64_t Hash(const uint8_t* data, size_t size) {
  uint64_t value = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 0x100000001B3ull;
  return value;
}

std::atomic<bool> g_mismatch{false};

}  // namespace

namespace fear::image_check {

void Run() {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  const uint8_t* base = memory->virtual_membase();
  int wrong = 0;
  for (const Site& site : kSites) {
    // A smaller image may not reach the address at all.
    bool same = false;
    uint32_t protect = 0;
    auto* heap = memory->LookupHeap(site.address);
    if (heap && heap->QueryProtect(site.address, &protect) &&
        (protect & rex::memory::kMemoryProtectRead) &&
        heap->QueryProtect(site.address + kSiteBytes - 1, &protect) &&
        (protect & rex::memory::kMemoryProtectRead)) {
      same = Hash(base + site.address, kSiteBytes) == site.hash;
    }
    if (!same) {
      ++wrong;
      REXLOG_ERROR("F.E.A.R.: the game's code at {:08X} isn't what the {} hook was written for",
                   site.address, site.name);
    }
  }
  if (wrong) {
    g_mismatch = true;
    REXLOG_ERROR("F.E.A.R.: this is not the supported {} executable ({} of {} hooks don't match "
                 "its code): fixes may misbehave or crash the game",
                 fear::title::kFiles ? "F.E.A.R. Files" : "F.E.A.R.", wrong, std::size(kSites));
  } else {
    REXLOG_INFO("F.E.A.R.: the executable's code matches at all {} hooks", std::size(kSites));
  }
}

bool Mismatch() { return g_mismatch; }

}  // namespace fear::image_check
