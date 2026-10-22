// fear - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

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
    // Without a GPU plugin the runtime ignores every Vd* call and the game
    // never draws. "xenos" is the SDK's Xenos -> D3D12/Vulkan translator; a
    // --gpu_plugin on the command line still takes precedence.
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
