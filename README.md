# loadmaster
loadmaster(负载大师) is designed to waste your machine performance. Powerful, flexible, simple. 

# Prerequisites
- Linux
- CMake 3.1 +
- C++ compiler with C++11 support

# Build & Run
- Typical CMake building routine
- Statically linked by default
- Customizable runtime arguments specified by CLI args - see `<exe> -h`
- It's recommend to rename the executable as you want

# Workload
## CPU
- Load range is [0, 100] (each core)
- System overall CPU load 60% is a threshold; loadmaster CPU module will be temporarily disabled 
- Scheduling interval is 100 milliseconds (`kScheduleIntervalMS`)
- Specify target load by `-l <load>`; default: 200 (`kDefaultCpuLoad`)
- Specify target worker thread number by `-c <count>`; default: minimum required by load
- Specify target scheduling algorithm (described below) by `-ca <algorithm>`
### Default Scheduler
- Try to dispatch target load to each worker thread uniformly 
### Random Normal Scheduler
- Worker thread load changes every 10 seconds
- Average load in 5 minutes is the specified value
- Normal distribution is used in load calculation

## Memory
This module is disabled by default. Use `-m <memory_mib>` to specify an extra memory usage, then this process won't be seemed so weird. 
### Default Scheduler
- Memory usage changes every 45 seconds (`kMemoryScheduleIntervalSecond`)
- Range: `memory_mib` \*  `rand(kMemoryMinimumRatio, 1.0)`,  4KiB aligned
- Memory block will be filled, and if block size larger than 32 MiB (`kMemoryNoThreadSpawnThresholdMiB`), a new thread will be temporarily spawned in order to allocate and fill memory

# Usage Sample
```bash
$ ./loadmaster -l 160 -ca rand_normal -m 32
```
