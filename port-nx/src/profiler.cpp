// F.E.A.R. 360 - a sampling profiler for where the CPU time goes. See profiler.h.

#include "profiler.h"

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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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

// A sample's bucket: a guest function (its address), or a module (tagged with the top bit set and
// the module's base, its name looked up when reporting).
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
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(ip), &module);
  return kModuleTag | reinterpret_cast<uint64_t>(module);
}

std::string BucketName(uint64_t bucket) {
  char text[MAX_PATH + 16];
  if (!(bucket & kModuleTag)) {
    std::snprintf(text, sizeof(text), "sub_%08X", uint32_t(bucket));
    return text;
  }
  const auto module = reinterpret_cast<HMODULE>(bucket & ~kModuleTag);
  if (!module) return "(unknown)";
  char path[MAX_PATH] = {};
  GetModuleFileNameA(module, path, MAX_PATH);
  const char* name = std::strrchr(path, '\\');
  return name ? name + 1 : path;
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
  if (name.empty()) name = "thread " + std::to_string(id);
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
};

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
      if (GetThreadContext(t.handle, &context)) {
        ++t.samples;
        ++t.buckets[Bucket(uintptr_t(context.Rip))];
      }
      ResumeThread(t.handle);
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
  std::thread([] {
    SetThreadDescription(GetCurrentThread(), L"F.E.A.R. profiler");
    Run();
  }).detach();
}

}  // namespace fear::profiler

#else

namespace fear::profiler {
void Start() {}
}  // namespace fear::profiler

#endif
