# F.E.A.R. PC game code (GameClient.dll, GameServer.dll, ClientFx.fxd) built from the user's
# own v1.08 public tools source. The source lists come from the SDK's VS2003 projects
# (pc/tools/vcproj.py); our changes are patches in pc/game/patches, applied by pc/tools/setup.ps1.
cmake_minimum_required(VERSION 3.25)
project(fear_pc_game LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The game DLLs load into 32-bit FEAR.exe: configure with an x86 toolchain.")
endif()

set(FEAR_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fear-publictools" CACHE PATH "Public tools tree")
set(FEAR_SDK_CONFIG Release CACHE STRING "Project configuration read from the SDK")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_sources "${CMAKE_BINARY_DIR}/sources.cmake")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/../tools/vcproj.py"
          "${FEAR_SDK}" "${FEAR_SDK_CONFIG}" "${_sources}"
  COMMAND_ERROR_IS_FATAL ANY)
include("${_sources}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${FEAR_SDK}/Game/ClientShellDLL/Game_ClientShell.vcproj"
  "${FEAR_SDK}/Game/ObjectDLL/Game_ServerShell.vcproj")

# The engine (FEAR.exe) was built with VS .NET 2003. Match its language defaults where they
# change code generation or what compiles: wchar_t as a typedef, 2003 for-loop scope, no
# exceptions, shared runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(FEAR_COMPILE_OPTIONS
  /W1 /wd4530 /wd4996 /wd4244 /wd4267 /wd4018 /wd4838 /wd4311 /wd4312 /wd4302
  /Zc:wchar_t- /Zc:forScope- /permissive /GS- /GR /EHs-c- /Zi /O2 /Oy- /MP /utf-8
  # The 2003 headers pulled these in transitively; today's don't.
  "$<$<COMPILE_LANGUAGE:CXX>:/FIctype.h;/FIstring>")
# FEAR_PC_NO_GAMESPY: the engine's GameSpy interfaces pass std::string across the DLL boundary,
# which only works with the VS .NET 2003 library (see the patch that adds it).
set(FEAR_COMMON_DEFINES FEAR_PC_NO_GAMESPY _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS
  _WINSOCK_DEPRECATED_NO_WARNINGS _USE_32BIT_TIME_T _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
  _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
set(FEAR_SDK_LIBDIR "${FEAR_SDK}/engine/sdk/lib/win/${FEAR_SDK_CONFIG}")

# Our own code: pc/core (no engine types, with unit tests) and the game-side adapters in src/.
add_subdirectory(../core "${CMAKE_BINARY_DIR}/core")
enable_testing()
add_test(NAME fearpc_core_tests COMMAND fearpc_core_tests)

function(fear_target name kind)
  if(kind STREQUAL "STATIC")
    add_library(${name} STATIC ${${name}_SOURCES})
  else()
    add_library(${name} SHARED ${${name}_SOURCES})
  endif()
  target_include_directories(${name} PRIVATE ${${name}_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${${name}_DEFINES} ${FEAR_COMMON_DEFINES})
  target_compile_options(${name} PRIVATE ${FEAR_COMPILE_OPTIONS})
  set_target_properties(${name} PROPERTIES CXX_STANDARD 14 CXX_EXTENSIONS OFF)
endfunction()

fear_target(Shared_StdLith STATIC)
fear_target(Shared_Platform STATIC)
fear_target(Game_GUIMgr STATIC)
fear_target(Game_Shared_Client STATIC)
fear_target(Game_Shared_Server STATIC)
fear_target(Game_ClientShell SHARED)
fear_target(Game_ServerShell SHARED)
fear_target(Game_ClientFX SHARED)

# FEAR_PC_PAD: game controllers (src/fear_pad.h), patched into CBindMgr, CPlayerMgr and
# CGameClientShell::Update.
target_sources(Game_ClientShell PRIVATE src/fear_pad.cpp)
target_include_directories(Game_ClientShell PRIVATE src)
target_compile_definitions(Game_ClientShell PRIVATE FEAR_PC_PAD)
target_link_libraries(Game_ClientShell PRIVATE fearpc_core)

set(_sdk_libs "${FEAR_SDK_LIBDIR}/Shared_CRC.lib" "${FEAR_SDK_LIBDIR}/Shared_Assert.lib")
target_link_libraries(Game_Shared_Client PUBLIC Shared_Platform Shared_StdLith)
target_link_libraries(Game_Shared_Server PUBLIC Shared_Platform Shared_StdLith)
target_link_libraries(Game_ClientShell PRIVATE Game_Shared_Client Game_GUIMgr ${_sdk_libs}
  shell32 winmm ws2_32 shlwapi)
target_link_libraries(Game_ServerShell PRIVATE Game_Shared_Server ${_sdk_libs} winmm)
target_link_libraries(Game_ClientFX PRIVATE Shared_Platform Shared_StdLith "${FEAR_SDK_LIBDIR}/Shared_Assert.lib")

set(_dll_link_options /DEBUG /OPT:REF /OPT:ICF /SAFESEH:NO /IGNORE:4089 /IGNORE:4099)
foreach(t Game_ClientShell Game_ServerShell Game_ClientFX)
  target_sources(${t} PRIVATE src/crt_shims.c)
  target_link_options(${t} PRIVATE ${_dll_link_options})
endforeach()
set_target_properties(Game_ClientShell PROPERTIES OUTPUT_NAME GameClient)
set_target_properties(Game_ServerShell PROPERTIES OUTPUT_NAME GameServer)
set_target_properties(Game_ClientFX PROPERTIES OUTPUT_NAME ClientFx SUFFIX .fxd)
