# loadmaster
loadmaster is designed to waste your machine performance. Powerful, flexible, simple.

# Prerequisites
- C++ compiler with C++20 support
- CMake >= 3.12

# Build & Run
- Typical CMake building routine (see `release_build.sh` for a one-shot
  local helper).
- libstdc++/libgcc are linked statically by default so the binary is portable
  across distros; libc and libdl remain dynamic so the GPU module can `dlopen`
  the vendor drivers. Disable via `-DLOADMASTER_STATIC_LINK=OFF`.
- Customizable runtime arguments specified by CLI args - see `<exe> -h`.
- It's recommended to rename the executable as you want.

## Portable release builds
Because we link glibc dynamically on Linux (to keep `dlopen` working for
the GPU module), a binary compiled on a modern distro will refuse to
start on machines with an older glibc. To produce binaries that run on
virtually any distro from the last decade, use the helper scripts in
`scripts/`:

| Target | Script | Output |
|--------|--------|--------|
| Linux x86_64   | `scripts/build_linux.sh`               (host auto-detected) | `dist/loadmaster-x86_64`             |
| Linux aarch64  | `scripts/build_linux.sh --arch aarch64` (run on aarch64 host) | `dist/loadmaster-aarch64`            |
| Windows x86_64 | `scripts\build_windows.ps1`            (Windows + VS 2022)   | `dist/loadmaster-windows-x86_64.exe` |

The Linux script only requires Docker. It builds inside the
manylinux2014 image (CentOS 7 / glibc 2.17), strips the result, and
drops it into `dist/`. By default it targets the host's architecture
(`x86_64` or `aarch64`); pass `--arch` to override. Cross-architecture
builds (e.g. arm64 from x86_64) work via qemu-user-static binfmt but
are much slower; running on a native host of the target arch is
recommended.

The Windows script uses MSVC 2022 + CMake (both bundled with "Build
Tools for Visual Studio 2022") and statically links the C/C++ runtime
(`/MT`), so the resulting `.exe` does not need the Visual C++
Redistributable on the target machine. The NVIDIA GPU path works out of
the box with any standard NVIDIA driver install (`nvcuda.dll` lives in
`C:\Windows\System32`); the AMD GPU path requires the AMD HIP SDK for
Windows installed and on the `PATH` of the final user.

# Workload
## CPU
- Load range is [0, 100] (each core)
- Scheduling interval is 100 milliseconds (`kScheduleIntervalMS`)
- Each scheduling tick estimates the load contributed by *other* processes and
  adjusts our target so the *total* system load tends toward the requested value.
- Specify target load by `-l <load>`; default: 200 (`kDefaultCpuLoad`)
- Specify target worker thread number by `-c <count>`; default: minimum required by load
- Specify target scheduling algorithm (described below) by `-ca <algorithm>`
### Default Scheduler
- Try to dispatch target load to each worker thread uniformly
### Random Normal Scheduler
- Worker thread load changes every 10 seconds
- Average load over 5 minutes is the specified value
- Normal distribution is used in load calculation

## Memory
This module is disabled by default. Use `-m <memory_mib>` to specify an extra
memory usage, then this process won't look so weird.
### Default Scheduler
- Memory usage changes every 45 seconds (`kMemoryScheduleIntervalSecond`)
- Range: `memory_mib` * `rand(kMemoryMinimumRatio, 1.0)`, 4 KiB aligned
- Each new block is overwritten (XOR-fill) to force physical commit.
- When the new block size is >= 32 MiB (`kMemoryNoThreadSpawnThresholdMiB`), the
  allocate-and-fill work is done in a one-shot background thread so the main
  scheduling loop never blocks on a large `memset`/page-fault storm.

## GPU
Disabled by default. Use `-g <load>` and/or `-gm <mib>` to enable.

- `-g  <load>`     per-device compute load in `[0, 100]`. 0 means disabled.
- `-gm <mib>`      per-device device-memory load in MiB. 0 means no extra memory.
- `-gi <indices>`  comma-separated device indices, e.g. `0`, `0,2,3`, or `all`
                   (default).
- `-gv <vendor>`   `auto` (default), `nvidia`, or `amd`. `auto` prefers NVIDIA
                   and falls back to AMD.

Each selected device gets its own worker thread that runs the same busy/sleep
pattern as the CPU workers: each `kScheduleIntervalMS` tick, the worker
launches a busy kernel sized to occupy `load%` of the period, then sleeps for
the remainder. A one-time calibration probe at startup tunes the per-thread
loop count to the actual device.

### Runtime dependencies
The GPU module is fully **runtime-loaded** via `dlopen`. The loadmaster binary
itself does not link against any GPU library, so a build runs on any machine
regardless of which (if any) GPU vendor stack is installed:

| Vendor | Dependencies (loaded at runtime) | Source of kernel code |
|--------|----------------------------------|----------------------|
| NVIDIA | `libcuda.so.1` (NVIDIA driver) | embedded PTX, JITed by the driver |
| AMD    | `libamdhip64.so` + `libhiprtc.so` (ROCm runtime) | embedded HIP C++ source, compiled at startup by hipRTC |

If the requested vendor's libraries aren't found, the module logs and disables
itself; other modules (CPU/memory) still run normally.

### Notes
- Mixed-vendor hosts (e.g., NVIDIA + AMD installed at the same time) are
  intentionally out of scope: with `-gv auto` only one vendor is used.
- WSL2 is supported on NVIDIA via the `/usr/lib/wsl/lib/libcuda.so.1` shim.
- WSL2 is **not** supported on AMD: ROCm's WSL story is preview-only and
  doesn't expose `/dev/kfd` for most cards (including RDNA4 like RX 9070 XT).
  On WSL2 the AMD path short-circuits with a clear diagnostic and the rest
  of loadmaster (CPU/Memory/NVIDIA) keeps working. Run on native Linux or
  on Windows with the HIP SDK for full AMD support.
- On Windows the runtime libraries we look for are `nvcuda.dll` (NVIDIA,
  ships with the driver) and `amdhip64*.dll` + `hiprtc*.dll` (AMD, ships
  with the HIP SDK).

# Usage Sample
```bash
# CPU + memory
$ ./loadmaster -l 180 -ca rand_normal -m 32

# Same, but also drive GPU #0 at 60% with 256 MiB of VRAM occupied
$ ./loadmaster -l 180 -m 32 -g 60 -gm 256 -gi 0
```
