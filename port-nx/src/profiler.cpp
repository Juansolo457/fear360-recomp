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
#include <tuple>
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

// The exe's function holding an address, from its unwind table; null for a leaf function (no
// entry) or outside the exe.
const RUNTIME_FUNCTION* ExeFunctionEntry(uintptr_t address) {
  DWORD64 image = 0;
  const RUNTIME_FUNCTION* entry = RtlLookupFunctionEntry(DWORD64(address), &image, nullptr);
  return entry && image == g_exe_base ? entry : nullptr;
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
// for none or "(unknown)": code outside any module, e.g. Wine's Unix side). The exe's host code
// has the next bit set too, and the RVA of the function holding it in the low 32 bits.
constexpr uint64_t kModuleTag = 1ull << 63;
constexpr uint64_t kHostCodeTag = 1ull << 62;

uint64_t Bucket(uintptr_t ip) {
  if (ip >= g_exe_base && ip < g_exe_end && !g_functions.empty()) {
    // The translated function at or before it, if the unwind table says it holds the address
    // (#40): our renderer and the SDK's code sit after the translated code, and the function
    // table's import thunks after them, so the nearest start alone put the renderer under the
    // last translated function (sub_8307D8A8 in F.E.A.R.). A leaf function has no entry: one
    // without its own counts up to 64 KB from its start.
    const RUNTIME_FUNCTION* entry = ExeFunctionEntry(ip);
    auto it = std::upper_bound(g_functions.begin(), g_functions.end(),
                               std::make_pair(ip, UINT32_MAX));
    if (it != g_functions.begin()) {
      const uintptr_t start = std::prev(it)->first;
      if (entry ? g_exe_base + entry->BeginAddress == start
                : !ExeFunctionEntry(start) && ip - start < 0x10000) {
        return std::prev(it)->second;
      }
    }
    // The exe's host code: the function holding it (a leaf: its 16-byte line).
    const uint32_t rva =
        entry ? uint32_t(entry->BeginAddress) : uint32_t(ip - g_exe_base) & ~0xFu;
    return kModuleTag | kHostCodeTag | rva;
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
  if (bucket & kHostCodeTag) {
    return fmt::format("{}+0x{:X}", ModuleName(g_exe_base), uint32_t(bucket));
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

// --- Who made a system call (#30) ---------------------------------------------------------------
// A sample in a system DLL (ntdll, win32u, kernelbase, kernel32) is named after its callers: the
// chain of other modules on the way out (hid.dll, then rexruntimerd.dll with SDL inside, then
// fear.exe's host code, ...; each module once, innermost first) and the first translated game
// function, from the return addresses on the thread's stack. The top of the stack
// is copied while the thread is suspended (ReadProcessMemory: a system call, no user-mode lock)
// and scanned after it resumes; a word counts as a return address when it points into a module
// just past a call instruction. A scan can pick up a stale return address left on the stack, so
// it is an attribution for the profile, not a stack trace.

// The executable sections of the loaded modules: (start, end, module base), sorted; refreshed with
// every second. Only code is looked at, so a pointer into a module's data isn't taken for a
// return address, and only bytes of a section the module maps are read.
struct CodeRange {
  uintptr_t start, end, module;
  bool operator<(const CodeRange& other) const { return start < other.start; }
};
std::vector<CodeRange> g_code_ranges;
uintptr_t g_system_modules[4] = {};

void RefreshModules() {
  g_code_ranges.clear();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) return;
  MODULEENTRY32W entry = {sizeof(entry)};
  for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
    const auto* base = entry.modBaseAddr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) continue;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) continue;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
      if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
      const auto start = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
      g_code_ranges.push_back(
          {start, start + section->Misc.VirtualSize, reinterpret_cast<uintptr_t>(base)});
    }
  }
  CloseHandle(snapshot);
  std::sort(g_code_ranges.begin(), g_code_ranges.end());
  const wchar_t* names[] = {L"ntdll.dll", L"win32u.dll", L"kernelbase.dll", L"kernel32.dll"};
  for (size_t i = 0; i < std::size(names); ++i) {
    g_system_modules[i] = reinterpret_cast<uintptr_t>(GetModuleHandleW(names[i]));
  }
}

// The code section holding an address, or nullptr.
const CodeRange* CodeRangeOf(uintptr_t address) {
  auto it = std::upper_bound(g_code_ranges.begin(), g_code_ranges.end(), CodeRange{address, 0, 0});
  if (it == g_code_ranges.begin()) return nullptr;
  --it;
  return address < it->end ? &*it : nullptr;
}

bool IsSystemModule(uintptr_t module) {
  return std::find(std::begin(g_system_modules), std::end(g_system_modules), module) !=
         std::end(g_system_modules);
}

// Whether the bytes before an address in a code section are a call: E8 rel32, or FF /2 (with or
// without a REX prefix or a displacement).
bool FollowsCall(uintptr_t address, const CodeRange& range) {
  if (address < range.start + 8) return false;
  const auto* code = reinterpret_cast<const uint8_t*>(address);
  if (code[-5] == 0xE8) return true;
  for (int back = 2; back <= 7; ++back) {
    if (code[-back] == 0xFF && ((code[-back + 1] >> 3) & 7) == 2) return true;
  }
  return false;
}

constexpr size_t kStackWords = 2048;  // 16 KB from the stack pointer

// A thread's stack base, from its TEB (NT_TIB.StackBase), read now: fibers switch it.
uintptr_t StackBase(uintptr_t teb) {
  uintptr_t base = 0;
  SIZE_T read = 0;
  if (!teb || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(teb + 8),
                                 &base, sizeof(base), &read) ||
      read != sizeof(base)) {
    return 0;
  }
  return base;
}

uintptr_t TebOf(HANDLE thread) {
  struct BasicInformation {
    LONG exit_status;
    PVOID teb;
    PVOID process, thread;
    ULONG_PTR affinity;
    LONG priority, base_priority;
  } info = {};
  using QueryThread = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<QueryThread>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  // 0: ThreadBasicInformation.
  if (!query || query(thread, 0, &info, sizeof(info), nullptr) < 0) return 0;
  return reinterpret_cast<uintptr_t>(info.teb);
}

// The callers of a system-DLL sample, from a copy of the stack: up to kChainModules non-system
// modules, each at its innermost return address (a bucket; host code in the exe counts as the
// module fear.exe), then the first translated function (its guest address), zeros after.
constexpr size_t kChainModules = 4;
using CallerChain = std::array<uint64_t, kChainModules + 1>;

CallerChain SystemCallers(const uint64_t* stack, size_t words) {
  CallerChain chain = {};
  size_t modules = 0;
  uintptr_t last_module = 0;
  for (size_t i = 0; i < words; ++i) {
    const uintptr_t word = uintptr_t(stack[i]);
    const CodeRange* range = CodeRangeOf(word);
    if (!range || !FollowsCall(word, *range)) continue;
    const uintptr_t module = range->module;
    const uint64_t bucket = Bucket(word);
    if (!(bucket & kModuleTag)) {
      chain[kChainModules] = uint32_t(bucket);  // translated code: the end of the chain
      break;
    }
    if (IsSystemModule(module) || module == last_module) continue;
    last_module = module;
    if (modules < kChainModules) chain[modules++] = bucket;
  }
  return chain;
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
  // System-DLL samples by (bucket, caller chain).
  uintptr_t teb = 0;
  std::map<std::pair<uint64_t, CallerChain>, uint32_t> system_callers;
};

std::string SystemCallersText(const Thread& t) {
  std::vector<std::pair<uint32_t, std::pair<uint64_t, CallerChain>>> top;
  for (const auto& [key, count] : t.system_callers) top.emplace_back(count, key);
  std::sort(top.rbegin(), top.rend());
  const uint64_t total = std::max<uint64_t>(t.samples, 1);
  std::string text;
  for (size_t i = 0; i < top.size() && i < 6 && top[i].first * 200 >= total; ++i) {
    const auto& [bucket, callers] = top[i].second;
    std::string chain = BucketName(bucket);
    for (size_t m = 0; m < kChainModules && callers[m]; ++m) chain += " < " + BucketName(callers[m]);
    if (callers[kChainModules]) chain += fmt::format(" < sub_{:08X}", uint32_t(callers[kChainModules]));
    text += fmt::format("{}{} {:.1f}%", i ? ", " : "", chain, 100.0 * top[i].first / total);
  }
  return text;
}

uint64_t ModuleKey(uint64_t bucket) {
  if (bucket & kHostCodeTag) return kModuleTag | kHostCodeTag;
  return (bucket & kModuleTag) ? bucket & ~uint64_t(0xFFFF) : GuestModuleIndex(uint32_t(bucket));
}

// A thread's samples by part of the game (see profiler.h), merged by name (Files' game client
// comes in two pieces).
std::string ModulesText(const Thread& t) {
  std::map<std::string, uint64_t> by_name;
  for (const auto& [key, count] : t.modules) {
    std::string name;
    if (key & kHostCodeTag) {
      name = ModuleName(g_exe_base) + " (host code)";
    } else if (key & kModuleTag) {
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
  RefreshModules();
  static uint64_t stack_copy[kStackWords];
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
      // And the modules' code sections, for the stack scans: a DLL unloaded since would have its
      // code read for at most a second.
      RefreshModules();
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
          t.teb = TebOf(handle);
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
      // Outside the exe: the top of its stack too, for the system call's callers (a system call
      // reads it: no user-mode lock).
      size_t stack_words = 0;
      if (sampled && (context.Rip < g_exe_base || context.Rip >= g_exe_end)) {
        const uintptr_t base = StackBase(t.teb);
        if (base > context.Rsp && base - context.Rsp < (uintptr_t(64) << 20)) {
          SIZE_T read = 0;
          if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(context.Rsp),
                                stack_copy,
                                std::min<size_t>(base - context.Rsp, sizeof(stack_copy)), &read)) {
            stack_words = read / sizeof(uint64_t);
          }
        }
      }
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
        const uintptr_t module = uintptr_t(bucket & ~kModuleTag & ~uint64_t(0xFFFF));
        if (stack_words && (bucket & kModuleTag) && !(bucket & kHostCodeTag) && module &&
            IsSystemModule(module)) {
          ++t.system_callers[{bucket, SystemCallers(stack_copy, stack_words)}];
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
        if (!row.t->system_callers.empty()) {
          REXLOG_INFO("[profile]       system calls from: {}", SystemCallersText(*row.t));
        }
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
        t.system_callers.clear();
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
