cmake_minimum_required(VERSION 3.15)
project(loadmaster LANGUAGES CXX)

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

# Emit compile_commands.json so IDEs / clangd / VSCode C/C++ extension
# can resolve include paths (including FetchContent'd GoogleTest headers
# when tests are enabled) without manual c_cpp_properties.json juggling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

# Unit tests. OFF by default so release builds and packaging scripts are
# unaffected. Enable with `-DLOADMASTER_BUILD_TESTS=ON`; on the first
# configure this will pull GoogleTest in via FetchContent.
option(LOADMASTER_BUILD_TESTS "Build loadmaster unit tests" OFF)
if(LOADMASTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
