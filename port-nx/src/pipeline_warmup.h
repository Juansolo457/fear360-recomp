// F.E.A.R. 360 - the pipeline warm-up's progress panel.
//
// The renderer (src/d3d12/draw.cpp and vulkan_backend.cpp, fear_d3d12_warmup) builds every
// pipeline earlier runs stored before the game's first draw, on all its worker threads, while the
// game waits: on a new PC, or after a graphics driver update, the first scenes would otherwise
// wait for each one the first time something draws with it (40 ms and up on the Steam Deck). With
// the driver's pipeline cache warm it takes a moment. If it takes more than half a second, a panel
// shows the progress.
// (fear-nx's renderer had the same warm-up for its own pipelines, with this panel: shader_warmup.*
// in the history.)

#pragma once

#include <cstdint>
#include <functional>

namespace rex::ui {
class ImGuiDrawer;
}

namespace fear::pipeline_warmup {

// The renderer's progress, from its thread: Begin calls the callback set below.
void Begin(uint32_t total);
void Advance(uint32_t done);
void End();

// In FearApp: what Begin calls (on the renderer's thread), to open the panel on the UI thread.
void SetBeginCallback(std::function<void()> callback);

// On the UI thread: the progress panel. It shows after half a second and closes when done.
void OpenPanel(rex::ui::ImGuiDrawer* drawer);

}  // namespace fear::pipeline_warmup
