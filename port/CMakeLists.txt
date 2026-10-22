# fear - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fear LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FEAR_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(fear WIN32 ${FEAR_SOURCES})
else()
    add_executable(fear ${FEAR_SOURCES})
endif()

rexglue_setup_target(fear GPU_PLUGINS xenos)
