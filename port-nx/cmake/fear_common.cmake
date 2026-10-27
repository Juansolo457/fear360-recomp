# What port-nx and port-files build the same way for their target `fear` (both include this).
#
# fear_add_renderer: the renderer (port-nx/src/d3d12/, docs/NATIVE_RENDERER.md; GRAW 360's, from
# pgr4-native), the game's only one: D3D12 and Vulkan on Windows, Vulkan only elsewhere
# (docs/VULKAN_RENDERER.md). An object library: its hooks (REX_HOOK_RAW) override the generated
# Direct3D functions only when its objects are linked in. The SDK's shader translators and texture
# tables it uses are already in the static Xenos plugin (REXGLUE_USE_D3D12, REXGLUE_USE_VULKAN) and
# rexcore, with the patches the renderer needs (patches/rexglue-sdk-nx/0016 on); its Vulkan
# backend records on the SDK's Vulkan device, with the SDK's VMA.
#   fear_add_renderer(<port-nx/src dir> [FILES])   # FILES: F.E.A.R. Files' Direct3D (fear_d3d.h)
function(fear_add_renderer host)
    file(GLOB sources CONFIGURE_DEPENDS "${host}/d3d12/*.cpp")
    if(NOT WIN32)
        # The D3D12 backend's files (its device, draws, textures, resolves, present, frame pacer,
        # DXBC shaders); the rest is API-free or the Vulkan backend's.
        list(FILTER sources EXCLUDE REGEX
             "/(d3d12_[a-z_]+|draw|frame_pacer|geometry_shader|gpu_timer|pipeline_library|present|present_debug|render_targets|resolve|resolve_debug|shader_cache|shader_specialize|shader_translate|textures)\\.cpp$")
    endif()
    add_library(fear_d3d12 OBJECT ${sources})
    if("FILES" IN_LIST ARGN)
        target_compile_definitions(fear_d3d12 PRIVATE FEAR_TITLE_FILES)
    endif()
    target_include_directories(fear_d3d12 PRIVATE "${host}/d3d12"
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
        "${REXSDK_DIR}/thirdparty/renderdoc")  # the SDK's Vulkan headers include it
    rexglue_apply_target_settings(fear_d3d12)
    # The Vulkan backend's headers: Vulkan, VMA and glslang's SPIR-V builder (the SDK's SPIR-V
    # translator includes it); the plugin links the libraries.
    target_link_libraries(fear_d3d12 PRIVATE rex::runtime xxHash::xxhash Vulkan::Headers
        GPUOpen::VulkanMemoryAllocator glslang::SPIRV)
    target_link_libraries(fear PRIVATE fear_d3d12)
    if(WIN32)
        target_link_libraries(fear PRIVATE d3d12 dxguid)
    endif()
endfunction()

# fear_link_plugin_whole: the renderer is written against the plugin's internals (shader
# translators, texture conversion, register file, ...), which the DLL build keeps private: the
# static plugin is linked whole so its cvars and rex_gpu_create survive too.
function(fear_link_plugin_whole)
    target_link_libraries(fear PRIVATE rexgpu-xenos xxHash::xxhash)
    if(WIN32)
        target_link_options(fear PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:rexgpu-xenos>")
    else()
        target_link_options(fear PRIVATE "LINKER:--whole-archive" "$<TARGET_FILE:rexgpu-xenos>"
                            "LINKER:--no-whole-archive")
    endif()
    set_property(TARGET fear APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:rexgpu-xenos>)
endfunction()

# fear_stage_runtime_libraries: on Linux the SDK gives the program an $ORIGIN rpath but stages its
# shared libraries beside it on Windows and macOS only (rexglue_configure_target); the same copy
# here, so the build folder runs.
function(fear_stage_runtime_libraries)
    if(UNIX AND NOT APPLE)
        foreach(lib rexruntime TracyClient)
            if(TARGET ${lib})
                add_custom_command(TARGET fear POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${lib}>
                            $<TARGET_FILE_DIR:fear>
                    VERBATIM)
            endif()
        endforeach()
    endif()
endfunction()
