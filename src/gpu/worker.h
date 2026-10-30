#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <thread>

#include "constants.h"
#include "device.h"
#include "mem_traffic.h"

namespace gpu {

// Owns one GpuDevice and the worker thread that drives it. Same busy/sleep
// model as cpu::CpuWorkerContext: each kScheduleIntervalMS period, run the
// kernel up to `load_set_` milliseconds of busy work, then sleep the rest.
//
// While other processes use the device it runs only what the budget leaves
// over after their share (see SetOthers).
//
// The worker thread also owns the optional `-gm` block: it allocates it on
// start, frees it while another process uses the device, and allocates it
// again once alone, so device calls stay on one thread at a time.
class GpuWorkerContext final {
 public:
  // `traffic_seed` seeds the fluctuation of the `-gm` block's traffic
  // (fixed in tests).
  GpuWorkerContext(int id, std::unique_ptr<GpuDevice> device,
                   std::uint32_t traffic_seed = std::random_device{}());
  ~GpuWorkerContext();

  GpuWorkerContext(const GpuWorkerContext &) = delete;
  GpuWorkerContext &operator=(const GpuWorkerContext &) = delete;
  GpuWorkerContext(GpuWorkerContext &&) = delete;
  GpuWorkerContext &operator=(GpuWorkerContext &&) = delete;

  // Find the loop count for which one kernel launch takes a full
  // scheduling period. Call before Start(); a worker started without it
  // calibrates on its own thread before its first launch (a device that
  // others were using at startup). Returns false when the device cannot run the
  // kernel at all; the caller should drop it.
  bool CalibrateBaseLoopCount();

  void Start();
  void RequestStop();
  void Join();

  // Update the per-tick busy budget in milliseconds (0..kScheduleIntervalMS).
  void SetLoadSet(int load_ms) { load_set_.store(load_ms, std::memory_order_relaxed); }

  // Device memory to hold while the device is ours alone (`-gm`). Call
  // before Start().
  void SetMemoryBytes(size_t bytes) { memory_bytes_ = bytes; }

  // Share the device with other processes that take `percent` of it: no
  // `-gm` block, and of the busy budget only what is left after their
  // share (nothing at 100). nullopt: the device is ours alone again. Takes
  // effect at the worker's next tick.
  void SetOthers(std::optional<int> percent) {
    others_percent_.store(percent ? std::clamp(*percent, 0, kGpuMaxLoadPerDevice) : -1,
                          std::memory_order_relaxed);
  }
  bool Sharing() const { return others_percent_.load(std::memory_order_relaxed) >= 0; }

  // Kernel time run on the device so far, in ms (calibration included).
  double BusyMs() const {
    return static_cast<double>(busy_us_.load(std::memory_order_relaxed)) / 1000.0;
  }

  GpuDevice *device() const { return device_.get(); }
  int id() const { return id_; }
  // Whether a calibration succeeded. Read before Start() or after Join().
  bool calibrated() const { return calibrated_; }

 private:
  int id_;
  std::unique_ptr<GpuDevice> device_;
  // Loop count for one full kScheduleIntervalMS of kernel time, valid once
  // calibrated_. Written by CalibrateBaseLoopCount() before Start(),
  // afterwards only by the worker.
  int base_loop_count_;
  bool calibrated_;
  std::atomic<int> load_set_;
  // Percent of the device other processes take, -1 when there are none.
  std::atomic<int> others_percent_;
  std::atomic<int64_t> busy_us_;
  // `-gm` block; the state below is touched by the worker thread only.
  size_t memory_bytes_;
  bool memory_held_;
  // An allocation failed: don't retry every tick, only after the next
  // spell of sharing.
  bool memory_failed_;
  // Fluctuation of the block's traffic around its mean, and when it was
  // last stepped.
  MemTrafficEnvelope traffic_envelope_;
  std::optional<std::chrono::steady_clock::time_point> traffic_stepped_at_;
  // What the block's traffic costs in kernel time, ms per MiB, and the
  // fit it comes from (see UpdateMemCost): exponentially weighted sums
  // over the launches that moved some, of x = MiB and y = wall time minus
  // the ALU loop's predicted time.
  struct MemCostFit {
    int launches = 0;
    double w = 0.0, x = 0.0, y = 0.0, xx = 0.0, xy = 0.0;
  };
  double mem_ms_per_mib_;
  MemCostFit mem_cost_fit_;
  // Mean traffic at full load, set from the device's bandwidth when the
  // block is first allocated (0 until then).
  double traffic_bytes_per_second_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
  // Free the `-gm` block while sharing the device, allocate it otherwise.
  void SyncMemory(bool sharing);
  // Run the kernel once and return its wall time in ms, or nullopt on
  // failure. Counts towards BusyMs().
  std::optional<double> TimeKernelMs(int loop_count);
  // Median of three timed runs (robust against a single hiccup).
  std::optional<double> MedianKernelMs(int loop_count);
  // Nudge base_loop_count_ towards the observed speed of the device, given
  // the ALU loop's predicted and observed time.
  void AdaptBaseLoopCount(double alu_target_ms, double alu_elapsed_ms);
  // Refine mem_ms_per_mib_ from a launch that moved `mib` MiB and took
  // `residual_ms` longer than its ALU loop alone should have.
  void UpdateMemCost(double mib, double residual_ms);
};

}  // namespace gpu
