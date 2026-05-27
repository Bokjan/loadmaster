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
scripts/run_tests.sh --help           # full option list
```

```powershell
# Windows (PowerShell, mirror of the .sh wrapper):
scripts\run_tests.ps1                 # Debug build, all tests
scripts\run_tests.ps1 -Filter 'Cli.*' # narrow to a subset
scripts\run_tests.ps1 -Release        # Release build
scripts\run_tests.ps1 -Clean          # wipe build_tests\ first
scripts\run_tests.ps1 -?              # full option list
```

Or, if you prefer driving CMake directly:

```bash
# 1. Configure with tests enabled.
cmake -S . -B build_tests -DLOADMASTER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# 2. Build.
cmake --build build_tests -j

# 3. Run via CTest.
ctest --test-dir build_tests --output-on-failure
```

GoogleTest itself is fetched and built on the first configure via
`FetchContent` (pinned to a specific release tag + SHA-256 hash, see
`tests/CMakeLists.txt`). No system-wide install of gtest is required.
Subsequent configures reuse the cached download under
`build_tests/_deps/`.

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
├── CMakeLists.txt              # one entry point, declares every test binary
├── README.md                   # this file
├── cli/
│   └── cli_test.cc             # argv -> Options end-to-end
├── core/
│   └── options_test.cc         # CLI argument validation matrix
├── cpu/
│   └── stat_test.cc            # GetBusyTicks / TicksToMilliseconds
└── util/
    ├── atomic_bitmap_test.cc
    ├── rolling_sampler_test.cc
    ├── normal_dist_test.cc
    └── proc_stat_internal_test.cc   # Linux-only; fmemopen-backed fixtures
```

The directory structure mirrors `src/`: a test for `src/<module>/foo.h`
lives at `tests/<module>/foo_test.cc`.

## Scope: what we test, what we don't

This project is a workload generator, so most code is intentionally
side-effectful (busy loops, allocations, `dlopen` of vendor drivers).
The test suite deliberately focuses on the **pure-logic core** that is
deterministic, hardware-independent, and runnable on any CI worker:

**In scope**

- `util/`: math, ring buffers, bitmaps, parsing helpers
- `core/options`: CLI argument validation and translation
- `cli/cli`: top-level argument parser
- `cpu/stat`: `/proc/stat`-style delta computation, fed with synthetic
  snapshots
- `cpu/manager_random_normal`: load-distribution math built on top of
  `util::NormalDistribution`

**Out of scope (by design)**

- The actual CPU busy loops (`cpu/critical_loop`, `cpu/worker`): timing
  sensitive, not meaningfully testable without flakiness.
- Real memory allocation / XOR-fill (`memory/`): integration territory.
- GPU backends (`gpu/nvidia`, `gpu/amd`, `gpu/apple`): require vendor
  drivers and physical devices; CI workers do not have these. If
  needed, mock the `gpu::Device` interface and test `gpu::Manager` in
  isolation.
- Logger and dynamic-loader plumbing (`util/log`, `util/dl`): pure
  side effects, low regression risk.

## Adding a new test

1. Create `tests/<module>/<name>_test.cc`.
2. Add one entry in `tests/CMakeLists.txt` via `loadmaster_add_test`:

   ```cmake
   loadmaster_add_test(util_my_thing_test
       util/my_thing_test.cc
       # If the test exercises a .cc (not a header-only template),
       # list its source here so we don't have to depend on the main
       # `loadmaster` target. Keep the dependency surface minimal.
       ${CMAKE_SOURCE_DIR}/src/util/my_thing.cc
   )
   ```

3. Inside the test file, include the header by its in-`src/` path
   (e.g. `#include "util/my_thing.h"`); the test target already adds
   `src/` to its include directories.
4. Reconfigure (`cmake -B build_tests ...`) once so CMake picks up the
   new file, then `cmake --build build_tests -j && ctest --test-dir
   build_tests`.

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
- On MSVC, `gtest_force_shared_crt` is left **on** in
  `tests/CMakeLists.txt`. The main `loadmaster` target uses
  `/MT` (static CRT) in release builds; tests do not inherit that
  setting because it would force-rebuild gtest with `/MT` and
  complicate the FetchContent cache. If you need to test code that is
  sensitive to CRT flavor, build the test binary separately with the
  matching runtime.
- Tests that touch Linux-specific surfaces (e.g. `/proc/stat` parsing)
  are guarded with `#if defined(__linux__)` so non-Linux CI workers
  silently skip them.

## IDE setup

The top-level `CMakeLists.txt` sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`,
so configuring with `LOADMASTER_BUILD_TESTS=ON` produces
`build_tests/compile_commands.json` that includes GoogleTest's header
paths. Point your editor at it (VSCode C/C++: `"compileCommands":
"${workspaceFolder}/build_tests/compile_commands.json"`; clangd / Neovim
should pick it up automatically) and IntelliSense / clangd will resolve
`#include <gtest/gtest.h>` correctly. Without this step you may see
red squiggles on the gtest include even though the build itself passes.
