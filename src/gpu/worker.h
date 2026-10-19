#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#include "device.h"

namespace gpu {

// Owns one GpuDevice and the worker thread that drives it. Same busy/sleep
// model as cpu::CpuWorkerContext: each kScheduleIntervalMS period, run the
// kernel up to `load_set_` milliseconds of busy work, then sleep the rest.
class GpuWorkerContext final {
 public:
  GpuWorkerContext(int id, std::unique_ptr<GpuDevice> device);
  ~GpuWorkerContext();

  GpuWorkerContext(const GpuWorkerContext &) = delete;
  GpuWorkerContext &operator=(const GpuWorkerContext &) = delete;
  GpuWorkerContext(GpuWorkerContext &&) = delete;
  GpuWorkerContext &operator=(GpuWorkerContext &&) = delete;

  // Find the loop count for which one kernel launch takes a full
  // scheduling period. Must be called before Start(). Returns false when
  // the device cannot run the kernel at all; the caller should drop it.
  bool CalibrateBaseLoopCount();

  void Start();
  void RequestStop();
  void Join();

  // Update the per-tick busy budget in milliseconds (0..kScheduleIntervalMS).
  void SetLoadSet(int load_ms) { load_set_.store(load_ms, std::memory_order_relaxed); }

  GpuDevice *device() const { return device_.get(); }
  int id() const { return id_; }

 private:
  int id_;
  std::unique_ptr<GpuDevice> device_;
  // Loop count for one full kScheduleIntervalMS of kernel time. Written by
  // CalibrateBaseLoopCount() before Start(), afterwards only by the worker.
  int base_loop_count_;
  std::atomic<int> load_set_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
  // Run the kernel once and return its wall time in ms, or nullopt on failure.
  std::optional<double> TimeKernelMs(int loop_count);
  // Median of three timed runs (robust against a single hiccup).
  std::optional<double> MedianKernelMs(int loop_count);
  // Nudge base_loop_count_ towards the observed speed of the device.
  void AdaptBaseLoopCount(int target_ms, double elapsed_ms);
};

}  // namespace gpu
