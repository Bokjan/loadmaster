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

# Mirror the generated compile_commands.json into the project root after
# every build so clangd can find it without any manual configuration.
# Background: clangd looks for compile_commands.json at the source file's
# directory and walks upward, but it does NOT search arbitrary build
# directories like `build_release_macos_arm64/`. Putting a symlink (or a
# copy as a fallback) at the project root is the canonical way to bridge
# that gap, and works regardless of which build script the developer used.
#
# Implementation notes:
#   * `cmake -E create_symlink` produces a real symlink on POSIX; on
#     Windows it requires SeCreateSymbolicLinkPrivilege (admin or
#     Developer Mode). We fall back to `copy_if_different` when the
#     symlink call fails, which still gives clangd the right contents
#     -- it just won't auto-refresh on subsequent rebuilds (acceptable
#     because the common Windows IDE story is `cmake .. && open IDE`,
#     re-run cmake to refresh).
#   * Guarded on the build directory differing from the source dir so
#     in-source builds (rare, but possible) don't try to symlink a file
#     onto itself.
#   * Guarded on this being the top-level project so embedding loadmaster
#     as a subdirectory in a larger build doesn't clobber the outer
#     project's compile_commands.json.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND
   NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND
   CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # Eagerly refresh the symlink at *configure* time too, not just at
    # build time. This handles the common case where a developer
    # nuked an old build directory and the previous symlink has gone
    # dangling -- without this, clangd / IDEs may keep biting on the
    # stale link until the next full build completes. Pointing the
    # link at the current build dir's compile_commands.json is safe
    # even if that file doesn't exist yet: it'll come into existence
    # at the end of this configure step, and a few editors that
    # follow symlinks lazily are happy either way.
    file(REMOVE "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(CREATE_LINK
         "${CMAKE_BINARY_DIR}/compile_commands.json"
         "${CMAKE_SOURCE_DIR}/compile_commands.json"
         SYMBOLIC)

    # And keep the per-build refresh, so subsequent incremental
    # builds also re-target the link if the developer hops between
    # multiple build directories.
    add_custom_target(loadmaster_link_compile_commands ALL
        BYPRODUCTS "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/compile_commands.json"
            ||
                ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Mirroring compile_commands.json to project root for IDE / clangd"
        VERBATIM
    )
endif()

add_subdirectory(src)

# Unit tests. OFF by default so release builds and packaging scripts are
# unaffected. Enable with `-DLOADMASTER_BUILD_TESTS=ON`; on the first
# configure this will pull GoogleTest in via FetchContent.
option(LOADMASTER_BUILD_TESTS "Build loadmaster unit tests" OFF)
if(LOADMASTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
