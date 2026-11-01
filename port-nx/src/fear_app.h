// F.E.A.R. (Xbox 360) - ReXGlue application, port-nx flavour.
//
// One renderer: the D3D12 renderer (src/d3d12/, docs/NATIVE_RENDERER.md), which draws the game
// from its Direct3D calls on D3D12 or Vulkan (docs/VULKAN_RENDERER.md), on fear-nx's graphics
// system (src/native/), which runs the GPU ring and ReXGlue's presenter of that API. The SDK's
// Xenos plugin is still linked in
// (REXGLUE_GPU_XENOS_STATIC) for the shader translator and texture code the renderer uses, but
// never drives the game.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

#include "crash_report.h"
#include "display_vblank.h"
#include "fear_collection.h"
#include "fear_menu.h"
#include "fear_mkb.h"
#include "fear_panel.h"
#include "fear_settings.h"
#include "fear_title.h"
#include "fear_version.h"
#include "look_tuning.h"
#include "native/native_system.h"
#include "d3d12/renderer.h"  // RendererApi
#include "pipeline_warmup.h"
#include "profiler.h"

// The runtime signs in only player 1 (a local profile; Xbox Live is gone), and gives controllers
// player slots in connection order. With another XInput device connected first (here a Logitech
// one), the Xbox pad was player 2: not signed in, so the game played without a profile and never
// saved. Single player wants every controller on player 1.
REXCVAR_DEFINE_BOOL(fear_shared_controllers, true, "F.E.A.R.",
                    "Every connected controller drives player 1, the signed-in profile that can "
                    "save; off = one controller per player slot (the keyboard and mouse drive "
                    "player 1 either way)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class FearApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FearApp>(new FearApp(ctx, "fear",
        PPCImageConfig));
  }

 protected:
  // Recompilation Settings (fear_settings.h): saved in the user data folder, loaded below the
  // command line.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    fear::settings::Load(paths.user_data_root);
#if defined(_WIN32)
    // Controllers through XInput on native Windows, as the collection menu reads them: the SDL
    // driver found no usable pad in the game on a Windows 11 laptop (#54). Under Proton the
    // Steam Deck keeps SDL, its tested path. A backend chosen in settings.toml or on the command
    // line wins.
    if (!fear::display_vblank::UnderWine() &&
        rex::cvar::GetFlagSource("input_backend") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("input_backend", "xinput");
      REXLOG_INFO("F.E.A.R.: controllers through XInput (input_backend; \"sdl\" for SDL)");
    }
#endif
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { fear::panel::AddFonts(atlas); }

  // Which build this is (fear_version.h): the first line of the log, the window title and the
  // corner badge, before the collection menu opens.
  void OnPostInitLogging() override {
    REXLOG_INFO("{} ({})", fear::version::Title(), fear::title::kFiles ? "F.E.A.R. Files" : "F.E.A.R.");
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    if (window()) {
      window()->SetTitle(std::string(fear::title::kFiles ? "F.E.A.R. Files" : "F.E.A.R.") +
                         " - " + fear::version::Title());
    }
    fear::version::CreateBadge(drawer);
  }

  // The collection menu (fear_collection.h), before any game loads: with the window and ImGui up,
  // no runtime yet. Picking this program's game resumes here.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override {
    // First start (not on a Steam Deck): keyboard and mouse or a controller (fear_settings.h).
    if (fear::settings::AskInput(PanelHost(), [this, defaults, resume] {
          if (fear::collection::StartNow(PanelHost(), [defaults, resume] { resume(defaults); })) {
            resume(defaults);
          }
        })) {
      return std::nullopt;
    }
    if (fear::collection::StartNow(PanelHost(), [defaults, resume] { resume(defaults); })) {
      return defaults;
    }
    return std::nullopt;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    fear::crash_report::InstallCrashReporter();

    if (config.input_factory) {
      config.input_factory = [base = config.input_factory](bool tool_mode) {
        auto input = base(tool_mode);
        if (auto* system = dynamic_cast<rex::input::InputSystem*>(input.get())) {
          // The controllers' assignment, under the keyboard and mouse's (fear_mkb.h: in play, one
          // or the other).
          std::unique_ptr<rex::input::DeviceAssignment> pads;
          if (REXCVAR_GET(fear_shared_controllers)) {
            pads = std::make_unique<rex::input::SharedAssignment>();
            REXLOG_INFO("F.E.A.R.: every controller drives player 1 (the profile that saves)");
          } else {
            pads = std::make_unique<rex::input::SlotAssignment>();
          }
          system->SetDeviceAssignment(fear::mkb::Assign(std::move(pads)));
        }
        return input;
      };
    }

    // Either way the DLL loader must not run: there is no plugin file.
    config.gpu_plugin.clear();

    fear::display_vblank::Configure();
    config.graphics = fear_nx::native::CreateGraphicsSystem();
    REXLOG_INFO("F.E.A.R.: the D3D12 renderer on {}, with ReXGlue's presenter",
                fear::d3d12::RendererApi());
  }

  void OnPostSetup() override {
    if (runtime()) fear::look_tuning::Apply(*runtime());
    fear::profiler::Start();

    // The pipeline warm-up (pipeline_warmup.h): the renderer runs it before the game's
    // first draw; the panel shows on the UI thread.
    fear::pipeline_warmup::SetBeginCallback([this] {
      app_context().CallInUIThreadDeferred(
          [this] { fear::pipeline_warmup::OpenPanel(imgui_drawer()); });
    });

    // The game gets no input while a panel (or an SDK overlay under the mouse) has it.
    if (auto* input = runtime() ? static_cast<rex::input::InputSystem*>(runtime()->input_system())
                                : nullptr) {
      // Keyboard and mouse, PC style (fear_mkb.h): before the callback, which reaches only the
      // drivers already there.
      fear::mkb::Install(*input, window());
      input->SetActiveCallback([this] {
        if (!fear::panel::GameGetsInput()) return false;
        // WantCaptureMouse is ImGui's answer from its last frame, and with no dialog open ImGui
        // draws no frames: the last one's answer (the pointer over a panel as it closed) would
        // hold for good, and the game would get no input, the mouse never captured for mouse
        // look. Only an open dialog's answer counts. Logged as it changes.
        auto* drawer = imgui_drawer();
        const bool wants = drawer && drawer->GetIO().WantCaptureMouse;
        const bool dialogs = drawer && drawer->HasDialogs();
        static std::atomic<int> logged{0};  // 0: nothing to say, 1: an overlay has it, 2: stale
        const int now = !wants ? 0 : dialogs ? 1 : 2;
        if (logged.exchange(now) != now) {
          REXLOG_INFO("F.E.A.R. input: {}",
                      now == 1   ? "an ImGui window has the mouse; the game waits"
                      : now == 2 ? "ImGui's last frame still claims the mouse, with nothing open: "
                                   "ignored"
                                 : "ImGui has let go of the mouse");
        }
        return now != 1;
      });
    }

    // Recompilation Settings, Switch Game and Quit to Desktop in the main menu (fear_menu.h).
    fear::menu::Actions actions;
    actions.settings = [this] { OpenPanel(&fear::settings::OpenSettings); };
    actions.quit = [this] { OpenPanel(&fear::settings::OpenQuit); };
    actions.switch_game = [this] {
      app_context().CallInUIThread([this] {
        if (fear::collection::SwitchGame() && window()) window()->RequestClose();
      });
    };
    fear::menu::SetActions(std::move(actions));
  }

 private:
  fear::panel::Host PanelHost() {
    fear::panel::Host host;
    host.drawer = imgui_drawer();
    host.later = [this](std::function<void()> function) {
      app_context().CallInUIThreadDeferred(std::move(function));
    };
    host.quit = [this] {
      if (window()) window()->RequestClose();
    };
    return host;
  }

  // From the game thread that handles the menu command.
  void OpenPanel(void (*open)(const fear::settings::Host&)) {
    app_context().CallInUIThread([this, open] { open(PanelHost()); });
  }
};
