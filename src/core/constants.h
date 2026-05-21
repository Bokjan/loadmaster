#pragma once

// Project-wide, module-agnostic constants. Per-module constants live in
// the corresponding subdirectory (cpu/constants.h, memory/constants.h,
// gpu/constants.h). Platform detection macros live in core/platform.h.

#include <cstdint>

#include "platform.h"

// Default load values for CLI options. Module-specific tunables (limits,
// kernel sizes, sampling counts, ...) belong in their module's
// constants header.
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
