# __FILE__ sanitization. By default the C/C++ preprocessor expands
# __FILE__ to the absolute source path the compiler was invoked with --
# which embeds the builder's home directory / username into the binary
# (visible via `strings`). We strip that out at compile time on
# Clang/GCC via -ffile-prefix-map, mapping "<repo>/" to the empty
# string so __FILE__ becomes a project-relative path like
# "src/util/proc_stat.cc".
#
# MSVC has no equivalent flag but already defaults __FILE__ to a path
# relative to the build's working directory (the build_release_windows
# directory used by build_windows.ps1), which does not contain the
# builder's username.
#
# Once __FILE__ is already a short relative path we no longer need the
# logger's runtime SOURCE_PATH_SIZE offset trick. We keep the macro for
# compatibility (so older builds that don't pass the flag still print
# tidy paths) but set it to 0 here; the logger's FILE_NAME() macro then
# becomes an identity op.
set(SOURCE_PATH_SIZE 0)

find_package(Threads REQUIRED)

add_executable(loadmaster)

target_sources(loadmaster PRIVATE
    main.cc
    core/runtime.cc
    core/options.cc
    cli/cli.cc
    cli/version_string.cc
    cpu/stat.cc
    cpu/worker.cc
    cpu/manager.cc
    cpu/manager_default.cc
    cpu/manager_random_normal.cc
    cpu/factory.cc
    cpu/critical_loop.cc
    memory/factory.cc
    memory/manager.cc
    memory/manager_default.cc
    memory/allocator.cc
    gpu/factory.cc
    gpu/manager.cc
    gpu/manager_default.cc
    gpu/manager_random_normal.cc
    gpu/worker.cc
    gpu/nvidia/cuda_driver.cc
    gpu/nvidia/nvidia_device.cc
    gpu/amd/hip_driver.cc
    gpu/amd/amd_device.cc
    util/dl.cc
    util/log.cc
    util/clock.cc
    util/normal_dist.cc
    util/proc_stat.cc
)

# macOS-only sources: Apple Metal GPU backend. Compiled as Objective-C++
# (.mm) so we can use the Metal/Foundation frameworks directly. ARC is
# enabled below.
if(APPLE)
    target_sources(loadmaster PRIVATE
        gpu/apple/metal_device.mm
    )
    set_source_files_properties(gpu/apple/metal_device.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

target_include_directories(loadmaster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(loadmaster PRIVATE cxx_std_20)

target_compile_definitions(loadmaster PRIVATE SOURCE_PATH_SIZE=${SOURCE_PATH_SIZE})

if(MSVC)
    target_compile_options(loadmaster PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus      # report the actual C++ standard via __cplusplus
        /Zc:preprocessor     # use the conforming preprocessor (needed for our macros)
        /utf-8               # interpret source files as UTF-8
    )
    target_compile_definitions(loadmaster PRIVATE
        _CRT_SECURE_NO_WARNINGS  # we use std::sscanf etc.; their MSVC-specific
                                 # _s variants are guarded by _MSC_VER.
    )
else()
    target_compile_options(loadmaster PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wno-unused-parameter
        -fPIC
        # Rewrite the source-path prefix baked into __FILE__ (and any
        # other compiler-emitted path strings) so the resulting binary
        # does not leak the absolute build path -- which on developer
        # machines exposes the user's home directory / username. The
        # trailing slash matters: we want "src/foo.cc" not "/foo.cc".
        # Supported by Clang 10+ and GCC 8+ (the project's C++20
        # requirement already implies newer than that on the
        # non-MSVC paths).
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
    )
endif()

# Debug-only macro
target_compile_definitions(loadmaster PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Threading. CMAKE_DL_LIBS is empty on Windows (LoadLibrary lives in
# kernel32, auto-linked), and resolves to -ldl on Linux.
target_link_libraries(loadmaster PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Linking strategy.
#   * Linux/GCC/Clang:
#     The GPU module dlopen()s vendor libraries at runtime, which is
#     incompatible with a fully-static (-static) executable on glibc.
#     Link libstdc++/libgcc statically so the binary is portable across
#     distros while libc/libdl stay dynamic.
#   * macOS/AppleClang:
#     Apple does not support / does not ship static libc++ or libSystem
#     archives, and `-static-libstdc++ -static-libgcc` are GCC-only flags
#     that AppleClang silently accepts but ld64 then rejects. We rely on
#     the system-provided libc++.dylib + libSystem.dylib, which are
#     guaranteed-present on every supported macOS version.
#   * Windows/MSVC:
#     Use the static C/C++ runtime (/MT, /MTd in Debug) so the binary
#     does not require the Visual C++ Redistributable on the target
#     machine.
option(LOADMASTER_STATIC_LINK
       "Statically link the C++/runtime into the loadmaster executable" ON)
if(LOADMASTER_STATIC_LINK)
    if(MSVC)
        # CMake 3.15+: pick the static runtime via this property.
        set_property(TARGET loadmaster PROPERTY
                     MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(APPLE)
        # No-op: see comment above. Honoring the option here would just
        # produce a broken link line. We keep the option ON by default for
        # cross-platform parity in build scripts.
    else()
        target_link_options(loadmaster PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

# macOS: link the system frameworks we depend on.
#   * CoreFoundation: pulled in transparently by libSystem today, but
#     listed explicitly so the dependency is obvious.
#   * Foundation     : Objective-C runtime + NSString/NSError used by
#     gpu/apple/metal_device.mm.
#   * Metal          : MTLDevice / MTLCommandQueue / MTLLibrary etc.,
#     i.e. the entire Apple-side GPU backend.
if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(loadmaster PRIVATE
        ${COREFOUNDATION_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
    )
endif()
