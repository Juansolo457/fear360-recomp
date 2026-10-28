// F.E.A.R. 360 - profile-guided optimization, the training run (#87).
//
// tools/build-linux.sh with FEAR_PGO=generate builds the game instrumented (-fprofile-generate)
// and defines FEAR_PGO_GENERATE. The profile runtime writes its counters at exit, but the game
// never exits normally: ReXGlue's window close ends in std::_Exit (rex_app.cpp, OnClosing), and
// Switch Game starts the other program and leaves the same way. So a thread writes them every
// 20 s instead, each time the whole of this process's counts so far, into its own file
// (pgo-raw/<pid>.profraw beside the executable; no %m, so writes replace, not add). A session
// loses at most its last 20 s. The folder is made here: the runtime doesn't make it for a name
// set this way (the first Deck training lost F.E.A.R. Files' counts to that).
// tools/pgo-merge.sh merges the files into pgo/<project>/default.profdata, which the next build
// uses.
#if defined(FEAR_PGO_GENERATE)

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char* name);

namespace {

struct ProfileWriter {
  ProfileWriter() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::read_symlink("/proc/self/exe", ec);
    dir = ec ? std::filesystem::path("pgo-raw") : dir.parent_path() / "pgo-raw";
    std::filesystem::create_directories(dir, ec);
    // The runtime keeps the pointer: a string that lives as long as the process.
    static const std::string name = (dir / "%p.profraw").string();
    __llvm_profile_set_filename(name.c_str());
    std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        __llvm_profile_write_file();
      }
    }).detach();
  }
};

const ProfileWriter g_writer;

}  // namespace

#endif
