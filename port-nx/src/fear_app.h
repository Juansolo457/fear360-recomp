// F.E.A.R. (Xbox 360) - ReXGlue application, port-nx flavour.
//
// Two renderers are compiled in: fear-nx's native Vulkan Xenos renderer
// (src/native/, --nx_renderer native) and the SDK's stock Xenos plugin, which
// is linked into this executable (REXGLUE_GPU_XENOS_STATIC) rather than loaded
// as rexgpu-xenos.dll, so its factory is called directly.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>

#include "crash_report.h"
#include "native/native_system.h"

REXCVAR_DEFINE_STRING(fear_gpu_backend, "any", "F.E.A.R.",
                      "Backend of the stock Xenos plugin when nx_renderer is 'xenos': "
                      "any (d3d12 first), d3d12 or vulkan")
    .allowed({"any", "d3d12", "vulkan"})
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

    // Either way the DLL loader must not run: there is no plugin file.
    config.gpu_plugin.clear();

    if (fear_nx::native::Enabled()) {
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
};
