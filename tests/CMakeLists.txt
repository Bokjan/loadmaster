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

    # util/log.h prints file paths trimmed by this length so log output
    # shows "src/foo/bar.cc" instead of an absolute path. The main
    # target sets this in src/CMakeLists.txt; mirror it here so any .cc
    # that pulls in log.h compiles cleanly under tests too.
    string(LENGTH "${CMAKE_SOURCE_DIR}/" LM_SOURCE_PATH_SIZE)
    target_compile_definitions(${name} PRIVATE SOURCE_PATH_SIZE=${LM_SOURCE_PATH_SIZE})

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

# Linux-only: parse-side of util::ProcStat (the comm-field disambiguation
# logic). The test source itself is fully guarded by IS_LINUX so it
# compiles to an empty TU on other platforms; we still register the
# target everywhere so the build doesn't bit-rot, and gtest_discover_tests
# will simply find zero tests on non-Linux hosts.
loadmaster_add_test(util_proc_stat_internal_test
    util/proc_stat_internal_test.cc
    ${CMAKE_SOURCE_DIR}/src/util/proc_stat.cc
    ${CMAKE_SOURCE_DIR}/src/util/clock.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# ---- core ----
loadmaster_add_test(core_options_test
    core/options_test.cc
    ${CMAKE_SOURCE_DIR}/src/core/options.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# ---- cli ----
# End-to-end argv -> Options pipeline. Pulls in the cli parser plus its
# transitive deps (options + version_string + logger).
loadmaster_add_test(cli_cli_test
    cli/cli_test.cc
    ${CMAKE_SOURCE_DIR}/src/cli/cli.cc
    ${CMAKE_SOURCE_DIR}/src/cli/version_string.cc
    ${CMAKE_SOURCE_DIR}/src/core/options.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)

# ---- cpu ----
# Pure helpers from cpu/stat: the GetBusyTicks summation and the
# TicksToMilliseconds linear conversion. We deliberately do NOT pull in
# GetCpuProcStat (would touch /proc, GetSystemTimes, or host_statistics64).
loadmaster_add_test(cpu_stat_test
    cpu/stat_test.cc
    ${CMAKE_SOURCE_DIR}/src/cpu/stat.cc
    ${CMAKE_SOURCE_DIR}/src/util/clock.cc
    ${CMAKE_SOURCE_DIR}/src/util/log.cc
)
