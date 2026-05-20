#pragma once

#ifndef IS_WINDOWS
#    if defined(_WIN32) || defined(_WIN64)
#        define IS_WINDOWS (1)
#    else
#        define IS_WINDOWS (0)
#    endif
#endif

// macOS / Darwin (XNU). Has POSIX dlopen/sigaction/sysconf, but no /proc
// filesystem -- CPU / memory stats use Mach + libproc instead.
#ifndef IS_MACOS
#    if defined(__APPLE__) && defined(__MACH__)
#        define IS_MACOS (1)
#    else
#        define IS_MACOS (0)
#    endif
#endif

// Linux (specifically: /proc-based stats path). Anything that is neither
// Windows nor macOS is currently treated as Linux.
#ifndef IS_LINUX
#    if !IS_WINDOWS && !IS_MACOS
#        define IS_LINUX (1)
#    else
#        define IS_LINUX (0)
#    endif
#endif

#if IS_WINDOWS
// Avoid <windows.h> dragging in winsock1, GDI, and the min/max macros that
// would clobber std::min / std::max. These #defines must precede every
// inclusion of <windows.h> in the project.
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

#include <cstdint>

// Default options
constexpr int kDefaultCpuLoad = 200;
constexpr int kDefaultMemoryLoadMiB = 0;
constexpr int kDefaultGpuLoad = 0;
constexpr int kDefaultGpuMemoryMiB = 0;

// General
constexpr int kSmallBufferLength = 128;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kScheduleIntervalMS = 100;
constexpr int kScheduleFrequency = kMillisecondsPerSecond / kScheduleIntervalMS;
constexpr int64_t kKibiByte = 1024;
constexpr int64_t kMebiByte = 1024 * kKibiByte;
constexpr int64_t kGibiByte = 1024 * kMebiByte;

// CPU
constexpr int kCpuSchedulingGranularityNS = 1 * 1000 * 1000;  // 1 ms
constexpr int kCpuMaxLoadPerCore = 100;
constexpr int kCpuBaseLoopCountMin = 0;
constexpr int kCpuBaseLoopCountMax = 25'000'000;
constexpr int kCpuBaseLoopCountTestIteration = 16;
constexpr double kCpuRandNormalMu = 0.0;
constexpr double kCpuRandNormalSigma = 2.0;
constexpr double kCpuRandNormalCdfTarget = 0.95;
constexpr int kCpuRandNormalSchedulePeriodMS = 600'000;   // 5min
constexpr int kCpuRandNormalScheduleIntervalMS = 10'000;  // 10s
constexpr int kCpuRandNormalSchedulePointCount =
    kCpuRandNormalSchedulePeriodMS / kCpuRandNormalScheduleIntervalMS;
constexpr int kCpuAvgLoadSamplingCount = 100;

// Memory
constexpr int kMemoryScheduleIntervalSecond = 45;
constexpr double kMemoryMinimumRatio = 0.5;
constexpr int kMemoryNoThreadSpawnThresholdMiB = 32;

// GPU
// Per-device load is in the range [0, 100], expressed as the fraction of
// each `kScheduleIntervalMS` tick spent submitting / waiting on the busy
// kernel. The kernel itself does pure ALU work (32-bit integer madd
// chains) and is launched with a fixed grid size; we tune the per-thread
// loop count once at startup so a "100% busy" tick equals one full
// scheduling period.
constexpr int kGpuMaxLoadPerDevice = 100;
constexpr int kGpuKernelGridSize = 1024;
constexpr int kGpuKernelBlockSize = 256;
constexpr int kGpuKernelLoopBaseMin = 0;
constexpr int kGpuKernelLoopBaseMax = 50'000'000;
constexpr int kGpuKernelLoopBaseTestIteration = 12;
