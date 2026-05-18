#pragma once

#include <atomic>
#include <memory>
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

  // Probe the device's base loop count for one full scheduling period.
  // Must be called before Start().
  void CalibrateBaseLoopCount();

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
  int base_loop_count_;
  std::atomic<int> load_set_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
};

}  // namespace gpu
