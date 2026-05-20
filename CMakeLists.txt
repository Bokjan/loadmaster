cmake_minimum_required(VERSION 3.15)
project(loadmaster
        VERSION 0.5.2
        DESCRIPTION "Workload generator"
        LANGUAGES CXX)

# Sensible defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

# Picked up by CMAKE_MSVC_RUNTIME_LIBRARY (CMake 3.15+) so that
# `set_property(... MSVC_RUNTIME_LIBRARY ...)` in src/CMakeLists.txt
# is the single source of truth for the C runtime selection on MSVC.
cmake_policy(SET CMP0091 NEW)

add_subdirectory(src)
