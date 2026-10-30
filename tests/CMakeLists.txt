# Unit tests for loadmaster.
#
# This CMakeLists is only entered when LOADMASTER_BUILD_TESTS=ON (see the
# top-level CMakeLists.txt). It uses an installed GoogleTest when there is
# one and otherwise pulls it in via FetchContent, and registers one
# executable per test binary with CTest.

# Prefer an installed GoogleTest (distro package, vcpkg, Homebrew, ...): no
# download, so a fresh build tree configures offline. Not under sanitizers,
# where GoogleTest is built instrumented along with everything else (see
# LOADMASTER_SANITIZE), nor on MSVC, where a prebuilt package rarely
# matches the CRT that LOADMASTER_STATIC_LINK selects.
option(LOADMASTER_SYSTEM_GTEST "Use an installed GoogleTest when one is found" ON)
if(LOADMASTER_SYSTEM_GTEST AND NOT LOADMASTER_SANITIZE AND NOT MSVC)
    find_package(GTest 1.10 CONFIG QUIET)
endif()

if(GTest_FOUND)
    message(STATUS "GoogleTest: using installed ${GTest_VERSION} (${GTest_DIR})")
else()
    include(FetchContent)

    # Use extraction time as the timestamp of unpacked files so that
    # changing the URL forces a rebuild. Set via the policy rather than the
    # DOWNLOAD_EXTRACT_TIMESTAMP keyword, which only exists since CMake 3.24
    # and would be mis-parsed as part of URL_HASH by older versions.
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    # Pin to a known-good GoogleTest release. Update deliberately.
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    )

    # The C runtime on MSVC comes from CMAKE_MSVC_RUNTIME_LIBRARY (top-level
    # CMakeLists.txt), which applies to GoogleTest's targets too; keep
    # gtest's own flag rewriting out of the way.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Tests don't need to install gtest's own targets.
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    # GoogleTest's own .o files bake __FILE__ paths starting with
    # <build>/_deps/googletest-src/... into the test binaries. The
    # compile options from loadmaster_build_flags do not reach those
    # targets, so we sanitize them directly here. Only applied on non-MSVC
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
endif()

include(GoogleTest)

# Helper: declare a test binary that compiles `sources`, links gtest_main,
# and is registered with CTest via gtest_discover_tests.
#
# The code under test comes from loadmaster_core, the library the
# executable itself is built from, so a test sees exactly the shipped
# configuration (obfuscation, log threshold, generated headers) and the
# warning flags of loadmaster_build_flags; the linker pulls in only the
# objects a test actually references. STANDALONE tests link just the flags
# target, for header-only code a test wants to build in a configuration of
# its own (see util_obfuscate_off_test).
function(loadmaster_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 _lm "STANDALONE" "" "")
    add_executable(${name} ${_lm_UNPARSED_ARGUMENTS})
    if(_lm_STANDALONE)
        target_link_libraries(${name} PRIVATE loadmaster_build_flags)
    else()
        target_link_libraries(${name} PRIVATE loadmaster_core)
    endif()
    target_link_libraries(${name} PRIVATE GTest::gtest_main)

    # GoogleTest's headers live under the build tree when it was fetched,
    # and its macros expand __FILE__-carrying code into the test TUs.
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=")
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
)

# rand_normal schedule builder shared by the CPU and GPU managers: exact
# period average, bell shape, and the GPU's clip-and-redistribute at 100%.
loadmaster_add_test(util_normal_schedule_test
    util/normal_schedule_test.cc
)

# Compile-time XOR obfuscation layer. Pure header-only, and built in both
# configurations regardless of LOADMASTER_OBFUSCATE, hence STANDALONE. The
# test exercises Encode/Decode round-trips, MixByte self-inverse, and the
# Holder/Scoped wrappers used by version_string and the GPU kernel sources.
loadmaster_add_test(util_obfuscate_test STANDALONE
    util/obfuscate_test.cc
)
target_compile_definitions(util_obfuscate_test PRIVATE LOADMASTER_OBFUSCATE=1)

# Same source with obfuscation disabled (-DLOADMASTER_OBFUSCATE=OFF builds):
# Encode / Decode must degrade to the identity and still round-trip.
loadmaster_add_test(util_obfuscate_off_test STANDALONE
    util/obfuscate_test.cc
)
target_compile_definitions(util_obfuscate_off_test PRIVATE LOADMASTER_OBFUSCATE=0)

# util::TicksToNs (all platforms, pure arithmetic) and the POSIX-only
# util::GetClockTicksPerSecond; the latter's cases are guarded by
# !IS_WINDOWS, matching the guard inside clock.cc itself.
loadmaster_add_test(util_clock_test
    util/clock_test.cc
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
)

# Linux-only: parse-side of util::ProcStat (the comm-field disambiguation
# logic). The test source itself is fully guarded by IS_LINUX so it
# compiles to an empty TU on other platforms; we still register the
# target everywhere so the build doesn't bit-rot, and gtest_discover_tests
# will simply find zero tests on non-Linux hosts.
loadmaster_add_test(util_proc_stat_internal_test
    util/proc_stat_internal_test.cc
)

# util::ProcStat against this process's real CPU time (burn, then idle).
loadmaster_add_test(util_proc_stat_test
    util/proc_stat_test.cc
)

# ---- core ----
loadmaster_add_test(core_options_test
    core/options_test.cc
)

# SignalFlag is a header-only singleton wrapper around AtomicBitmap.
# AtomicBitmap's bit-level semantics are covered separately; this test
# pins the singleton contract and that each signal number maps to its own
# bit (1 << signal), so handling one signal never clears another's
# pending bit -- the invariant the main loop relies on when polling for
# SIGINT / SIGTERM.
loadmaster_add_test(core_signal_flag_test
    core/signal_flag_test.cc
)

# ---- cli ----
# End-to-end argv -> Options pipeline.
loadmaster_add_test(cli_cli_test
    cli/cli_test.cc
)

# Header-only VersionString(): the compile-time-assembled, optionally
# XOR-obfuscated "loadmaster X.Y.Z[-suffix]" entry point.
loadmaster_add_test(cli_version_string_test
    cli/version_string_test.cc
)

# ---- cpu ----
# Pure helpers from the Linux cpu/stat backend, exposed via
# cpu/stat_internal.h: the BusyJiffies aggregation ("everything not idle")
# and the JiffiesToNs conversion. The test TU is guarded by IS_LINUX; the
# target is still registered everywhere so the build doesn't bit-rot, and
# off Linux it compiles to zero tests.
loadmaster_add_test(cpu_stat_test
    cpu/stat_test.cc
)

# CriticalLoop is the inner busy-multiply routine each CPU worker
# burns cycles in. We pin only the call-contract (count<=0 -> 0) and
# that the multiply chain is not optimised away by the compiler --
# the returned value is intentionally non-deterministic (seeded
# thread_local PRNG) so we don't test it directly.
loadmaster_add_test(cpu_critical_loop_test
    cpu/critical_loop_test.cc
)

# LoadController turns load measurements into the CPU demand. Pure, so
# the test drives it against a simulated system (other load, lagging
# process measurement, imperfect workers, noise) and checks convergence,
# step response and that it no longer rides the clamp rails.
loadmaster_add_test(cpu_load_controller_test
    cpu/load_controller_test.cc
)

# ---- gpu ----
# GpuWorkerContext against a fake (sleep-based) device: shared kernel
# calibration and giving up on a device that keeps failing. No GPU or
# vendor library is needed.
loadmaster_add_test(gpu_worker_test
    gpu/worker_test.cc
)

# Contention monitor (yielding to other GPU users): the string formats the
# sources parse (vendor PCI ids, amdkfd topology, Windows GPU Engine
# instances, macOS IOUserClientCreator) on every host, the yield
# hysteresis and source merging with fake sources and injected time, and
# -- Linux only, zero tests elsewhere -- the amdkfd and DRM sources against
# a fake /proc + /sys tree.
loadmaster_add_test(gpu_contention_parse_test
    gpu/contention_parse_test.cc
)

loadmaster_add_test(gpu_contention_monitor_test
    gpu/contention_monitor_test.cc
)

loadmaster_add_test(gpu_contention_linux_test
    gpu/contention_linux_test.cc
)

# The CMake-side XOR encoder for busy_kernel.spv must agree with
# util::obfuscate and with the key in spirv_kernel.h. loadmaster_core
# supplies the LOADMASTER_OBFUSCATE value and the generated include dir.
loadmaster_add_test(gpu_spirv_embed_test
    gpu/spirv_embed_test.cc
)
target_compile_definitions(gpu_spirv_embed_test PRIVATE
    LOADMASTER_SPIRV_SOURCE="${CMAKE_SOURCE_DIR}/src/gpu/intel/busy_kernel.spv")

# ---- memory ----
# Allocator is the RAII block holder used by the memory subsystem.
# We pin the externally observable contract (empty / non-empty
# transitions, move semantics, Fill guard); out-of-bounds / leak checks
# of the byte loop come from `run_tests.sh --sanitize` runs, since the
# class exposes no buffer getter to peek at.
loadmaster_add_test(memory_allocator_test
    memory/allocator_test.cc
)

# MemoryResourceManagerDefault scheduling: block size range / alignment,
# the 45 s cadence, and that it keeps rescheduling after a background
# (>= 32 MiB) allocation, and that destroying it mid-fill joins promptly.
# Time is injected; no 45 s waits.
loadmaster_add_test(memory_manager_test
    memory/manager_test.cc
)
