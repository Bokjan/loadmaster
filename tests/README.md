# Unit tests

This directory hosts the unit-test suite for `loadmaster`. Tests are
**off by default**: the option `LOADMASTER_BUILD_TESTS` must be set
explicitly at configure time, so day-to-day release builds and the
packaging scripts under `scripts/` are not affected.

## Running

The fastest way is the wrapper script:

```bash
# POSIX (Linux / macOS):
scripts/run_tests.sh                  # Debug build, all tests
scripts/run_tests.sh --filter 'Cli.*' # narrow to a subset
scripts/run_tests.sh --release        # Release build
scripts/run_tests.sh --clean          # wipe build_tests/ first
scripts/run_tests.sh --sanitize       # ASan + UBSan, in build_tests_sanitize/
scripts/run_tests.sh --werror         # warnings are errors (as in CI)
scripts/run_tests.sh --help           # full option list
```

```powershell
# Windows (PowerShell, mirror of the .sh wrapper):
scripts\run_tests.ps1                 # Debug build, all tests
scripts\run_tests.ps1 -Filter 'Cli.*' # narrow to a subset
scripts\run_tests.ps1 -Release        # Release build
scripts\run_tests.ps1 -Clean          # wipe build_tests\ first
scripts\run_tests.ps1 -Werror         # warnings are errors (as in CI)
scripts\run_tests.ps1 -?              # full option list
```

Or, if you prefer driving CMake directly:

```bash
# 1. Configure with tests enabled.
cmake -S . -B build_tests -DLOADMASTER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# 2. Build.
cmake --build build_tests -j

# 3. Run via CTest (from the build dir; `--test-dir` needs CMake 3.20).
cd build_tests && ctest --output-on-failure
```

With CMake 3.21+, `cmake --preset dev && cmake --build --preset dev &&
ctest --preset dev` does the same (see `CMakePresets.json`).

An installed GoogleTest (e.g. Debian's `libgtest-dev`, Homebrew's
`googletest`, or one on `CMAKE_PREFIX_PATH`) is used when CMake finds one,
so configuring needs no network. Otherwise it is fetched and built on the
first configure via `FetchContent` (pinned to a specific release tag +
SHA-256 hash, see `tests/CMakeLists.txt`) and later configures reuse the
download under `build_tests/_deps/`. Sanitizer and MSVC builds always use
the fetched copy; `-DLOADMASTER_SYSTEM_GTEST=OFF` forces it everywhere.

Each `TEST()` is registered as a separate CTest entry by
`gtest_discover_tests`, so you can also filter with the standard
GoogleTest flags:

```bash
./build_tests/tests/util_rolling_sampler_test \
    --gtest_filter='RollingSamplerTest.FullRingRotation'
```

## Layout

```
tests/
├── CMakeLists.txt                  # one entry point, declares every test binary
├── README.md                       # this file
├── cli/
│   ├── cli_test.cc                 # argv -> Options end-to-end, strict integers
│   └── version_string_test.cc      # compile-time version string
├── core/
│   ├── options_test.cc             # CLI argument validation matrix
│   └── signal_flag_test.cc         # one bit per signal, singleton
├── cpu/
│   ├── critical_loop_test.cc       # busy loop is not optimised away
│   ├── load_controller_test.cc     # controller vs. a simulated system
│   └── stat_test.cc                # Linux: busy jiffies, per-CPU /proc/stat, cgroup cpu.max
├── gpu/
│   ├── spirv_embed_test.cc         # CMake SPIR-V encoder vs. util::obfuscate
│   └── worker_test.cc              # calibration / failure handling, fake device
├── memory/
│   ├── allocator_test.cc           # RAII block holder
│   └── manager_test.cc             # size range, 45 s cadence, background path
└── util/
    ├── atomic_bitmap_test.cc
    ├── clock_test.cc               # TicksToNs, GetClockTicksPerSecond
    ├── dl_test.cc                  # loader failure paths
    ├── normal_dist_test.cc
    ├── normal_schedule_test.cc     # rand_normal points: exact average, 100% cap
    ├── obfuscate_test.cc           # built twice: obfuscation on and off
    ├── proc_stat_internal_test.cc  # Linux: /proc/<pid>/stat parser
    ├── proc_stat_test.cc           # this process's real CPU time
    ├── rolling_sampler_test.cc
    └── win_util_test.cc            # Windows: FILETIME helper
```

The directory structure mirrors `src/`: a test for `src/<module>/foo.h`
lives at `tests/<module>/foo_test.cc`.

## Scope: what we test, what we don't

This project is a workload generator, so most code is intentionally
side-effectful (busy loops, allocations, `dlopen` of vendor drivers).
The suite covers the logic that decides *how much* load to generate and
the parsing / unit conversion it depends on, using injected time,
simulated systems and fake devices where real ones would be slow or
unavailable:

**In scope**

- `util/`: math, ring buffers, bitmaps, unit conversion, parsing helpers,
  the rand_normal schedule, obfuscation, the shared-library loader's
  failure paths, process CPU accounting
- `core/options`, `cli/cli`: CLI parsing and validation
- `cpu/`: system-stat parsing, the load controller, the busy loop
- `memory/`: the allocator and the manager's scheduling
- `gpu/`: the worker's calibration / failure handling and the SPIR-V
  embedding

**Out of scope (by design)**

- End-to-end load accuracy on real hardware (it depends on the host);
  check it manually, e.g. by comparing `-l` with `/proc/stat`.
- The vendor GPU backends themselves (`gpu/nvidia`, `gpu/amd`,
  `gpu/intel`, `gpu/opencl`, `gpu/apple`): they need drivers and
  physical devices that CI workers don't have.
- Platform stat backends other than the host's: each platform's
  `ReadSystemBusyNs` / `ProcStat` only builds there.

## Adding a new test

1. Create `tests/<module>/<name>_test.cc`.
2. Add one entry in `tests/CMakeLists.txt` via `loadmaster_add_test`:

   ```cmake
   loadmaster_add_test(util_my_thing_test
       util/my_thing_test.cc
   )
   ```

   The test links `loadmaster_core`, the static library the executable is
   built from, so the code under test needs no listing and is compiled
   with the shipped configuration and warning flags. Pass `STANDALONE`
   for a header-only test that wants its own configuration (it then links
   only the `loadmaster_build_flags` target).

3. Inside the test file, include the header by its in-`src/` path
   (e.g. `#include "util/my_thing.h"`); `src/` is on the include path.
4. Re-run `scripts/run_tests.sh` (it re-configures, so CMake picks up
   the new file).

## Style

- One `TEST()` per behavior; name it `<Subject><WhatHappensUnderWhatCondition>`.
- Prefer `EXPECT_*` over `ASSERT_*` unless a failed check makes the
  rest of the test meaningless.
- For floating-point comparisons, use `EXPECT_NEAR` (or
  `EXPECT_DOUBLE_EQ` for bit-exact equality) and document the chosen
  tolerance in a comment.
- Lead each test file with a short comment explaining *why* this
  module is worth testing and which edge cases are covered, so future
  readers don't have to reverse-engineer intent from the assertions.

## Platform notes

- Tests are pure C++20 and build on Linux/macOS/Windows with the same
  compiler set that the main project supports (GCC 10+, Clang 12+,
  AppleClang 13+, MSVC 19.29+).
- On MSVC, the C runtime is chosen once for the whole build
  (`CMAKE_MSVC_RUNTIME_LIBRARY`, from `LOADMASTER_STATIC_LINK`), so
  `loadmaster_core`, the tests and GoogleTest all use the same one
  (`/MT` by default); mixing them fails to link.
- Tests that touch platform-specific surfaces (e.g. `/proc/stat` parsing)
  are guarded with `#if IS_LINUX` / `IS_WINDOWS` from `core/platform.h`.
  Include that header explicitly: an undefined `IS_*` macro evaluates to 0
  and would silently compile the suite away, which is why the build uses
  `-Werror=undef`.

## IDE setup

The top-level `CMakeLists.txt` defaults `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON`
(and mirrors the file into the source root unless
`LOADMASTER_LINK_COMPILE_COMMANDS=OFF`),
so configuring with `LOADMASTER_BUILD_TESTS=ON` produces
`build_tests/compile_commands.json` that includes GoogleTest's header
paths. Point your editor at it (VSCode C/C++: `"compileCommands":
"${workspaceFolder}/build_tests/compile_commands.json"`; clangd / Neovim
should pick it up automatically) and IntelliSense / clangd will resolve
`#include <gtest/gtest.h>` correctly. Without this step you may see
red squiggles on the gtest include even though the build itself passes.
