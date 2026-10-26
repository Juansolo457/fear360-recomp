// F.E.A.R. 360 - which build this is. See fear_version.h.

#include "fear_version.h"

#include <chrono>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "fear_panel.h"
#include "fear_version_info.h"  // generated: port-nx/cmake/fear_version.cmake

REXCVAR_DEFINE_STRING(fear_version_badge, "launch", "F.E.A.R.",
                      "The build's version in a corner of the screen: launch (the first 30 "
                      "seconds), always, off")
    .allowed({"launch", "always", "off"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

constexpr auto kLaunchTime = std::chrono::seconds(30);

class Badge final : public rex::ui::ImGuiDialog {
 public:
  explicit Badge(rex::ui::ImGuiDrawer* drawer)
      : ImGuiDialog(drawer), shown_(std::chrono::steady_clock::now()) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (closing_) return;
    // Gone once its time is up: an open dialog keeps the ImGui overlay drawing every frame.
    static const bool always = REXCVAR_GET(fear_version_badge) == "always";
    if (!always && std::chrono::steady_clock::now() - shown_ > kLaunchTime) {
      closing_ = true;
      Close();
      return;
    }
    const float pad = fear::panel::FontSize() * 0.4f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, io.DisplaySize.y - pad),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.45f);
    fear::panel::PushFont(0.55f);
    if (ImGui::Begin("##fear_version", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(fear::version::Title().c_str());
    }
    ImGui::End();
    fear::panel::PopFont();
  }

 private:
  std::chrono::steady_clock::time_point shown_;
  bool closing_ = false;
};

}  // namespace

namespace fear::version {

const char* Number() { return FEAR_VERSION_NUMBER; }

const char* Text() { return FEAR_VERSION_TEXT; }

std::string Title() { return std::string("F.E.A.R. 360 Recompiled ") + Text(); }

void CreateBadge(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || REXCVAR_GET(fear_version_badge) == "off") return;
  new Badge(drawer);  // deletes itself when closed (ImGuiDialog)
}

}  // namespace fear::version
