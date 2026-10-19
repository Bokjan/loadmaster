# loadmaster
loadmaster is designed to waste your machine performance. Powerful, flexible, simple.

# Prerequisites
- C++ compiler with C++20 support
- CMake >= 3.15

# Build & Run
- Typical CMake building routine (see `scripts/build_local.sh` for a one-shot local helper), or a preset from `CMakePresets.json` with CMake 3.21+: `cmake --preset release && cmake --build --preset release` (`debug` / `release`: with tests, `sanitize`: ASan + UBSan, `tsan`: ThreadSanitizer, plus `macos-arm64-*` and `windows-*`; `ctest --preset <name>` runs the tests). Presets build under `build/<preset>/`.
- The scripts in `scripts/` use Ninja for new build trees when it is installed, and CMake uses `ccache` when it is installed.
- CI (`.github/workflows/ci.yml`) runs the unit tests on Linux (GCC, Clang, ASan + UBSan, TSan; x86_64 and aarch64), macOS arm64, Windows and FreeBSD with warnings as errors. Pushing a `v<version>` tag runs `.github/workflows/release.yml`, which builds the release artifacts below with the `scripts/build_*` scripts, checks them with `scripts/check_obfuscation.sh`, and publishes them with their checksums as the latest GitHub release.
- libstdc++/libgcc are linked statically by default so the binary is portable across distros; libc and libdl remain dynamic so the GPU module can `dlopen` the vendor drivers. Disable via `-DLOADMASTER_STATIC_LINK=OFF`.
- Customizable runtime arguments specified by CLI args - see `<exe> -h`.
- It's recommended to rename the executable as you want.

## Development hooks
Local git hooks enforce two things before a commit:

- **`pre-commit`** — rejects the commit if any staged `.h` / `.cc` file is not clang-format clean (repo-root `.clang-format`, Google base, ColumnLimit 100). Run `clang-format -i` on the offending files, `git add` them, and commit again.
- **`commit-msg`** — rejects the commit if the message hard-wraps a paragraph (each paragraph must stay on one logical line; the renderer soft-wraps).

The hooks live in `tools/hooks/` and delegate to `tools/check_clang_format.py` and `tools/check_commit_messages.py` (both check-only; they never rewrite anything). Activate them with:

```bash
git config core.hooksPath tools/hooks
```

This is a **local** setting (per clone), so each contributor runs it once after cloning. `clang-format` must be on `PATH` for the `pre-commit` hook to run (otherwise the commit is blocked with an install hint).

## Build options
| CMake variable | Default | Effect |
|----------------|---------|--------|
| `LOADMASTER_STATIC_LINK` | `ON` | Statically link the C++ / runtime into the executable so it has no `vcredist` / `libstdc++`-version dependency. On Linux this is **semi-static** (`-static-libstdc++ -static-libgcc`): `libc` / `libdl` / `libpthread` stay dynamic because the GPU module needs `dlopen` to load the vendor drivers, which is incompatible with a fully-`-static` glibc binary. Cross-distro portability there is achieved by building inside the manylinux2014 container (see `scripts/build_linux.sh`) rather than by full static linking. |
| `LOADMASTER_OBFUSCATE` | `ON` | XOR-mask embedded GPU kernel sources, the SPIR-V module, and the CLI version / help text in `.rodata` so `strings` and AV heuristics don't trip on them. Runtime output is unchanged. Turn off when bisecting a kernel build / JIT failure. |
| `LOADMASTER_LOG_MIN_LEVEL` | `trace` | Strips `LOG_*` sites below this level at preprocessor time. Lower-priority sites expand to `((void)0)`, so their format strings, `__FILE__` paths, and helper code never enter the binary. Bump to `info` / `warn` for a quieter, leaner release; `LOG_FATAL` is never compiled out. |
| `LOADMASTER_SANITIZE` | *(empty)* | Comma-separated sanitizers for every target, tests included, e.g. `address,undefined` or `thread` (GCC / Clang) or `address` (MSVC). Disables `LOADMASTER_STATIC_LINK`. `scripts/run_tests.sh --sanitize` builds the test suite this way in `build_tests_sanitize/`. |
| `LOADMASTER_LINK_COMPILE_COMMANDS` | `ON` | Mirror the build dir's `compile_commands.json` into the source root (symlink, or copy on Windows) for clangd / IDEs. Turn off for throwaway or containerised builds so they don't repoint your link; the release scripts and all presets but `debug` do. `CMAKE_EXPORT_COMPILE_COMMANDS` itself defaults to `ON` and can be overridden. |
| `LOADMASTER_BUILD_TESTS` | `OFF` | Build the GoogleTest suite (`scripts/run_tests.sh` / `run_tests.ps1` and the presets turn it on). |
| `LOADMASTER_SYSTEM_GTEST` | `ON` | Use an installed GoogleTest when CMake finds one instead of downloading it (not for sanitizer or MSVC builds). |
| `LOADMASTER_HARDEN` | `ON` | Exploit mitigations, each used where the toolchain supports it: PIE, `-fstack-protector-strong`, `-fstack-clash-protection`, CET (`-fcf-protection`, x86) / PAC+BTI (`-mbranch-protection`, AArch64), `_FORTIFY_SOURCE=2` (optimised, non-sanitizer builds), full RELRO on ELF; `/guard:cf` and `/CETCOMPAT` on MSVC. PIE is dropped (with a configure message) when the toolchain's static `libstdc++` isn't position-independent. |
| `LOADMASTER_GC_SECTIONS` | `ON` | In optimised builds, let the linker drop unreferenced functions and data (`--gc-sections`, `-dead_strip`, `/OPT:REF,ICF`). |
| `LOADMASTER_LTO` | `OFF` | Link-time optimisation. Off because it barely shrinks this binary and widens what the optimiser can constant-fold across files, which the XOR obfuscation has to survive; if you enable it, run `scripts/check_obfuscation.sh` on the stripped binary. |
| `LOADMASTER_WERROR` | `OFF` | Treat warnings in loadmaster's own code and tests as errors (`-Werror` / `/WX`). CI turns it on. |
| `LOADMASTER_CCACHE` | `ON` | Use `ccache` as the compiler launcher when it is installed (Makefile / Ninja generators). |


## Portable release builds
Because we link glibc dynamically on Linux (to keep `dlopen` working for the GPU module), a binary compiled on a modern distro will refuse to start on machines with an older glibc. To produce binaries that run on virtually any distro from the last decade, use the helper scripts in `scripts/`:

| Target | Script | Output |
|--------|--------|--------|
| Linux x86_64   | `scripts/build_linux.sh`               (host auto-detected) | `dist/loadmaster-<version>-linux-x86_64`       |
| Linux aarch64  | `scripts/build_linux.sh --arch aarch64` (run on aarch64 host) | `dist/loadmaster-<version>-linux-aarch64`      |
| Windows x86_64 | `scripts\build_windows.ps1`            (Windows + VS 2022)   | `dist/loadmaster-<version>-windows-x86_64.exe` |
| macOS arm64    | `scripts/build_macos.sh`               (Apple Silicon host)  | `dist/loadmaster-<version>-macos-arm64`        |
| macOS x86_64   | `scripts/build_macos.sh --arch x86_64` (not published by CI) | `dist/loadmaster-<version>-macos-x86_64`       |

`<version>` is the one in `src/core/version.h` (what `-v` prints). Each artifact gets a `<artifact>.sha256` next to it (`sha256sum -c` / `shasum -a 256 -c` format). The scripts start from an empty build tree, as a release should; pass `--incremental` (`-Incremental` on Windows) to reuse it while iterating.

The Linux script only requires Docker. It builds inside the manylinux2014 image (CentOS 7 / glibc 2.17) as the invoking user (so the build tree stays yours), strips the result inside the container, and drops it into `dist/`. `--manylinux 2_28` builds in the manylinux_2_28 image (AlmaLinux 8 / glibc 2.28, newer GCC) instead, which drops CentOS / RHEL 7, Ubuntu < 18.10 and Debian < 10. It prints the image digest it used; set `MANYLINUX_IMAGE=quay.io/pypa/manylinux2014_<arch>@sha256:<digest>` to pin it for reproducible builds. By default it targets the host's architecture (`x86_64` or `aarch64`); pass `--arch` to override. Cross-architecture builds (e.g. arm64 from x86_64) work via qemu-user-static binfmt but are much slower; running on a native host of the target arch is recommended.

The Windows script uses MSVC 2022 + CMake (both bundled with "Build Tools for Visual Studio 2022") and statically links the C/C++ runtime (`/MT`), so the resulting `.exe` does not need the Visual C++ Redistributable on the target machine. The NVIDIA GPU path works out of the box with any standard NVIDIA driver install (`nvcuda.dll` lives in `C:\Windows\System32`); the AMD GPU path requires the AMD HIP SDK for Windows installed and on the `PATH` of the final user.

The macOS script uses the Xcode Command Line Tools (`AppleClang` + `libc++` + `ld64`) and CMake; install the former with `xcode-select --install` and the latter via Homebrew (`brew install cmake`) if needed. Unlike Linux/Windows the binary is *not* statically linked: Apple does not ship static archives for `libc++`/`libSystem`, so `loadmaster` links against the OS-provided dylibs (which are part of the macOS ABI guarantee). Pick the deployment target via `CMAKE_OSX_DEPLOYMENT_TARGET` (the script defaults to 11.0). The GPU module is supported on macOS via a built-in Metal backend (see the [Runtime dependencies](#runtime-dependencies) section below); CPU and memory modules behave the same as on Linux/Windows.

## FreeBSD / DragonFly
Source builds work out of the box: install `cmake` from `pkg` (`pkg install cmake`; the base system already ships `clang` + `libc++`) and run the standard `cmake -S . -B build && cmake --build build -j 4` (bmake needs an explicit job count), or use `scripts/build_local.sh`. The CPU + memory modules work identically to Linux; the GPU module's `dlopen`-based backends will all self-disable (no ROCm / Level Zero / CUDA on BSD), with OpenCL the only one that may light up if you've installed an OpenCL ICD (e.g. `pocl`).

There is intentionally **no** "build once, run anywhere" script for BSD: unlike Linux's manylinux story, BSDs don't promise ABI compatibility across major releases, so the recommended distribution model is "build on the target host's OS major version, ship per-major binaries". OpenBSD / NetBSD are not yet wired up -- they share the same MIBs but their `kinfo_proc` layout differs; see comments in `src/cpu/stat_bsd.cc` for the small remaining work.

# Workload
## CPU
- Load is in "percent of one core" units: 100 per core, so a fully busy 4-core machine is 400. Valid range is `0` to `100 x threads`, where threads is `-c` or, without it, the number of CPUs this process may use (on Linux this honours `taskset` / cpusets and a cgroup v2 `cpu.max` quota). `-l 0` disables the CPU module.
- Scheduling interval is 100 milliseconds (`kScheduleIntervalMS`)
- Each scheduling tick estimates the load contributed by *other* processes and adjusts ours so the *total* load tends toward the requested value (a feedforward term plus a small integral correction, see `src/cpu/load_controller.h`). On Linux, when the process is restricted to a subset of CPUs, "total" means the total over those CPUs.
- Specify target load by `-l <load>`; default: 200 (`kDefaultCpuLoad`), capped to `100 x threads` on smaller machines
- Specify target worker thread number by `-c <count>`; default (or `0`): minimum required by load
- Specify target scheduling algorithm (described below) by `-ca <algorithm>`
### Default Scheduler
- Try to dispatch target load to each worker thread uniformly
### Random Normal Scheduler
- Target load changes every 10 seconds
- Average load over each 10-minute period (60 points) is exactly the specified value
- The points follow a normal distribution (the central 95%, about 0.24x to 1.65x of the average); `-c` must leave room for the peak

## Memory
This module is disabled by default. Use `-m <memory_mib>` to specify an extra memory usage, then this process won't look so weird.
### Default Scheduler
- Memory usage changes every 45 seconds (`kMemoryScheduleIntervalSecond`)
- Range: `memory_mib` * `rand(kMemoryMinimumRatio, 1.0)`, 4 KiB aligned
- Each new block is fully written once to force physical commit.
- When the new block size is >= 32 MiB (`kMemoryBackgroundThreadThresholdMiB`), the allocate-and-fill work is done in a one-shot background thread so the main scheduling loop never blocks on a large `memset`/page-fault storm; shutdown interrupts it between 4 MiB chunks.

## GPU
Disabled by default. Use `-g <load>` and/or `-gm <mib>` to enable.

- `-g  <load>`     per-device compute load in `[0, 100]`. 0 means disabled.
- `-gm <mib>`      per-device device-memory load in MiB. 0 means no extra memory.
- `-gi <indices>`  comma-separated device indices, e.g. `0`, `0,2,3`, or `all` (default). Indices are local to the backend in use; indices that don't exist are reported.
- `-gv <vendor>`   `auto` (default), `nvidia`, `amd`, `intel`, `opencl`, or `apple`. `auto` prefers Apple Metal on macOS, otherwise tries NVIDIA -> AMD (HIP) -> Intel (Level Zero) -> OpenCL and uses the first backend that has any device (even if `-gi` selects none of them). The OpenCL fallback is what covers Intel iGPUs without Level Zero and AMD APU iGPUs (which ROCm/HIP can't drive).
- `-ga <algo>`     `default` (default) or `rand_normal`. With `rand_normal` each device independently walks a shuffled normal-distribution-shaped load schedule, so the *10-minute average* matches `-g <load>` while the instantaneous load fluctuates and multi-GPU hosts don't all spike in lockstep. Points are capped at 100%; what the cap clips off the peaks is spread over the other points, so the average still holds.

Each selected device gets its own worker thread that runs the same busy/sleep pattern as the CPU workers: each `kScheduleIntervalMS` tick, the worker launches a busy kernel sized to occupy `load%` of the period, then sleeps for the remainder (the host thread sleeps while the kernel runs; it does not spin). A calibration at startup tunes the per-thread loop count to the actual device and is then continuously corrected for clock boost / throttling. A device whose kernel can't run is dropped at startup, and one that fails 50 launches in a row is given up.

### Runtime dependencies
On Linux and Windows the GPU module is fully **runtime-loaded** via `dlopen` / `LoadLibrary`. The loadmaster binary itself does not link against any vendor GPU library, so a build runs on any machine regardless of which (if any) GPU vendor stack is installed:

| Vendor | Dependencies (loaded at runtime) | Source of kernel code |
|--------|----------------------------------|-----------------------|
| NVIDIA | `libcuda.so.1` (NVIDIA driver) | embedded PTX, JITed by the driver |
| AMD    | `libamdhip64.so` + `libhiprtc.so` (ROCm runtime) | embedded HIP C++ source, compiled at startup by hipRTC |
| Intel  | `libze_loader.so.1` / `ze_loader.dll` (Intel Graphics Driver / oneAPI Level Zero) | embedded SPIR-V, JITed by the driver |
| OpenCL | `libOpenCL.so.1` / `OpenCL.dll` (Khronos ICD loader; OS-shipped on Windows) | embedded OpenCL C source, compiled at startup by the ICD |
| Apple  | `Metal.framework` (linked at build time on macOS) | embedded MSL source, compiled at startup by `MTLDevice newLibraryWithSource:` |

If the requested vendor's libraries aren't found, the module logs and disables itself; other modules (CPU/memory) still run normally.

### Notes
- Mixed-vendor hosts (e.g., NVIDIA + AMD installed at the same time) are intentionally out of scope: with `-gv auto` only one vendor is used.
- Integrated-GPU support: Intel iGPUs (Iris Xe / UHD / Arc Graphics) go through the Intel Level Zero backend, which needs a precompiled SPIR-V kernel (`src/gpu/intel/busy_kernel.spv`, generated offline from `busy_kernel.cl`; see that file for `ocloc` / `clang+llvm-spirv` commands). Without that .spv the Intel path disables itself and `-gv auto` falls through to OpenCL, which also covers AMD APU iGPUs (e.g. Ryzen 7000G/8000G, Steam Deck) without ROCm.
- WSL2: NVIDIA works via the `/usr/lib/wsl/lib/libcuda.so.1` shim. AMD is **not** supported (ROCm doesn't expose `/dev/kfd` in WSL2); the AMD path short-circuits with a clear diagnostic and the rest of loadmaster keeps working. Intel L0 / OpenCL inside WSL2 requires Microsoft's Intel compute-runtime WSL package and is not exhaustively tested by us.
- On Windows, vendor-specific DLLs ship with the corresponding driver install (`nvcuda.dll`, `amdhip64*.dll` + `hiprtc*.dll`, `ze_loader.dll`); `OpenCL.dll` is part of the OS itself. They are looked up in `System32` and then in the absolute directories on `PATH` (HIP SDK) -- never in the current directory or next to `loadmaster.exe`.
- A GPU exposed by two OpenCL drivers (e.g. Mesa rusticl alongside ROCm or Intel NEO) is only used once, when the drivers report its PCI address or UUID. Large `-gm` values are allocated in chunks on OpenCL and Level Zero, whose single allocations are capped (often to a quarter of VRAM / ~4 GiB).
- On macOS the only supported GPU backend is Apple Metal (`-gv apple`, also picked by `-gv auto`). NVIDIA / AMD compute stacks have never been or are no longer available on Darwin. The Metal backend works on every Apple Silicon Mac and on Intel Macs with Metal-capable GPUs (any model from ~2012 onwards).
- On Apple Silicon the GPU shares system RAM (UMA), so `-gm <mib>` reserves that many MiB of physical memory rather than dedicated VRAM. Plan accordingly when running with `-gm` and a large `-m`.

# Exit status & signals
- Exit status is `0` after a normal run stopped by a signal (or `-h` / `-v`), and non-zero for invalid arguments or when no module could be enabled (for example `-l 0` without `-m` / `-g`, or a GPU-only run on a host without a usable GPU).
- `SIGINT` / `SIGTERM` stop loadmaster cleanly on POSIX. On Windows, Ctrl-C, Ctrl-Break, closing the console window, logoff and shutdown all request a clean stop.
- `-L <level>` picks the log level: `trace`, `debug`, `info`, `warn` (default), `error`, `fatal`, `off`; `all` is an alias for `trace`.

# Usage Sample
```bash
# CPU + memory
$ ./loadmaster -l 180 -ca rand_normal -m 32

# Same, but also drive GPU #0 at 60% with 256 MiB of VRAM occupied
$ ./loadmaster -l 180 -m 32 -g 60 -gm 256 -gi 0

# macOS: drive the Apple Silicon GPU at 80% (auto-picks Metal)
$ ./loadmaster -l 0 -g 80 -gm 512
```
