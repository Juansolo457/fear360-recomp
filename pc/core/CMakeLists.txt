# pc/core: the parts of the PC build that don't know the engine (pad input, the 360 layouts
# and look model). fearpc_core_logic has no dependencies and is unit-tested; fearpc_core adds
# SDL3 for the pads.

set(FEAR_SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/rexglue-sdk-nx/thirdparty/sdl3"
    CACHE PATH "SDL3 source tree")
set(FEAR_SDL3_COMMIT 8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4)  # SDL 3.5.0, as the recomp's SDK

add_library(fearpc_core_logic STATIC src/layout.cpp src/look.cpp src/pad_math.cpp)
target_include_directories(fearpc_core_logic PUBLIC include)
# Private: the headers are plain C++11, and the game code that includes them builds as C++14.
target_compile_features(fearpc_core_logic PRIVATE cxx_std_17)

add_executable(fearpc_core_tests tests/core_tests.cpp)
target_compile_features(fearpc_core_tests PRIVATE cxx_std_17)
target_link_libraries(fearpc_core_tests PRIVATE fearpc_core_logic)

# SDL3, static, with only what pads need.
if(EXISTS "${FEAR_SDL3_DIR}/CMakeLists.txt")
  set(_sdl_source "${FEAR_SDL3_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG ${FEAR_SDL3_COMMIT})
  FetchContent_GetProperties(SDL3)
  if(NOT sdl3_POPULATED)
    FetchContent_Populate(SDL3)
  endif()
  set(_sdl_source "${sdl3_SOURCE_DIR}")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(_off AUDIO VIDEO GPU RENDER CAMERA SENSOR POWER DIALOG TRAY)
  set(SDL_${_off} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${_sdl_source}" "${CMAKE_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)

add_library(fearpc_core STATIC src/pad_sdl.cpp)
target_link_libraries(fearpc_core PUBLIC fearpc_core_logic PRIVATE SDL3::SDL3-static)

# Prints the pads SDL sees and their state.
add_executable(fearpc_pad_probe tools/pad_probe.cpp)
target_link_libraries(fearpc_pad_probe PRIVATE fearpc_core SDL3::SDL3-static)
