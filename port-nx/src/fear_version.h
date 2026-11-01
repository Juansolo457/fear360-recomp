// F.E.A.R. 360 - which build this is, on screen and in the log.
//
// The release number is the repository's VERSION file; the build adds where its source stands
// against that release's tag (port-nx/cmake/fear_version.cmake, run before every build): "0.1.5"
// for the release itself (a source download), "0.1.5 af9770c" built from the tag, "0.1.5+3
// 121a788" three commits later, "... modified" with uncommitted changes.
//
// It shows in the window title, at the top of the collection menu and Recompilation Settings, in
// the first line of each log, and in a corner badge over the game. fear_version_badge picks when
// the badge shows: "launch" (the first 30 seconds), "always" or "off".

#pragma once

#include <string>

namespace rex::ui {
class ImGuiDrawer;
}

namespace fear::version {

// "0.1.5"
const char* Number();
// "0.1.5+3 121a788"
const char* Text();
// "F.E.A.R. 360 Recompiled 0.1.5+3 121a788"
std::string Title();

// In ReXApp::OnCreateDialogs: the corner badge (fear_version_badge).
void CreateBadge(rex::ui::ImGuiDrawer* drawer);

}  // namespace fear::version
