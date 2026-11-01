// F.E.A.R. 360 - starting the game again, or the other game of the collection: Recompilation
// Settings' Save and restart, Switch Game, picking a title in the collection menu.
//
// A new run is told the old one's process id (fear_restart_wait_pid) and waits for it to exit
// before it starts (WaitForPreviousRun, from ReXApp::OnConfigurePaths), so the two never hold the
// display, the save files or the log at the same time.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fear::process {

// This program and the arguments it was started with (UTF-8, without the program).
std::filesystem::path OwnExecutable();
std::vector<std::string> OwnArguments();

// The arguments without the given flags (as --name value, --name=value, --name or --no-name) and
// with the log file renamed "<name>-restart", so the new run doesn't write over this one's log.
std::vector<std::string> WithoutFlags(const std::vector<std::string>& arguments,
                                      const std::vector<std::string>& flags);

// Starts a program that waits for this run to exit (the arguments get --fear_restart_wait_pid).
// False if it couldn't be started.
bool StartAfterThis(const std::filesystem::path& executable, std::vector<std::string> arguments,
                    const std::filesystem::path& working_dir);

// Blocks until the run named by fear_restart_wait_pid has exited (at most 30 s).
void WaitForPreviousRun();

}  // namespace fear::process
