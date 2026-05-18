# loadmaster
loadmaster is designed to waste your machine performance. Powerful, flexible, simple.

# Prerequisites
- C++ compiler with C++20 support
- CMake >= 3.12

# Build & Run
- Typical CMake building routine (see `release_build.sh` for a one-shot helper).
- Statically linked by default on Linux. Disable via `-DLOADMASTER_STATIC_LINK=OFF`.
- Customizable runtime arguments specified by CLI args - see `<exe> -h`.
- It's recommended to rename the executable as you want.

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

# Usage Sample
```bash
$ ./loadmaster -l 180 -ca rand_normal -m 32
```
