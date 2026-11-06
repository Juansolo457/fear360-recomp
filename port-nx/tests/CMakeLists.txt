# Unit tests for the host code that runs without the game: no SDK, no translated code, no game
# data. tools/test.ps1 builds and runs them on Windows; CI (.github/workflows/tests.yml) on Linux.
#
#   cmake -S port-nx/tests -B out/tests && cmake --build out/tests && ctest --test-dir out/tests
#
# Catch2 v3, as the SDK's own tests use: fetched at the tag below, or taken from a checkout with
# -DFEAR_CATCH2_DIR=<third_party/rexglue-sdk-nx/thirdparty/catch2> (no download).

cmake_minimum_required(VERSION 3.25)
project(fear_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(FEAR_CATCH2_DIR)
    add_subdirectory("${FEAR_CATCH2_DIR}" catch2 EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.12.0)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(fear_tests
    weapon_cycle_test.cpp
)
target_include_directories(fear_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(fear_tests PRIVATE Catch2::Catch2WithMain)

enable_testing()
add_test(NAME fear_tests COMMAND fear_tests)
