// F.E.A.R. 360 - a sampling profiler for where the CPU time goes. See profiler.h.

#include "profiler.h"

#include "fear_title.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/thread.h>

REXCVAR_DEFINE_BOOL(fear_profile, false, "F.E.A.R.",
                    "Sample every busy thread and log, every 10 s, each thread's CPU use and the "
                    "game functions it spends it in (diagnosis; costs some CPU)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(fear_profile_hz, 500, "F.E.A.R.",
                      "Samples a second per busy thread with fear_profile")
    .range(50, 2000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <rex/ppc/func.h>

namespace fear::profiler {
namespace {

using Clock = std::chrono::steady_clock;

// The parts of the executable by start address, each up to the next one's start
// (docs/ARCHITECTURE.md, "Map of the 360 executable"; from class names, strings and calls with
// tools/analysis/, to 64 KB except where a boundary was found exactly). Approximate at the edges.
struct GuestModule {
  uint32_t start;
  const char* name;
};
constexpr GuestModule kFearModules[] = {
    {0x82170000, "engine"},           {0x82280000, "engine renderer"},
    {0x822D0000, "engine sound"},     {0x822F0000, "game client"},
    {0x82510000, "game server"},      {0x826C0000, "AI planner"},
    {0x82810000, "game shared"},      {0x82870000, "C runtime"},
    {0x828A0000, "Havok"},            {0x82962328, "Direct3D"},
    {0x829C0000, "D3DX and image libraries"},
    {0x82AC0000, "ClientFX"},         {0x82BE0000, "shader compilers"},
    {0x83040000, "startup, Bink, misc"},
};
// Files links them in another order: the engine renderer sits inside the game client, the shared
// game code after the libraries.
constexpr GuestModule kFilesModules[] = {
    {0x82190000, "engine"},           {0x822B0000, "game client"},
    {0x824E0000, "engine renderer"},  {0x82530000, "game client"},
    {0x82560000, "game server"},      {0x82710000, "AI planner"},
    {0x82870000, "C runtime"},        {0x828A0000, "Havok"},
    {0x82958920, "Direct3D"},         {0x829A0000, "D3DX, XDK and image libraries"},
    {0x82AE0000, "game shared"},      {0x82B50000, "ClientFX"},
    {0x82BE0000, "shader compilers"}, {0x82F70000, "startup, Bink, misc"},
};
constexpr std::span<const GuestModule> kModules =
    fear::title::kFiles ? std::span<const GuestModule>(kFilesModules)
                        : std::span<const GuestModule>(kFearModules);

// The module's index in kModules, or kModules.size() for an address before the map.
size_t GuestModuleIndex(uint32_t address) {
  auto it = std::upper_bound(kModules.begin(), kModules.end(), address,
                             [](uint32_t a, const GuestModule& m) { return a < m.start; });
  return it == kModules.begin() ? kModules.size() : size_t(std::prev(it) - kModules.begin());
}

// Direct3D's wait reasons (XDK D3DBLOCKTYPE order; see profiler.h).
constexpr const char* kWaitReasons[] = {
    "untracked",           "ring buffer full",     "secondary ring full",
    "swap throttle",       "until idle",           "until not busy",
    "on fence",            "vertex shader release", "pixel shader release",
    "vertex buffer release", "vertex buffer lock", "index buffer release",
    "index buffer lock",   "texture release",      "texture lock",
    "command buffer release", "command buffer lock", "constant buffer release",
    "constant buffer lock", "other",
};
constexpr uint32_t kWaitOther = std::size(kWaitReasons) - 1;

// The reason each thread is waiting in Direct3D for, + 1 (0: not waiting), by thread id. Written by
// the hooks on the waiting thread, read by the sampler while that thread is suspended (atomics
// only: no locks). A slot belongs to one thread id for good; 64 is far more than the threads that
// draw.
struct WaitSlot {
  std::atomic<uint32_t> thread{0};
  std::atomic<uint32_t> reason{0};
};
WaitSlot g_wait_slots[64];
std::atomic<bool> g_profiling{false};

WaitSlot* FindWaitSlot(uint32_t thread) {
  for (WaitSlot& slot : g_wait_slots) {
    if (slot.thread.load(std::memory_order_relaxed) == thread) return &slot;
  }
  return nullptr;
}

WaitSlot* ThisThreadsWaitSlot() {
  thread_local WaitSlot* slot = nullptr;
  if (slot) return slot;
  const uint32_t id = GetCurrentThreadId();
  if ((slot = FindWaitSlot(id))) return slot;
  for (WaitSlot& candidate : g_wait_slots) {
    uint32_t free = 0;
    if (candidate.thread.compare_exchange_strong(free, id)) return slot = &candidate;
  }
  return nullptr;  // all taken: this thread's waits count as its functions
}

// Translated functions by native address: (native start, guest address), sorted.
std::vector<std::pair<uintptr_t, uint32_t>> g_functions;
uintptr_t g_exe_base = 0, g_exe_end = 0;

void BuildFunctionTable() {
  for (const PPCFuncMapping* m = PPCFuncMappings; m->guest || m->host; ++m) {
    if (m->host) g_functions.emplace_back(reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest));
  }
  std::sort(g_functions.begin(), g_functions.end());
  const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  g_exe_base = reinterpret_cast<uintptr_t>(base);
  g_exe_end = g_exe_base + nt->OptionalHeader.SizeOfImage;
}

// A module's named exports by address (RVA), sorted: a sample outside the translated code is named
// after the nearest export at or below it ("ucrtbase.dll!fma"; a function that isn't exported gets
// the name of the export before it). Wine's builtin DLLs are PE files with export tables too.
// Built on first use per module; only the sampling thread touches these.
struct Exports {
  std::vector<std::pair<uint32_t, std::string>> by_rva;
};
std::unordered_map<uintptr_t, Exports> g_exports;  // by module base

const Exports& ModuleExports(uintptr_t module) {
  auto [it, added] = g_exports.try_emplace(module);
  if (!added || !module) return it->second;
  const auto* base = reinterpret_cast<const uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return it->second;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return it->second;
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dir.VirtualAddress || !dir.Size) return it->second;
  const auto* table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
  const auto* functions = reinterpret_cast<const DWORD*>(base + table->AddressOfFunctions);
  const auto* names = reinterpret_cast<const DWORD*>(base + table->AddressOfNames);
  const auto* ordinals = reinterpret_cast<const WORD*>(base + table->AddressOfNameOrdinals);
  for (DWORD i = 0; i < table->NumberOfNames; ++i) {
    if (ordinals[i] >= table->NumberOfFunctions) continue;
    const DWORD rva = functions[ordinals[i]];
    // A forwarder ("ntdll.RtlFoo") points into the export directory, not at code.
    if (!rva || (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)) continue;
    it->second.by_rva.emplace_back(rva, reinterpret_cast<const char*>(base + names[i]));
  }
  std::sort(it->second.by_rva.begin(), it->second.by_rva.end());
  return it->second;
}

std::string ModuleName(uintptr_t module) {
  if (!module) return "(unknown)";
  char path[MAX_PATH] = {};
  GetModuleFileNameA(reinterpret_cast<HMODULE>(module), path, MAX_PATH);
  const char* name = std::strrchr(path, '\\');
  return name ? name + 1 : path;
}

// A sample's bucket: a guest function (its address), or a module and export (the top bit set,
// the module's base, which is 64 KB aligned, and in the low 16 bits the export's index + 1, or 0
// for none or "(unknown)": code outside any module, e.g. Wine's Unix side).
constexpr uint64_t kModuleTag = 1ull << 63;

uint64_t Bucket(uintptr_t ip) {
  if (ip >= g_exe_base && ip < g_exe_end && !g_functions.empty()) {
    auto it = std::upper_bound(g_functions.begin(), g_functions.end(),
                               std::make_pair(ip, UINT32_MAX));
    // Within 1 MB of a translated function's start: that function (functions of the generated
    // code sit together; the host's own code in the exe ends up as "fear.exe").
    if (it != g_functions.begin() && ip - std::prev(it)->first < (1u << 20)) {
      return std::prev(it)->second;
    }
  }
  HMODULE handle = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(ip), &handle);
  const auto module = reinterpret_cast<uintptr_t>(handle);
  uint64_t index = 0;
  if (module && !(module & 0xFFFF) && module != g_exe_base) {
    const auto& by_rva = ModuleExports(module).by_rva;
    auto it = std::upper_bound(by_rva.begin(), by_rva.end(),
                               std::make_pair(uint32_t(ip - module), std::string()),
                               [](const auto& a, const auto& b) { return a.first < b.first; });
    if (it != by_rva.begin() && size_t(it - by_rva.begin()) < 0xFFFF) {
      index = uint64_t(it - by_rva.begin());  // the one before it, + 1
    }
  }
  return kModuleTag | uint64_t(module) | index;
}

std::string BucketName(uint64_t bucket) {
  char text[MAX_PATH + 16];
  if (!(bucket & kModuleTag)) {
    std::snprintf(text, sizeof(text), "sub_%08X", uint32_t(bucket));
    return text;
  }
  const uintptr_t module = uintptr_t(bucket & ~kModuleTag & ~uint64_t(0xFFFF));
  const uint64_t index = bucket & 0xFFFF;
  std::string name = ModuleName(module);
  if (index) {
    const auto& by_rva = ModuleExports(module).by_rva;
    if (index <= by_rva.size()) name += "!" + by_rva[index - 1].second;
  }
  return name;
}

// Where a thread started ("ucrtbase.dll!thread_start", "dxgi.dll!..."), for threads nobody named.
std::string StartName(HANDLE thread) {
  using QueryThread = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<QueryThread>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  void* start = nullptr;
  // 9: ThreadQuerySetWin32StartAddress.
  if (!query || query(thread, 9, &start, sizeof(start), nullptr) < 0 || !start) return "";
  return BucketName(Bucket(reinterpret_cast<uintptr_t>(start)));
}

std::string ThreadName(HANDLE thread, DWORD id) {
  using GetDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
  static const auto get = reinterpret_cast<GetDescription>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  std::string name;
  PWSTR wide = nullptr;
  if (get && SUCCEEDED(get(thread, &wide)) && wide) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      name.resize(size - 1);
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, name.data(), size, nullptr, nullptr);
    }
    LocalFree(wide);
  }
  if (name.empty()) {
    name = "thread " + std::to_string(id);
    const std::string start = StartName(thread);
    if (!start.empty()) name += " (from " + start + ")";
  }
  return name;
}

uint64_t CpuTime100ns(HANDLE thread) {
  FILETIME created, exited, kernel, user;
  if (!GetThreadTimes(thread, &created, &exited, &kernel, &user)) return 0;
  auto value = [](const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return value(kernel) + value(user);
}

struct Thread {
  HANDLE handle = nullptr;
  std::string name;
  uint64_t cpu_at_refresh = 0;  // for "busy"
  uint64_t cpu_at_report = 0;   // for the report's CPU use
  bool busy = true;
  uint64_t samples = 0;
  std::unordered_map<uint64_t, uint32_t> buckets;
  // The same samples by part of the game, except those taken in a Direct3D wait: a guest
  // module's index in kModules, or the bucket's module (tag and base, no export).
  std::unordered_map<uint64_t, uint32_t> modules;
  WaitSlot* wait_slot = nullptr;  // once the thread has waited in Direct3D
  uint64_t wait_samples = 0;
  std::array<uint32_t, std::size(kWaitReasons)> waits = {};
};

uint64_t ModuleKey(uint64_t bucket) {
  return (bucket & kModuleTag) ? bucket & ~uint64_t(0xFFFF) : GuestModuleIndex(uint32_t(bucket));
}

// A thread's samples by part of the game (see profiler.h), merged by name (Files' game client
// comes in two pieces).
std::string ModulesText(const Thread& t) {
  std::map<std::string, uint64_t> by_name;
  for (const auto& [key, count] : t.modules) {
    std::string name;
    if (key & kModuleTag) {
      name = ModuleName(uintptr_t(key & ~kModuleTag));
    } else {
      name = key < kModules.size() ? kModules[key].name : "(before the map)";
    }
    by_name[name] += count;
  }
  if (t.wait_samples) by_name["Direct3D wait"] += t.wait_samples;
  std::vector<std::pair<uint64_t, std::string>> top;
  for (auto& [name, count] : by_name) top.emplace_back(count, name);
  std::sort(top.rbegin(), top.rend());
  const uint64_t total = std::max<uint64_t>(t.samples, 1);
  std::string text;
  for (size_t i = 0; i < top.size() && i < 12 && top[i].first * 200 >= total; ++i) {
    text += fmt::format("{}{} {:.0f}%", i ? ", " : "", top[i].second, 100.0 * top[i].first / total);
  }
  return text;
}

std::string WaitsText(const Thread& t) {
  std::vector<std::pair<uint32_t, size_t>> top;
  for (size_t reason = 0; reason < t.waits.size(); ++reason) {
    if (t.waits[reason]) top.emplace_back(t.waits[reason], reason);
  }
  std::sort(top.rbegin(), top.rend());
  const uint64_t total = std::max<uint64_t>(t.samples, 1);
  std::string text;
  for (size_t i = 0; i < top.size(); ++i) {
    text += fmt::format("{}{} {:.1f}%", i ? ", " : "", kWaitReasons[top[i].second],
                        100.0 * top[i].first / total);
  }
  return text;
}

void Run() {
  BuildFunctionTable();
  const DWORD self = GetCurrentThreadId();
  const DWORD pid = GetCurrentProcessId();
  const uint32_t hz = REXCVAR_GET(fear_profile_hz);
  const auto interval = std::chrono::nanoseconds(1000000000ll / hz);
  REXLOG_INFO("[profile] sampling busy threads at {} Hz; {} translated functions", hz,
              g_functions.size());
  std::map<DWORD, Thread> threads;
  auto last_refresh = Clock::time_point();
  auto last_report = Clock::now();
  auto next = Clock::now();
  while (true) {
    const auto now = Clock::now();
    // Once a second: the process's threads, and which of them used the CPU since.
    if (now - last_refresh >= std::chrono::seconds(1)) {
      last_refresh = now;
      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry = {sizeof(entry)};
        std::map<DWORD, bool> alive;
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
          if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
          alive[entry.th32ThreadID] = true;
          if (threads.count(entry.th32ThreadID)) continue;
          HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                         THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                                     FALSE, entry.th32ThreadID);
          if (!handle) continue;
          Thread& t = threads[entry.th32ThreadID];
          t.handle = handle;
          t.name = ThreadName(handle, entry.th32ThreadID);
          t.cpu_at_refresh = t.cpu_at_report = CpuTime100ns(handle);
        }
        CloseHandle(snapshot);
        for (auto it = threads.begin(); it != threads.end();) {
          if (!alive.count(it->first)) {
            CloseHandle(it->second.handle);
            it = threads.erase(it);
            continue;
          }
          const uint64_t cpu = CpuTime100ns(it->second.handle);
          // Busy: over 2% of a core in the last second.
          it->second.busy = cpu - it->second.cpu_at_refresh > 200000;
          it->second.cpu_at_refresh = cpu;
          // Names can be set after a thread starts.
          if (it->second.name.rfind("thread ", 0) == 0) {
            it->second.name = ThreadName(it->second.handle, it->first);
          }
          if (!it->second.wait_slot) it->second.wait_slot = FindWaitSlot(it->first);
          ++it;
        }
      }
    }
    // A sample of each busy thread.
    for (auto& [id, t] : threads) {
      if (!t.busy) continue;
      if (SuspendThread(t.handle) == DWORD(-1)) continue;
      CONTEXT context = {};
      context.ContextFlags = CONTEXT_CONTROL;
      const bool sampled = GetThreadContext(t.handle, &context);
      // Whether it's waiting in Direct3D, while it's stopped (an atomic: no lock).
      const uint32_t waiting = t.wait_slot ? t.wait_slot->reason.load(std::memory_order_relaxed) : 0;
      // Resumed before anything that can take a lock: naming the sample looks up its module
      // (the loader lock) and counting it allocates (the heap's), and the suspended thread may
      // hold either.
      ResumeThread(t.handle);
      if (sampled) {
        ++t.samples;
        const uint64_t bucket = Bucket(uintptr_t(context.Rip));
        ++t.buckets[bucket];
        if (waiting) {
          ++t.wait_samples;
          ++t.waits[std::min(waiting - 1, kWaitOther)];
        } else {
          ++t.modules[ModuleKey(bucket)];
        }
      }
    }
    // Every 10 s: the report.
    if (now - last_report >= std::chrono::seconds(10)) {
      const double wall_100ns = std::chrono::duration<double>(now - last_report).count() * 1e7;
      last_report = now;
      struct Row {
        double cpu;
        Thread* t;
      };
      std::vector<Row> rows;
      std::unordered_map<uint64_t, uint64_t> all;
      double total_cpu = 0;
      for (auto& [id, t] : threads) {
        const uint64_t cpu = CpuTime100ns(t.handle);
        const double share = double(cpu - t.cpu_at_report) / wall_100ns;
        t.cpu_at_report = cpu;
        total_cpu += share;
        rows.push_back({share, &t});
      }
      std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cpu > b.cpu; });
      // The profiler's own thread (not sampled, not in the rows). Under Wine part of its cost
      // is in the wineserver process, which no thread here shows.
      static uint64_t self_cpu_at_report = 0;
      const uint64_t self_cpu = CpuTime100ns(GetCurrentThread());
      REXLOG_INFO("[profile] the profiler itself: {:.1f}% of a core",
                  double(self_cpu - self_cpu_at_report) / wall_100ns * 100.0);
      self_cpu_at_report = self_cpu;
      REXLOG_INFO("[profile] CPU over 10 s: {:.2f} cores in all, {} threads", total_cpu,
                  threads.size());
      for (const Row& row : rows) {
        for (auto& [bucket, count] : row.t->buckets) all[bucket] += count;
      }
      int shown = 0;
      for (const Row& row : rows) {
        if (shown++ >= 10 || row.cpu < 0.02) break;
        std::vector<std::pair<uint32_t, uint64_t>> top;
        for (auto& [bucket, count] : row.t->buckets) top.emplace_back(count, bucket);
        std::sort(top.rbegin(), top.rend());
        std::string text;
        for (size_t i = 0; i < top.size() && i < 8; ++i) {
          text += fmt::format("{}{} {:.0f}%", i ? ", " : "", BucketName(top[i].second),
                              100.0 * top[i].first / std::max<uint64_t>(row.t->samples, 1));
        }
        REXLOG_INFO("[profile]   {:5.1f}% of a core  {}: {}", row.cpu * 100.0, row.t->name, text);
        REXLOG_INFO("[profile]       modules: {}", ModulesText(*row.t));
        if (row.t->wait_samples) {
          REXLOG_INFO("[profile]       Direct3D waits: {}", WaitsText(*row.t));
        }
      }
      std::vector<std::pair<uint64_t, uint64_t>> top_all;
      uint64_t total = 0;
      for (auto& [bucket, count] : all) {
        top_all.emplace_back(count, bucket);
        total += count;
      }
      std::sort(top_all.rbegin(), top_all.rend());
      std::string text;
      for (size_t i = 0; i < top_all.size() && i < 25; ++i) {
        text += fmt::format("{}{} {:.1f}%", i ? ", " : "", BucketName(top_all[i].second),
                            100.0 * top_all[i].first / std::max<uint64_t>(total, 1));
      }
      REXLOG_INFO("[profile] top over the busy threads: {}", text);
      for (auto& [id, t] : threads) {
        t.samples = 0;
        t.buckets.clear();
        t.modules.clear();
        t.wait_samples = 0;
        t.waits = {};
      }
    }
    next += interval;
    const auto after = Clock::now();
    if (next < after) next = after;
    rex::thread::SleepUntil(next);
  }
}

}  // namespace

void Start() {
  if (!REXCVAR_GET(fear_profile)) return;
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  g_profiling = true;
  std::thread([] {
    SetThreadDescription(GetCurrentThread(), L"F.E.A.R. profiler");
    Run();
  }).detach();
}

}  // namespace fear::profiler

void FearD3DWaitBegin(PPCRegister& r5) {
  using namespace fear::profiler;
  if (!g_profiling.load(std::memory_order_relaxed)) return;
  if (WaitSlot* slot = ThisThreadsWaitSlot()) {
    slot->reason.store(std::min(r5.u32, kWaitOther) + 1, std::memory_order_relaxed);
  }
}

void FearD3DWaitEnd() {
  using namespace fear::profiler;
  if (!g_profiling.load(std::memory_order_relaxed)) return;
  if (WaitSlot* slot = ThisThreadsWaitSlot()) slot->reason.store(0, std::memory_order_relaxed);
}

#else

namespace fear::profiler {
void Start() {}
}  // namespace fear::profiler

void FearD3DWaitBegin(PPCRegister&) {}
void FearD3DWaitEnd() {}

#endif
