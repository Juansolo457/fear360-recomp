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
    src/bloom.cpp
    src/high_fps.cpp
    src/profiler.cpp
    src/cpu_check.cpp
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
    src/pitfight.cpp
    src/difficulty.cpp
    src/shader_warmup.cpp
    ${FEAR_NATIVE_SOURCES}
)

# The CPU check runs before any x86-64-v3 code (src/cpu_check.cpp), so it is built for v2 and
# without the precompiled header (built for v3).
set_source_files_properties(src/cpu_check.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v2"
                            SKIP_PRECOMPILE_HEADERS ON)
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
# The shader warm-up list (src/shader_warmup.h): generated locally into warmup/ by
# tools/make_warmup.py and not tracked. Each build copies it, or an empty list, to the build tree.
add_custom_target(fear_warmup_list
    COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/warmup/shader_warmup.inc"
            "-DDST=${CMAKE_CURRENT_BINARY_DIR}/warmup/shader_warmup.inc"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/warmup_list.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/warmup/shader_warmup.inc"
    COMMENT "Shader warm-up list")
add_dependencies(fear fear_warmup_list)
target_include_directories(fear PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/warmup"   # shader_warmup.inc, the list above
    "${REXSDK_DIR}/src/graphics"
    "${REXSDK_DIR}/thirdparty/glslang"
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
