#pragma once

#include "core/constants.h"

// Memory module tunables.
constexpr int kMemoryScheduleIntervalSecond = 45;
constexpr double kMemoryMinimumRatio = 0.5;
constexpr int kMemoryNoThreadSpawnThresholdMiB = 32;
