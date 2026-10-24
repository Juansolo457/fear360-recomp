// F.E.A.R. 360 - the shader warm-up (#53): the shaders and pipelines seen in play, built before the
// game starts instead of the first time a draw needs them.
//
// On a new PC, or after a graphics driver update, the first run hitched badly: each shader was
// translated and each pipeline compiled by the driver the first time something drew with it, so
// the first load looked about to crash and then everything popped in, and each new area did it
// again. Later runs were smooth: the renderer remembers what it built (its prewarm file, per GPU)
// and the driver keeps the pipelines (the driver cache). A new PC had neither.
//
// So this build carries a list per game, <project>/warmup/shader_warmup.inc, written by
// tools/make_warmup.py from prewarm files recorded in play. For each shader it says where the game
// keeps it (a compiled effect in Game/uberCatalog.cat, or the executable), which dwords the game's
// D3D patches into a vertex shader for its vertex layout, and its translation variants; then the
// pipelines' keys. No shader code: Load rebuilds the shaders from the player's own files and keeps
// those that match their hashes. The renderer adds them to its prewarm list, and before the game's
// first GPU command translates everything and builds every pipeline on all cores, while the game
// waits (Renderer::RunWarmup). If that takes more than a moment, a panel shows the progress. With
// the driver's cache warm it takes a fraction of a second.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace rex::ui {
class ImGuiDrawer;
}

namespace fear::shader_warmup {

// fear_shader_warmup: off = the list isn't used and nothing is built before play (as before).
bool Enabled();

struct Shader {
  uint64_t hash = 0;  // XXH3 of the microcode as in guest memory
  uint32_t type = 0;  // xenos::ShaderType: 0 vertex, 1 pixel
  std::vector<uint32_t> ucode;  // as in guest memory (big-endian words)
  std::vector<uint64_t> modifications;
};

struct List {
  std::vector<Shader> shaders;
  std::vector<std::vector<uint32_t>> pipelines;  // PipelineKey, as 32-bit words
  uint32_t key_size = 0;                         // the recording build's sizeof(PipelineKey)
};

// In FearApp::OnPostSetup: the game data folder, which has Game/uberCatalog.cat.
void SetGameDataRoot(const std::filesystem::path& root);

// This build's list for the title, its shaders rebuilt from the game data and the executable (in
// guest memory, so after the module has loaded). Logs what it found.
List Load(uint32_t title_id);

// The renderer's progress, from its ring thread: Begin calls the callback set below.
void Begin(uint32_t total);
void Advance(uint32_t done);
void End();

// In FearApp: what Begin calls (on the ring thread), to open the panel on the UI thread.
void SetBeginCallback(std::function<void()> callback);

// On the UI thread: the progress panel. It shows after half a second and closes when done.
void OpenPanel(rex::ui::ImGuiDrawer* drawer);

}  // namespace fear::shader_warmup
