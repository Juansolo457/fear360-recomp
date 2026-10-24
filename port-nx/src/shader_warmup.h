// F.E.A.R. 360 - the shader warm-up (#53): the shaders and pipelines seen in play, built before the
// game starts instead of the first time a draw needs them.
//
// On a new PC, or after a graphics driver update, the first run hitched badly: each shader was
// translated and each pipeline compiled by the driver the first time something drew with it, so
// the first load looked about to crash and then everything popped in, and each new area did it
// again. Later runs were smooth: the renderer remembers what it built (its prewarm file, per GPU)
// and the driver keeps the pipelines (the driver cache). A new PC had neither.
//
// Before the game's first GPU command, the renderer translates everything its prewarm file has seen
// and builds every pipeline on all cores, while the game waits (Renderer::RunWarmup). If that takes
// more than a moment, a panel shows the progress. With the driver's cache warm it takes a fraction
// of a second.
//
// A build can also carry a warm-up list per game, <project>/warmup/shader_warmup.inc, which
// tools/make_warmup.py generates locally from the player's own game files and prewarm files, for
// example to give a second PC (the laptop, the Deck) what the first one has played. It is derived
// from the game (shader hashes, where each shader is in the game's files, the dwords the game's D3D
// patches into a vertex shader for its vertex layout, the pipelines' keys), so it is not tracked
// and never published: the build compiles in an empty list when there is none
// (cmake/warmup_list.cmake). Load rebuilds the listed shaders from the player's own files and keeps
// those that match their hashes; the renderer adds them to its prewarm list.

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
