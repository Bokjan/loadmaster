# Unit tests for loadmaster.
#
# This CMakeLists is only entered when LOADMASTER_BUILD_TESTS=ON (see the
# top-level CMakeLists.txt). It pulls GoogleTest via FetchContent so no
# system-wide install is required, and registers one executable per test
# binary with CTest.

include(FetchContent)

# Pin to a known-good GoogleTest release. Update deliberately.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    # Use extraction time as the timestamp of unpacked files so that
    # changing the URL forces a rebuild (CMP0135 NEW behavior). Older
    # CMake silently ignores this option.
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Make sure GoogleTest links against the same CRT we use on MSVC. The
# top-level project sets MSVC_RUNTIME_LIBRARY on the loadmaster target;
# gtest reads this global at configure time.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Tests don't need to install gtest's own targets.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# GoogleTest's own .o files bake __FILE__ paths starting with
# <build>/_deps/googletest-src/... into the test binaries. The
# per-test compile_options we apply in loadmaster_add_test() do not
# reach those targets, so we sanitize them directly here. Same
# CMAKE_BINARY_DIR-prefix map as below; only applied on non-MSVC
# (MSVC defaults to relative __FILE__).
if(NOT MSVC)
    foreach(_gtest_tgt gtest gtest_main gmock gmock_main)
        if(TARGET ${_gtest_tgt})
            target_compile_options(${_gtest_tgt} PRIVATE
                "-ffile-prefix-map=${CMAKE_BINARY_DIR}/="
            )
        endif()
    endforeach()
endif()

include(GoogleTest)

# Helper: declare a test binary that compiles `sources`, links gtest_main,
# and is registered with CTest via gtest_discover_tests.
function(loadmaster_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE GTest::gtest_main)

    # __FILE__ sanitization. Mirror src/CMakeLists.txt so test binaries
    # don't leak the absolute build path either. We map two prefixes:
    #   * CMAKE_SOURCE_DIR : project sources (both src/* and tests/*).
    #   * CMAKE_BINARY_DIR : FetchContent extracts GoogleTest under
    #     <build>/_deps/googletest-src/, so its own __FILE__ strings
    #     also pick up the builder's home dir unless we rewrite them.
    # SOURCE_PATH_SIZE is kept at 0 (matching src/CMakeLists.txt) so
    # the logger's FILE_NAME() macro is an identity op now that
    # __FILE__ is already a project-relative path.
    target_compile_definitions(${name} PRIVATE SOURCE_PATH_SIZE=0)

    # Mirror the warning flags from src/CMakeLists.txt so tests catch the
    # same things, but stay tolerant of gtest's own quirks where needed.
    if(MSVC)
        target_compile_options(${name} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8
        )
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${name} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
            "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
            "-ffile-prefix-map=${CMAKE_BINARY_DIR}/="
        )
    endif()

    gtest_discover_tests(${name})
endfunction()

# ---- util ----
loadmaster_add_test(util_atomic_bitmap_test
    util/atomic_bitmap_test.cc
)

loadmaster_add_test(util_rolling_sampler_test
    util/rolling_sampler_test.cc
)

loadmaster_add_test(util_normal_dist_test
    util/normal_dist_test.cc
    ${CMAKE_SOURCE_DIR}/src/util/normal_dist.cc
)

# Compile-time XOR obfuscation layer. Pure header-only: no extra .cc
# is needed. The test exercises Encode/Decode round-trips, MixByte
# self-inverse, and the Holder/Scoped wrappers used by version_string
# and the GPU kernel sources.
loadmaster_add_test(util_obfuscate_test
    util/obfuscate_test.cc
)

# POSIX-only: util::GetJiffyFrequency. The TU is guarded by
# !IS_WINDOWS and compiles to zero tests on Windows (matching the
# guard inside clock.cc itself); the target is still registered
# everywhere so the build doesn't bit-rot.
loadmaster_add_test(util_clock_test
    util/clock_test.cc
    ${CMAKE_SOURCE_DIR}/src/util/clock.cc
)

# Windows-only: util::FiletimeTo100Ns is a one-line inline helper but
# it's the load-bearing arithmetic for the Windows CPU/proc-stat
# backends, so we lock the high/low orientation and overflow
# behaviour. TU guarded by IS_WINDOWS; zero tests elsewhere.
loadmaster_add_test(util_win_util_test
    util/win_util_test.cc
)

# Cross-platform shared-library loader. We can't rely on any
# particular system library existing on every CI worker, so the
# test focuses on the failure path (unknown names -> nullptr,
# null-handle safety, Dlerror() always non-null) which holds on
# every host.
loadmaster_add_test(util_dl_test
    util/dl_test.cc
    ${CMAKE_SOURCE_DIR}/src/util/dl.cc
)

# Linux-only: parse-side of util::ProcStat (the comm-field disambiguation
# logic). The test source itself is fully guarded by IS_LINUX so it
# compiles to an empty TU on other platforms; we still register the
# target everywhere so the build doesn't bit-rot, and gtest_discover_tests
# will simply find zero tests on non-Linux hosts.
loadmaster_add_test(util_proc_stat_internal_test
    util/proc_stat_internal_test.cc
    ${CMAKE_SOURCE_DIR}/src/util/proc_stat_linux.cc
    ${CMAKE_SOURCE_DIR}/src/util/clock.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# ---- core ----
loadmaster_add_test(core_options_test
    core/options_test.cc
    ${CMAKE_SOURCE_DIR}/src/core/options.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# SignalFlag is a header-only singleton wrapper around AtomicBitmap.
# AtomicBitmap's bit-level semantics are covered separately; this test
# pins the singleton contract and that Set/Has/Reset forward the
# signal *number* as a mask (not a bit index) -- the invariant the
# main loop relies on when polling for SIGINT / SIGTERM.
loadmaster_add_test(core_signal_flag_test
    core/signal_flag_test.cc
)

# ---- cli ----
# End-to-end argv -> Options pipeline. Pulls in the cli parser plus its
# transitive deps (options + logger). VersionString() lives entirely in
# its header now, so no extra .cc is needed.
loadmaster_add_test(cli_cli_test
    cli/cli_test.cc
    ${CMAKE_SOURCE_DIR}/src/cli/cli.cc
    ${CMAKE_SOURCE_DIR}/src/core/options.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# Header-only VersionString(): the compile-time-assembled, optionally
# XOR-obfuscated "loadmaster X.Y.Z[-suffix]" entry point. No .cc deps
# are needed; we only pull in core/version.h transitively.
loadmaster_add_test(cli_version_string_test
    cli/version_string_test.cc
)

# ---- cpu ----
# Pure helpers from the Linux cpu/stat backend, exposed via
# cpu/stat_internal.h: the BusyJiffies aggregation ("everything not idle")
# and the JiffiesToNs conversion. The test TU is guarded by IS_LINUX, and we
# only pull in stat_linux.cc on Linux -- the Windows/macOS backends
# reference platform-only APIs (GetSystemTimes / host_statistics64) and must
# never be compiled into this test. The target is still registered
# everywhere so the build doesn't bit-rot; off Linux it compiles to zero
# tests. We deliberately do NOT exercise ReadSystemBusyTicks() (it touches
# /proc, GetSystemTimes, or host_statistics64).
if(UNIX AND NOT APPLE)
    loadmaster_add_test(cpu_stat_test
        cpu/stat_test.cc
        ${CMAKE_SOURCE_DIR}/src/cpu/stat_linux.cc
        ${CMAKE_SOURCE_DIR}/src/util/clock.cc
        ${CMAKE_SOURCE_DIR}/src/util/log.cc
    )
else()
    loadmaster_add_test(cpu_stat_test
        cpu/stat_test.cc
    )
endif()

# CriticalLoop is the inner busy-multiply routine each CPU worker
# burns cycles in. We pin only the call-contract (count<=0 -> 0) and
# that the multiply chain is not optimised away by the compiler --
# the returned value is intentionally non-deterministic (seeded
# thread_local PRNG) so we don't test it directly.
loadmaster_add_test(cpu_critical_loop_test
    cpu/critical_loop_test.cc
    ${CMAKE_SOURCE_DIR}/src/cpu/critical_loop.cc
)

# ---- memory ----
# Allocator is the RAII block holder used by the memory subsystem.
# We pin the externally observable contract (empty / non-empty
# transitions, move semantics, FillXor guard); byte-level FillXor
# correctness is left to ASAN/UBSAN runs of the suite since the
# class exposes no buffer getter to peek at.
loadmaster_add_test(memory_allocator_test
    memory/allocator_test.cc
    ${CMAKE_SOURCE_DIR}/src/memory/allocator.cc
)

# MemoryResourceManagerDefault scheduling: the 45-second periodic resize
# loop (WillSchedule/Schedule) and the one-shot background allocate-and-fill
# jthread. These are behavioural smoke tests -- the manager exposes no
# allocator state, so we drive the inline and background paths and rely on
# ASAN/UBSAN to catch leaks, races, and missing joins. Pins the H1
# in-flight-flag lifecycle and the M10 stop-token chunked fill.
loadmaster_add_test(memory_manager_test
    memory/manager_test.cc
    ${CMAKE_SOURCE_DIR}/src/memory/manager_default.cc
    ${CMAKE_SOURCE_DIR}/src/memory/manager.cc
    ${CMAKE_SOURCE_DIR}/src/memory/allocator.cc
    ${CMAKE_SOURCE_DIR}/src/core/options.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)
