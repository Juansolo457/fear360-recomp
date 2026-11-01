// F.E.A.R. 360 - the pipeline warm-up's progress panel. See pipeline_warmup.h.

#include "pipeline_warmup.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

#include <fmt/format.h>
#include <imgui.h>

#include "fear_panel.h"

namespace {

std::atomic<uint32_t> g_total{0}, g_done{0};
std::atomic<int64_t> g_begun{0};  // steady_clock ticks
std::atomic<bool> g_active{false};
std::mutex g_callback_mutex;
std::function<void()> g_on_begin;

class WarmupPanel final : public fear::panel::Panel {
 public:
  explicit WarmupPanel(rex::ui::ImGuiDrawer* drawer) : Panel(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    if (!g_active.load()) {
      if (!closing_) {
        closing_ = true;
        Close();
      }
      return;
    }
    // Nothing for the first half second: with the driver's pipeline library warm it is done by
    // then.
    const auto begun = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(g_begun.load()));
    if (std::chrono::steady_clock::now() - begun < std::chrono::milliseconds(500)) return;
    const uint32_t total = std::max(g_total.load(), 1u);
    const uint32_t done = std::min(g_done.load(), total);
    if (fear::panel::BeginPanel("##fear_warmup", 26.0f)) {
      ImGui::TextUnformatted("PREPARING SHADERS");
      ImGui::Separator();
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.62f, 0.10f, 0.08f, 0.95f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
      ImGui::ProgressBar(float(done) / float(total), ImVec2(-1.0f, 0.0f),
                         fmt::format("{} / {}", done, total).c_str());
      ImGui::PopStyleColor(2);
      fear::panel::Dim(
          "Building shaders for your graphics card. This happens only once.");
    }
    fear::panel::EndPanel();
  }

 private:
  bool closing_ = false;
};

}  // namespace

namespace fear::pipeline_warmup {

void Begin(uint32_t total) {
  g_total = total;
  g_done = 0;
  g_begun = std::chrono::steady_clock::now().time_since_epoch().count();
  g_active = true;
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    callback = g_on_begin;
  }
  if (callback) callback();
}

void Advance(uint32_t done) { g_done = done; }

void End() { g_active = false; }

void SetBeginCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_on_begin = std::move(callback);
}

void OpenPanel(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || !g_active.load()) return;
  new WarmupPanel(drawer);  // deletes itself when closed (ImGuiDialog)
}

}  // namespace fear::pipeline_warmup
