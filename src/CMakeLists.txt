# Length of the absolute source-root prefix; used by the logger to print
# short file paths ("src/foo/bar.cc" instead of "/abs/path/src/foo/bar.cc").
string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)

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
    else()
        target_link_options(loadmaster PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
