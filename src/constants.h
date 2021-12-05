#pragma once

#include "errcode.h"

constexpr int kDefaultCpuLoad = 200;
constexpr int kMaxLoadPerCore = 100;
constexpr int kBaseLoopCount = 1000000;
constexpr int kScheduleIntervalMS = 100;
constexpr int kPauseLoopCpuThreshold = 60;  // Don't run empty loop when total CPU reaches this
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kSmallBufferLength = 128;
