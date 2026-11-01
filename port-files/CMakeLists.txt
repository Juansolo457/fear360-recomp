# fearfiles (port-files): F.E.A.R. Files (Extraction Point + Perseus Mandate) on the same host
# code as port-nx: fear-nx's patched SDK, the D3D12 renderer, our fixes and options. Only
# the translation (generated/default, from fear_manifest.toml) is this title's own. The project
# keeps the name "fear" so generated/rexglue.cmake applies unchanged; the binary is fearfiles.exe.

cmake_minimum_required(VERSION 3.25)
project(fear LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT REXSDK_DIR)
    message(FATAL_ERROR "port-files builds the SDK from source: pass -DREXSDK_DIR=<third_party/rexglue-sdk-nx>.")
endif()
if(NOT REXGLUE_GPU_XENOS_STATIC)
    message(FATAL_ERROR "port-files links the Xenos plugin in: pass -DREXGLUE_GPU_XENOS_STATIC=ON.")
endif()
if(NOT WIN32 AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"))
    message(FATAL_ERROR "port-files builds for Windows and Linux x86-64 (docs/VULKAN_RENDERER.md).")
endif()
if(NOT REXGLUE_USE_VULKAN OR (WIN32 AND NOT REXGLUE_USE_D3D12))
    message(FATAL_ERROR "port-files needs the SDK's Vulkan backend, and on Windows its D3D12 one "
        "too: pass -DREXGLUE_USE_VULKAN=ON (and -DREXGLUE_USE_D3D12=ON on Windows).")
endif()

# No floating-point contraction: built for x86-64-v3 (tools/build-nx.ps1), clang's default
# -ffp-contract=on fuses a*b+c into one FMA, rounding once where the PowerPC rounded twice. The
# SDK sets -ffp-model=strict for its own targets only; this covers the translated code, the
# renderer and the PCH. The generator's explicit std::fma still becomes a single vfmadd.
add_compile_options($<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-contract=off>)

include(generated/rexglue.cmake)

# The translated code without asynchronous exception handling, as in port-nx/CMakeLists.txt (#40).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fear_manifest.toml" FEAR_MANIFEST)
if(FEAR_MANIFEST MATCHES "\ngenerate_exception_handlers *= *true")
    message(FATAL_ERROR "fear_manifest.toml generates guest SEH scopes: keep -fasync-exceptions.")
endif()
list(REMOVE_ITEM REXGLUE_RECOMP_OPTIONS /EHa -fasync-exceptions)
if(REXGLUE_ENTRYPOINT_GENERATED_SOURCES)
    set_source_files_properties(${REXGLUE_ENTRYPOINT_GENERATED_SOURCES}
        PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
endif()

set(HOST "${CMAKE_CURRENT_SOURCE_DIR}/../port-nx/src")
set(FEAR_SOURCES
    ${HOST}/main.cpp
    ${HOST}/anim_timing.cpp
    ${HOST}/bloom.cpp
    ${HOST}/fear_version.cpp
    ${HOST}/high_fps.cpp
    ${HOST}/weapon_fixes.cpp
    ${HOST}/profiler.cpp
    ${HOST}/cpu_check.cpp
    ${HOST}/crash_report.cpp
    ${HOST}/display_vblank.cpp
    ${HOST}/fear_collection.cpp
    ${HOST}/fear_controls.cpp
    ${HOST}/fear_menu.cpp
    ${HOST}/fear_mkb.cpp
    ${HOST}/fear_panel.cpp
    ${HOST}/fear_process.cpp
    ${HOST}/fear_settings.cpp
    ${HOST}/fx_debug.cpp
    ${HOST}/look_tuning.cpp
    ${HOST}/difficulty.cpp
    ${HOST}/pipeline_warmup.cpp
    ${HOST}/pgo.cpp
    ${HOST}/native/native_system.cpp
)

# The CPU check runs before any x86-64-v3 code (port-nx/src/cpu_check.cpp), so it is built for v2
# and without the precompiled header (built for v3).
set_source_files_properties(${HOST}/cpu_check.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v2"
                            SKIP_PRECOMPILE_HEADERS ON)
add_executable(fear WIN32 ${FEAR_SOURCES})
set_target_properties(fear PROPERTIES OUTPUT_NAME fearfiles)
# The build's version (src/fear_version.h): port-nx's cmake/fear_version.cmake turns the repository's VERSION file
# and `git describe` into fear_version_info.h before every build, rewriting it only when it changes.
set(FEAR_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/fear_version")
add_custom_target(fear_version
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DOUT=${FEAR_VERSION_DIR}/fear_version_info.h" -P "${CMAKE_CURRENT_SOURCE_DIR}/../port-nx/cmake/fear_version.cmake"
    BYPRODUCTS "${FEAR_VERSION_DIR}/fear_version_info.h"
    COMMENT "F.E.A.R. version"
    VERBATIM)
add_dependencies(fear fear_version)
target_include_directories(fear PRIVATE "${FEAR_VERSION_DIR}")
# Which game the host code is built for (port-nx/src/fear_title.h): the menu hooks' addresses.
target_compile_definitions(fear PRIVATE FEAR_TITLE_FILES)

rexglue_setup_target(fear)

include("${CMAKE_CURRENT_SOURCE_DIR}/../port-nx/cmake/fear_common.cmake")
fear_link_plugin_whole()
fear_stage_runtime_libraries()
if(WIN32)
    target_link_libraries(fear PRIVATE dbghelp dxgi)
else()
    target_link_libraries(fear PRIVATE SDL3::SDL3)  # the panels' controllers (fear_panel.cpp)
endif()
# The renderer, for F.E.A.R. Files' Direct3D (port-nx/cmake/fear_common.cmake).
fear_add_renderer("${HOST}" FILES)
target_include_directories(fear PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"            # generated/default/fear_init.h, from port-nx/src/main.cpp
    "${HOST}"
    "${REXSDK_DIR}/src/graphics"
    "${REXSDK_DIR}/thirdparty/renderdoc"
    "${REXSDK_DIR}/thirdparty/stb"           # stb_image.h's zlib, for the game's catalog
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
