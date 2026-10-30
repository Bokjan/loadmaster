#pragma once

#include <cstddef>

// GPU module tunables.
//
// Per-device load is in the range [0, 100], expressed as the fraction of
// each `kScheduleIntervalMS` tick spent submitting / waiting on the busy
// kernel. The kernel itself does pure ALU work (32-bit integer madd
// chains) and is launched with a fixed grid size; we tune the per-thread
// loop count once at startup so a "100% busy" tick equals one full
// scheduling period.
constexpr int kGpuMaxLoadPerDevice = 100;
constexpr int kGpuKernelGridSize = 1024;
constexpr int kGpuKernelBlockSize = 256;
// Upper bound for the per-thread loop count. Generous: on a top-end GPU
// the whole grid runs in a single wave, so one scheduling period
// (100 ms) can take several hundred million iterations per thread.
constexpr int kGpuKernelLoopBaseMax = 1'000'000'000;
// Calibration: a sample must take at least this long to be timed
// reliably; the estimate is accepted once within this relative error.
constexpr double kGpuCalibrationMinSampleMs = 5.0;
constexpr double kGpuCalibrationTolerance = 0.03;
constexpr int kGpuCalibrationRefineRounds = 3;
// Runtime drift correction (clock boost / throttle): only ticks with at
// least this much busy time are used, drift below the deadband is ignored.
constexpr int kGpuAdaptMinBusyMs = 10;
constexpr double kGpuAdaptDeadband = 0.05;
constexpr double kGpuAdaptSmoothing = 0.5;
// Device-memory traffic. While the kernel runs it also reads and writes
// back words of the `-gm` block, sweeping it end to end over successive
// launches, so the memory controller has work to do and the block isn't
// just an idle reservation. The mean rate follows the busy time: at -g 100
// this share of the device's peak memory bandwidth (GpuDevice::
// MemoryBandwidth), proportionally less below. Each word is loaded
// a whole stretch of ALU work before it is written back, so the latency
// stays hidden; what the transfers still cost is measured and taken out of
// the ALU loop (see GpuWorkerContext::Loop), so the busy time holds. An
// integrated GPU takes the traffic from system RAM.
constexpr double kGpuMemTrafficBandwidthShare = 0.01;
// The rate where the backend can't tell the bandwidth (OpenCL, Metal).
constexpr int kGpuMemTrafficMiBPerSecond = 1024;
// Real jobs keep the ALUs busy while their memory traffic comes and goes,
// so each device scales that mean by its own fluctuating factor: a
// mean-reverting random walk of the factor's logarithm (Ornstein-Uhlenbeck)
// with this stationary standard deviation and time constant, offset so the
// factor averages 1. With 0.8 it spends most of the time between ~0.15x and
// ~3.5x, drifting over a few seconds rather than jumping every tick.
constexpr double kGpuMemTrafficLogSigma = 0.8;
constexpr int kGpuMemTrafficTimeConstantMS = 3'000;
// Hard bounds on the factor (the cap trims the long-run mean by ~1%).
constexpr double kGpuMemTrafficMinFactor = 0.05;
constexpr double kGpuMemTrafficMaxFactor = 5.0;
// One access reads a 32-bit word and writes it back.
constexpr int kGpuMemTrafficBytesPerAccess = 8;
// The kernel indexes a window of the block with 32-bit word offsets (from
// a 64-bit window start), so the sweep walks it in windows of at most this
// size.
constexpr std::size_t kGpuMemSweepWindowMaxBytes = std::size_t{1} << 30;
// What the traffic costs in kernel time, in ms per MiB, is estimated from
// pairs of consecutive launches whose traffic differs by at least this
// fraction, and smoothed with this weight per pair. The ALU loop gives up
// at most this share of a launch's busy time to it.
constexpr double kGpuMemCostMinDelta = 0.1;
constexpr double kGpuMemCostSmoothing = 0.1;
constexpr double kGpuMemCostMaxShare = 0.5;
// After this many consecutive failed launches (~5 s of retries) the
// worker gives the device up instead of logging a warning forever.
constexpr int kGpuMaxConsecutiveFailures = 50;

// Sharing the device with other GPU users, the GPU counterpart of the CPU
// module filling only the gap other processes leave. Every kGpuYieldPollMS
// the contention monitor looks for other processes doing compute work on
// each device and measures how busy the whole device was. While it has
// others, the device frees its `-gm` block and runs only what `-g` leaves
// over after them: their share is the device's busy time minus our own
// (so time they ran while our kernel was queued isn't counted twice), at
// its highest over the last kGpuYieldResumeMS, so a job that pauses
// briefly doesn't get the GPU snatched back. Where nothing measures the
// device's busy time, other users count as keeping it fully busy (no
// kernels at all). The block comes back kGpuYieldResumeMS after the last
// sighting.
constexpr int kGpuYieldPollMS = 2'000;
constexpr int kGpuYieldResumeMS = 30'000;
// amdgpu's gpu_busy_percent is an instantaneous reading: average this many,
// this far apart.
constexpr int kGpuBusyPercentSamples = 10;
constexpr int kGpuBusyPercentSampleIntervalMS = 50;
// Sources that only see busy time (Windows GPU engine counters, macOS
// per-process GPU time) count another process once it keeps the device
// at least this busy over a poll.
constexpr double kGpuYieldMinBusyPercent = 5.0;
// Polls in a row in which no source could see a device before warning that
// it won't make room for other users, so one failed counter read doesn't
// trigger the warning.
constexpr int kGpuYieldUnknownWarnPolls = 3;

// GPU rand_normal scheduler. Mirrors the CPU rand_normal knobs: every
// device independently walks its own shuffled set of load points so that
// the *time-average* load matches the user-requested `-g <load>` while
// the instantaneous load fluctuates on a normal-distribution-shaped
// envelope. Scheduling cadence is much coarser than CPU's because GPU
// kernel launches are expensive and we don't want to thrash the queue.
constexpr double kGpuRandNormalMu = 0.0;
constexpr double kGpuRandNormalSigma = 2.0;
constexpr double kGpuRandNormalCdfTarget = 0.95;
constexpr int kGpuRandNormalSchedulePeriodMS = 600'000;   // 10min
constexpr int kGpuRandNormalScheduleIntervalMS = 10'000;  // 10s
constexpr int kGpuRandNormalSchedulePointCount =
    kGpuRandNormalSchedulePeriodMS / kGpuRandNormalScheduleIntervalMS;
