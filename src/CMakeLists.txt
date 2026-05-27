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
    gpu/intel/ze_driver.cc
    gpu/intel/intel_device.cc
    gpu/opencl/cl_driver.cc
    gpu/opencl/opencl_device.cc
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

# ---- String / payload obfuscation ----------------------------------------
# Several .rodata blobs (the four GPU kernel sources, the SPIR-V module,
# and the CLI version / help text) are XOR-obfuscated before being baked
# into the loadmaster binary. Compile-time C++ encoding happens via
# util::obfuscate::Encode (consteval) inside the .h files; this option
# only needs to flow through to the SPIR-V CMake-side encoder below and
# to the LOADMASTER_OBFUSCATE preprocessor define that util/obfuscate.h
# reads at compile time.
#
# Default ON. Turn OFF only when bisecting a kernel build/JIT failure
# or trying to read the help text out of a debug build: with
# obfuscation off the embedded byte arrays match the .h sources
# verbatim, so cuobjdump / spirv-dis / Metal compiler errors line up
# with what you read in the .h files.
#
# The historical option name `LOADMASTER_OBFUSCATE_KERNELS` is kept as
# an alias for backwards compatibility with older invocations.
option(LOADMASTER_OBFUSCATE
       "XOR-obfuscate embedded kernel + CLI strings before baking them into the binary" ON)
# Honor the legacy name if the user passed it explicitly.
if(DEFINED LOADMASTER_OBFUSCATE_KERNELS AND NOT DEFINED CACHE{LOADMASTER_OBFUSCATE_OVERRIDDEN})
    set(LOADMASTER_OBFUSCATE "${LOADMASTER_OBFUSCATE_KERNELS}" CACHE BOOL "" FORCE)
    set(LOADMASTER_OBFUSCATE_OVERRIDDEN ON CACHE INTERNAL "")
endif()
if(LOADMASTER_OBFUSCATE)
    target_compile_definitions(loadmaster PRIVATE LOADMASTER_OBFUSCATE=1)
else()
    target_compile_definitions(loadmaster PRIVATE LOADMASTER_OBFUSCATE=0)
endif()

# ---- Compile-time log threshold ------------------------------------------
# Strips LOG_* sites whose level is below this threshold at preprocessor
# time, so the format strings, the __FILE__ paths, and the per-call
# helper code never enter the binary. This both shrinks the executable
# and removes a sizeable source of identifying strings (module names,
# vendor literals like "NVIDIA: %d device(s) detected", etc.) that
# `strings` would otherwise pick up.
#
# The default `trace` keeps every site compiled in, so behavior matches
# the historical build exactly -- runtime `-L <level>` then narrows
# among them. Bump to `info` / `warn` for a quieter, leaner release
# binary; `LOG_FATAL` is never compiled out.
set(LOADMASTER_LOG_MIN_LEVEL "trace" CACHE STRING
    "Lowest log level kept in the binary (trace/debug/info/warn/error/fatal). \
Sites below this threshold expand to ((void)0).")
set_property(CACHE LOADMASTER_LOG_MIN_LEVEL PROPERTY STRINGS
             trace debug info warn error fatal)
# Translate to the numeric value matching LOADMASTER_LOG_LEVEL_*
# in src/util/log.h.
set(_lm_log_level_num 0)
string(TOLOWER "${LOADMASTER_LOG_MIN_LEVEL}" _lm_log_level_lc)
if(_lm_log_level_lc STREQUAL "trace")
    set(_lm_log_level_num 1)
elseif(_lm_log_level_lc STREQUAL "debug")
    set(_lm_log_level_num 2)
elseif(_lm_log_level_lc STREQUAL "info")
    set(_lm_log_level_num 3)
elseif(_lm_log_level_lc STREQUAL "warn")
    set(_lm_log_level_num 4)
elseif(_lm_log_level_lc STREQUAL "error")
    set(_lm_log_level_num 5)
elseif(_lm_log_level_lc STREQUAL "fatal")
    set(_lm_log_level_num 6)
else()
    message(FATAL_ERROR
        "LOADMASTER_LOG_MIN_LEVEL must be one of trace/debug/info/warn/error/fatal, "
        "got '${LOADMASTER_LOG_MIN_LEVEL}'")
endif()
target_compile_definitions(loadmaster PRIVATE
    LOADMASTER_LOG_MIN_LEVEL=${_lm_log_level_num})
message(STATUS
    "Log threshold: keeping LOG_${_lm_log_level_lc}+ sites compiled in (level ${_lm_log_level_num})")

# ---- Intel Level Zero busy kernel: SPIR-V embedding -----------------------
# The Intel backend needs a precompiled SPIR-V binary. We bake it into
# the binary at compile time by generating a tiny header (intel_spirv_data.h)
# in the build directory. If the .spv file exists in the source tree the
# header contains the (optionally XOR-obfuscated) byte array; otherwise
# the header still compiles but defines a zero-length placeholder, and
# the Intel backend disables itself at runtime (the OpenCL backend
# covers Intel iGPU in that case).
#
# Obfuscation: when LOADMASTER_OBFUSCATE=ON we XOR each byte by
# (key_byte_at(i) ^ (i & 0xff)) where the rolling key is the 32-bit
# kBusyKernelSpirvKey. This MUST match util::obfuscate::MixByte exactly
# so IntelDevice::Init() can decode it back. The .spv file on disk
# stays plaintext so spirv-dis / git diff still work.
#
# To regenerate the .spv (one-shot, on any Linux box with Intel
# compute-runtime tooling installed -- see src/gpu/intel/busy_kernel.cl
# for the exact ocloc/clang commands), drop the resulting file at
# src/gpu/intel/busy_kernel.spv and re-run CMake.
set(INTEL_SPIRV_FILE "${CMAKE_CURRENT_SOURCE_DIR}/gpu/intel/busy_kernel.spv")
set(INTEL_SPIRV_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/gpu/intel")
set(INTEL_SPIRV_GEN_HEADER "${INTEL_SPIRV_GEN_DIR}/intel_spirv_data.h")
# Per-backend XOR key. Must stay in sync with the constant of the same
# name in src/gpu/intel/spirv_kernel.h.
set(INTEL_SPIRV_KEY "0xB52E4C8D")
file(MAKE_DIRECTORY "${INTEL_SPIRV_GEN_DIR}")
if(EXISTS "${INTEL_SPIRV_FILE}")
    file(READ "${INTEL_SPIRV_FILE}" INTEL_SPIRV_HEX HEX)
    string(LENGTH "${INTEL_SPIRV_HEX}" INTEL_SPIRV_HEX_LEN)
    math(EXPR INTEL_SPIRV_BYTE_COUNT "${INTEL_SPIRV_HEX_LEN} / 2")

    if(LOADMASTER_OBFUSCATE)
        # Walk the hex string two characters at a time, XOR each byte
        # with the rolling key + index mask, and accumulate a "0xNN,"
        # comma-separated list. CMake's math(EXPR) supports bitwise
        # operators since 3.13; we rely on that.
        set(INTEL_SPIRV_C_ARRAY "")
        set(_byte_count_dec 0)
        # Pre-extract the four key bytes once.
        math(EXPR _k0 "(${INTEL_SPIRV_KEY}      ) & 0xff" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR _k1 "(${INTEL_SPIRV_KEY} >> 8 ) & 0xff" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR _k2 "(${INTEL_SPIRV_KEY} >> 16) & 0xff" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR _k3 "(${INTEL_SPIRV_KEY} >> 24) & 0xff" OUTPUT_FORMAT HEXADECIMAL)
        set(_keys "${_k0}" "${_k1}" "${_k2}" "${_k3}")
        # Iterate the hex pairs.
        math(EXPR _hex_last "${INTEL_SPIRV_HEX_LEN} - 1")
        foreach(_pos RANGE 0 ${_hex_last} 2)
            string(SUBSTRING "${INTEL_SPIRV_HEX}" ${_pos} 2 _byte_hex)
            math(EXPR _key_idx "${_byte_count_dec} % 4")
            list(GET _keys ${_key_idx} _k)
            math(EXPR _idx_mask "${_byte_count_dec} % 256")
            math(EXPR _enc "(0x${_byte_hex}) ^ (${_k}) ^ (${_idx_mask})"
                 OUTPUT_FORMAT HEXADECIMAL)
            string(APPEND INTEL_SPIRV_C_ARRAY "${_enc},")
            math(EXPR _byte_count_dec "${_byte_count_dec} + 1")
            # Cosmetic: line break every 16 bytes so the generated
            # header isn't a single 8 KB-long line.
            math(EXPR _wrap "${_byte_count_dec} % 16")
            if(_wrap EQUAL 0)
                string(APPEND INTEL_SPIRV_C_ARRAY "\n    ")
            endif()
        endforeach()
        message(STATUS "Intel backend: embedding ${INTEL_SPIRV_BYTE_COUNT} bytes of SPIR-V "
                       "(XOR-obfuscated) from ${INTEL_SPIRV_FILE}")
    else()
        # Plain "deadbeef" -> "0xde,0xad,0xbe,0xef,..." path, used only
        # when obfuscation is disabled at configure time.
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," INTEL_SPIRV_C_ARRAY "${INTEL_SPIRV_HEX}")
        string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    "
               INTEL_SPIRV_C_ARRAY "${INTEL_SPIRV_C_ARRAY}")
        message(STATUS "Intel backend: embedding ${INTEL_SPIRV_BYTE_COUNT} bytes of SPIR-V "
                       "from ${INTEL_SPIRV_FILE} (obfuscation disabled)")
    endif()

    set(INTEL_SPIRV_HEADER_CONTENT
"// Auto-generated by CMake from src/gpu/intel/busy_kernel.spv. Do not edit.
#pragma once
#include <cstddef>
#include <cstdint>
namespace gpu::intel {
inline constexpr std::uint8_t kBusyKernelSpirv[] = {
    ${INTEL_SPIRV_C_ARRAY}
};
inline constexpr std::size_t kBusyKernelSpirvSize = ${INTEL_SPIRV_BYTE_COUNT};
}  // namespace gpu::intel
")
else()
    set(INTEL_SPIRV_HEADER_CONTENT
"// Auto-generated by CMake. Placeholder: no busy_kernel.spv was found in the
// source tree, so the Intel Level Zero backend will disable itself at runtime.
// See src/gpu/intel/busy_kernel.cl for instructions on regenerating the .spv.
#pragma once
#include <cstddef>
#include <cstdint>
namespace gpu::intel {
inline constexpr std::uint8_t kBusyKernelSpirv[1] = { 0 };
inline constexpr std::size_t kBusyKernelSpirvSize = 0;
}  // namespace gpu::intel
")
    message(STATUS "Intel backend: no busy_kernel.spv found; backend will be disabled "
                   "at runtime (OpenCL fallback still covers Intel iGPU)")
endif()
file(WRITE "${INTEL_SPIRV_GEN_HEADER}" "${INTEL_SPIRV_HEADER_CONTENT}")
target_include_directories(loadmaster PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
