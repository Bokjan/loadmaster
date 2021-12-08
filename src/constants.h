#pragma once

#include "errcode.h"

#include <cstdint>

// Version
constexpr auto kVersionProject = "loadmaster";
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 1;
constexpr int kVersionPatch = 0;
constexpr auto kVersionSuffix = "beta.0";

// Default options
constexpr int kDefaultCpuLoad = 200;
constexpr int kDefaultMemoryLoadMiB = 0;

// General
constexpr int kSmallBufferLength = 128;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kScheduleIntervalMS = 100;
constexpr int64_t kKibiByte = 1024;
constexpr int64_t kMebiByte = 1024 * kKibiByte;
constexpr int64_t kGibiByte = 1024 * kMebiByte;

// CPU
constexpr int kCpuMaxLoadPerCore = 100;
constexpr int kCpuBaseLoopCount = 1000000;
constexpr int kCpuPauseLoopLoadPercentage = 60;  // Don't run empty loop when total CPU reaches this

// Memory
constexpr int kMemoryScheduleIntervalSecond = 45;
constexpr double kMemoryMinimumRatio = 0.5;
constexpr int kMemoryNoThreadSpawnThresholdMiB = 32;
