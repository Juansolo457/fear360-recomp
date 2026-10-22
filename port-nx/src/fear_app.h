// F.E.A.R. (Xbox 360) - ReXGlue application, port-nx flavour.
//
// Two renderers are compiled in: fear-nx's native Vulkan Xenos renderer
// (src/native/, --nx_renderer native) and the SDK's stock Xenos plugin, which
// is linked into this executable (REXGLUE_GPU_XENOS_STATIC) rather than loaded
// as rexgpu-xenos.dll, so its factory is called directly.

#pragma once

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>

#include "crash_report.h"
#include "display_vblank.h"
#include "look_tuning.h"
#include "native/native_system.h"

REXCVAR_DEFINE_STRING(fear_gpu_backend, "any", "F.E.A.R.",
                      "Backend of the stock Xenos plugin when nx_renderer is 'xenos': "
                      "any (d3d12 first), d3d12 or vulkan")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The runtime signs in only player 1 (a local profile; Xbox Live is gone), and gives controllers
// player slots in connection order. With another XInput device connected first (here a Logitech
// one), the Xbox pad was player 2: not signed in, so the game played without a profile and never
// saved. Single player wants every controller on player 1.
REXCVAR_DEFINE_BOOL(fear_shared_controllers, true, "F.E.A.R.",
                    "Every connected controller (and the keyboard) drives player 1, the "
                    "signed-in profile that can save; off = one controller per player slot")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// plugin_main.cpp, linked statically.
extern "C" uint32_t rex_gpu_abi_version(void);
extern "C" rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
                                                         const rex::system::GpuCreateInfo* info);

class FearApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FearApp>(new FearApp(ctx, "fear",
        PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    fear::crash_report::InstallCrashReporter();

    if (REXCVAR_GET(fear_shared_controllers) && config.input_factory) {
      config.input_factory = [base = config.input_factory](bool tool_mode) {
        auto input = base(tool_mode);
        if (auto* system = dynamic_cast<rex::input::InputSystem*>(input.get())) {
          system->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());
          REXLOG_INFO("F.E.A.R.: every controller drives player 1 (the profile that saves)");
        }
        return input;
      };
    }

    // Either way the DLL loader must not run: there is no plugin file.
    config.gpu_plugin.clear();

    if (fear_nx::native::Enabled()) {
      fear::display_vblank::Configure();
      config.graphics = fear_nx::native::CreateGraphicsSystem();
      REXLOG_INFO("F.E.A.R.: using fear-nx's native Vulkan Xenos renderer");
      return;
    }

    if (rex_gpu_abi_version() != rex::system::kGpuPluginAbiVersion) {
      REXLOG_ERROR("F.E.A.R.: linked Xenos plugin has ABI {}, host expects {}",
                   rex_gpu_abi_version(), rex::system::kGpuPluginAbiVersion);
      return;
    }
    const std::string backend = REXCVAR_GET(fear_gpu_backend);
    rex::system::GpuCreateInfo info{};
    info.struct_size = sizeof(info);
    info.backend = backend.c_str();
    config.graphics.reset(rex_gpu_create(rex::system::kGpuPluginAbiVersion, &info));
    if (config.graphics) {
      REXLOG_INFO("F.E.A.R.: using the stock Xenos plugin, backend '{}'", backend);
    } else {
      REXLOG_ERROR("F.E.A.R.: the stock Xenos plugin refused backend '{}'", backend);
    }
  }

  void OnPostSetup() override {
    if (runtime()) fear::look_tuning::Apply(*runtime());
  }
};
