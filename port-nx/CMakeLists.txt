# fear (port-nx): F.E.A.R. on fear-nx's patched ReXGlue SDK with its Vulkan Xenos renderer.
#
# Differences from port/: the SDK is built from source (REXSDK_DIR, see
# tools/build-nx.ps1), the Xenos GPU plugin is linked into the executable whole
# instead of loaded as a DLL, and src/native/ (fear-nx, GPL-3.0) is compiled in.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fear LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT REXSDK_DIR)
    message(FATAL_ERROR "port-nx builds the SDK from source: pass -DREXSDK_DIR=<third_party/rexglue-sdk-nx>.")
endif()
if(NOT REXGLUE_GPU_XENOS_STATIC)
    message(FATAL_ERROR "port-nx links the Xenos plugin into fear.exe: pass -DREXGLUE_GPU_XENOS_STATIC=ON.")
endif()

# No floating-point contraction: built for x86-64-v3 (tools/build-nx.ps1), clang's default
# -ffp-contract=on fuses a*b+c into one FMA, rounding once where the PowerPC rounded twice. The
# SDK sets -ffp-model=strict for its own targets only; this covers the translated code, the
# renderer and the PCH. The generator's explicit std::fma still becomes a single vfmadd.
add_compile_options($<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-contract=off>)

include(generated/rexglue.cmake)

# fear-nx's renderer (unchanged copy; see src/native/UPSTREAM.md). The tests
# next to it need their own harness and are not built.
set(FEAR_NATIVE_SOURCES
    src/native/native_system.cpp
    src/native/native_renderer.cpp
    src/native/native_textures.cpp
    src/native/native_geometry_shaders.cpp
)

set(FEAR_SOURCES
    src/main.cpp
    src/anim_timing.cpp
    src/high_fps.cpp
    src/profiler.cpp
    src/texture_pack.cpp
    src/crash_report.cpp
    src/display_vblank.cpp
    src/fear_collection.cpp
    src/fear_menu.cpp
    src/fear_panel.cpp
    src/fear_process.cpp
    src/fear_settings.cpp
    src/fx_debug.cpp
    src/look_tuning.cpp
    ${FEAR_NATIVE_SOURCES}
)

add_executable(fear WIN32 ${FEAR_SOURCES})

rexglue_setup_target(fear)

# The renderer is written against the plugin's internals (shader translator,
# texture conversion, register file, ...), which the DLL build keeps private.
# Link the static plugin whole so its cvars and rex_gpu_create survive too.
target_link_libraries(fear PRIVATE rexgpu-xenos xxHash::xxhash)
if(WIN32)
    target_link_libraries(fear PRIVATE dbghelp)   # crash reports (src/crash_report.cpp)
    target_link_libraries(fear PRIVATE dxgi)      # the display's vblank (src/display_vblank.cpp)
endif()
target_link_options(fear PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:rexgpu-xenos>")
set_property(TARGET fear APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:rexgpu-xenos>)
target_include_directories(fear PRIVATE
    "${REXSDK_DIR}/src/graphics"
    "${REXSDK_DIR}/thirdparty/glslang"
    "${REXSDK_DIR}/thirdparty/renderdoc"   # rex/ui/renderdoc_api.h, private to the plugin
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)

# Offline texture pack indexer (tools/texture_pack.py runs it): the SDK's own layout and
# untiling code and the renderer's pack key, so offline and runtime keys agree by construction.
add_executable(fear_texindex tools/texindex.cpp src/texture_pack.cpp)
target_include_directories(fear_texindex PRIVATE src "${REXSDK_DIR}/src/graphics")
target_link_libraries(fear_texindex PRIVATE rexgpu-xenos rex::runtime xxHash::xxhash)
add_dependencies(fear fear_texindex)
