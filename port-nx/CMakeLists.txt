# fear (port-nx): F.E.A.R. on fear-nx's patched ReXGlue SDK with the D3D12 renderer.
#
# Differences from port/: the SDK is built from source (REXSDK_DIR, see
# tools/build-nx.ps1 and tools/build-linux.sh), the Xenos GPU plugin is linked into the
# executable whole instead of loaded as a DLL, and src/native/ (fear-nx's GPU ring, GPL-3.0) and
# src/d3d12/ (the renderer, MIT: D3D12 and Vulkan on Windows, Vulkan only on Linux) are compiled
# in. SDK boilerplate lives in generated/rexglue.cmake.

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
if(NOT WIN32 AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"))
    message(FATAL_ERROR "port-nx builds for Windows and Linux x86-64 (docs/VULKAN_RENDERER.md).")
endif()
if(NOT REXGLUE_USE_VULKAN OR (WIN32 AND NOT REXGLUE_USE_D3D12))
    message(FATAL_ERROR "port-nx needs the SDK's Vulkan backend, and on Windows its D3D12 one too: "
        "pass -DREXGLUE_USE_VULKAN=ON (and -DREXGLUE_USE_D3D12=ON on Windows).")
endif()

# No floating-point contraction: built for x86-64-v3 (tools/build-nx.ps1), clang's default
# -ffp-contract=on fuses a*b+c into one FMA, rounding once where the PowerPC rounded twice. The
# SDK sets -ffp-model=strict for its own targets only; this covers the translated code, the
# renderer and the PCH. The generator's explicit std::fma still becomes a single vfmadd.
add_compile_options($<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-contract=off>)

include(generated/rexglue.cmake)

# The translated code without asynchronous exception handling (#40). generated/rexglue.cmake adds
# -fasync-exceptions for guest SEH scopes (__try/__except), but the manifest doesn't generate any
# (generate_exception_handlers is off). Guest memory faults (the write watch, MMIO) go to the SDK's
# vectored exception handler, which needs no EH mode.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fear_manifest.toml" FEAR_MANIFEST)
if(FEAR_MANIFEST MATCHES "\ngenerate_exception_handlers *= *true")
    message(FATAL_ERROR "fear_manifest.toml generates guest SEH scopes: keep -fasync-exceptions.")
endif()
list(REMOVE_ITEM REXGLUE_RECOMP_OPTIONS /EHa -fasync-exceptions)
if(REXGLUE_ENTRYPOINT_GENERATED_SOURCES)
    set_source_files_properties(${REXGLUE_ENTRYPOINT_GENERATED_SOURCES}
        PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
endif()

# fear-nx's graphics system (src/native/UPSTREAM.md): the GPU ring and ReXGlue's presenter (D3D12
# or Vulkan, the renderer's API). Its own Vulkan renderer is gone (docs/NATIVE_RENDERER.md; in git
# history).
set(FEAR_NATIVE_SOURCES
    src/native/native_system.cpp
)

set(FEAR_SOURCES
    src/main.cpp
    src/anim_timing.cpp
    src/bloom.cpp
    src/fear_version.cpp
    src/high_fps.cpp
    src/weapon_fixes.cpp
    src/profiler.cpp
    src/cpu_check.cpp
    src/crash_report.cpp
    src/display_vblank.cpp
    src/fear_collection.cpp
    src/fear_menu.cpp
    src/fear_panel.cpp
    src/fear_process.cpp
    src/fear_settings.cpp
    src/fx_debug.cpp
    src/look_tuning.cpp
    src/difficulty.cpp
    src/pipeline_warmup.cpp
    ${FEAR_NATIVE_SOURCES}
)

# The CPU check runs before any x86-64-v3 code (src/cpu_check.cpp), so it is built for v2 and
# without the precompiled header (built for v3).
set_source_files_properties(src/cpu_check.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v2"
                            SKIP_PRECOMPILE_HEADERS ON)
add_executable(fear WIN32 ${FEAR_SOURCES})
# The build's version (src/fear_version.h): cmake/fear_version.cmake turns the repository's VERSION file
# and `git describe` into fear_version_info.h before every build, rewriting it only when it changes.
set(FEAR_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/fear_version")
add_custom_target(fear_version
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DOUT=${FEAR_VERSION_DIR}/fear_version_info.h" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fear_version.cmake"
    BYPRODUCTS "${FEAR_VERSION_DIR}/fear_version_info.h"
    COMMENT "F.E.A.R. version"
    VERBATIM)
add_dependencies(fear fear_version)
target_include_directories(fear PRIVATE "${FEAR_VERSION_DIR}")

rexglue_setup_target(fear)

include(cmake/fear_common.cmake)
fear_link_plugin_whole()
fear_stage_runtime_libraries()
if(WIN32)
    target_link_libraries(fear PRIVATE dbghelp)   # crash reports (src/crash_report.cpp)
    target_link_libraries(fear PRIVATE dxgi)      # the display's vblank (src/display_vblank.cpp)
endif()
# The renderer (cmake/fear_common.cmake). port-files builds it too, for its own Direct3D
# (FEAR_TITLE_FILES, src/d3d12/fear_d3d.h).
fear_add_renderer("${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(fear PRIVATE
    "${REXSDK_DIR}/src/graphics"
    "${REXSDK_DIR}/thirdparty/renderdoc"   # rex/ui/renderdoc_api.h, private to the plugin
    "${REXSDK_DIR}/thirdparty/stb"         # stb_image.h's zlib, for the game's catalog
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)

# Offline texture pack indexer (tools/texture_pack.py runs it): the SDK's own layout and
# untiling code and the renderer's pack key, so offline and runtime keys agree by construction.
add_executable(fear_texindex tools/texindex.cpp src/texture_pack.cpp)
target_include_directories(fear_texindex PRIVATE src "${REXSDK_DIR}/src/graphics")
target_link_libraries(fear_texindex PRIVATE rexgpu-xenos rex::runtime xxHash::xxhash)
add_dependencies(fear fear_texindex)
