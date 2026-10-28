// D3D12 renderer: the cvars both backends and the renderer host read, compiled on every
// platform (the D3D12 renderer's own are in its files, the Vulkan backend's in
// vulkan_backend.cpp).
#include <rex/cvar.h>

#include <cstdint>
#include <string>

REXCVAR_DEFINE_BOOL(fear_d3d12_async_pipelines, true, "F.E.A.R. D3D12",
                    "Compile new pipeline states on worker threads, the frame's draws that need "
                    "them skipped, instead of compiling each at its draw");
REXCVAR_DEFINE_INT32(fear_d3d12_pipeline_threads, 0, "F.E.A.R. D3D12",
                     "Pipeline compile threads with --fear_d3d12_async_pipelines (0: a quarter "
                     "of the hardware threads, 1 to 4)");
REXCVAR_DEFINE_BOOL(fear_d3d12_pipeline_wait, true, "F.E.A.R. D3D12",
                    "With --fear_d3d12_async_pipelines, a frame whose draws need pipelines still "
                    "compiling is not shown and waits for them at its end, up to 0.5 s (false: "
                    "show it without those draws)");
REXCVAR_DEFINE_BOOL(fear_d3d12_pipeline_cache, true, "F.E.A.R. D3D12",
                    "Keep the shaders and pipelines the native renderer creates under "
                    "<cache_root>/fear-d3d12 (Vulkan: fear-d3d12-vulkan; -gradients with "
                    "--no-fear_d3d12_implicit_gradients) and compile them at start-up in later "
                    "runs (prewarm, plus a D3D12 pipeline library)");
REXCVAR_DEFINE_STRING(fear_d3d12_watch_trace, "", "F.E.A.R. D3D12",
                      "Binary log of vertex write-watch events (arms and faults, one 64-bit "
                      "record each, see WatchTrace) for offline analysis of the watch policy");
REXCVAR_DEFINE_BOOL(fear_d3d12_coherency_check, false, "F.E.A.R. D3D12",
                    "Debug: keep a CPU copy of what shared memory holds and compare the vertex and "
                    "index ranges draws read with guest memory, per request and again at the Swap "
                    "(stale pages: a write the page watch missed; late pages: data rewritten after "
                    "a draw of the frame read it), and check copy segments against every read. "
                    "Slow (512 MB of host memory, a memcmp of every range read)");
REXCVAR_DEFINE_BOOL(fear_vertex_index_range, false, "F.E.A.R. D3D12",
                    "Experiment (#40): copy each vertex range only as far as the draw's vertices "
                    "reach (its index bounds), not the whole vertex buffer, for shaders that fetch "
                    "with the plain vertex index; fewer pages hashed in big fights");
REXCVAR_DEFINE_BOOL(fear_d3d12_implicit_gradients, true, "F.E.A.R. D3D12",
                    "Experiment (Steam Deck shimmer): translate computed-LOD texture fetches in "
                    "pixel shaders with sample_b and the GPU's own derivatives (pgr4-native 0021; "
                    "the Vulkan renderer's SPIR-V shaders too, with OpImageSampleImplicitLod); "
                    "false: explicit coarse gradients, as before 0021");
REXCVAR_DEFINE_BOOL(fear_d3d12_luma_probe, false, "F.E.A.R. D3D12",
                    "Read back three rows of every front buffer (at 10, 40 and 80 % of its "
                    "height) and log their mean brightness in the frame CSV, to find frames "
                    "drawn partly black");
REXCVAR_DEFINE_BOOL(fear_d3d12_msaa, true, "F.E.A.R. D3D12",
                    "Draw with the multisampling the game asks for (its 2x or 4x MSAA); off: "
                    "one sample per pixel, which costs the GPU much less (the Steam Deck's "
                    "baseline)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(fear_d3d12_anisotropy, 1, "F.E.A.R. D3D12",
                     "Anisotropic filtering of the game's linearly filtered textures with mips, "
                     "at least this many samples per pixel (2, 4, 8 or 16; 1: the game's own)");
// The name fear-nx's renderer had, so a settings file keeps its choice.
REXCVAR_DEFINE_DOUBLE(fear_mip_bias, -0.5, "F.E.A.R. D3D12",
                      "Texture sharpness (#50, EchoPatch's improved mipmap bias): a mip level bias "
                      "added to every sampler, on top of the game's own; below 0 = sharper distant "
                      "textures (at some cost in shimmer), 0 = as shipped")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(fear_d3d12_warmup, true, "F.E.A.R. D3D12",
                    "Before the game's first draw, build every pipeline earlier runs stored, on "
                    "all the pipeline threads, while a panel shows the progress "
                    "(src/pipeline_warmup.h); off: a slice of them per frame in the background, "
                    "which the first scenes can still wait for")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
