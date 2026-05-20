#pragma once

#include <atomic>
#include <thread>

namespace cpu {

// One CPU busy worker. Owns the worker thread and the per-worker tunables.
//
// Lifetime:
//   ctor -> SetLoadSet() ... -> Start() -> [SetLoadSet() ...] -> RequestStop() -> Join() -> dtor
//
// Threading:
//   * SetLoadSet() may be called from any thread; it just stores into an
//     atomic and is observed by the worker on its next iteration.
//   * base_loop_count_ is owned exclusively by the worker thread (used for
//     the runtime feedback loop) and is NEVER read/written from outside.
class CpuWorkerContext final {
 public:
  CpuWorkerContext(int id, int initial_base_loop_count);
  ~CpuWorkerContext();

  CpuWorkerContext(const CpuWorkerContext &) = delete;
  CpuWorkerContext &operator=(const CpuWorkerContext &) = delete;
  CpuWorkerContext(CpuWorkerContext &&) = delete;
  CpuWorkerContext &operator=(CpuWorkerContext &&) = delete;

  void Start();
  void RequestStop();
  void Join();

  // Per-tick busy budget in milliseconds, [0, kScheduleIntervalMS].
  void SetLoadSet(int load_ms) { load_set_.store(load_ms, std::memory_order_relaxed); }
  int GetLoadSet() const { return load_set_.load(std::memory_order_relaxed); }

  int Id() const { return id_; }

 private:
  int id_;
  std::atomic<int> load_set_;
  // Runtime-tuned count of CriticalLoop iterations whose total wall time
  // is approximately kCpuSchedulingGranularityNS (~1ms). Adjusted each
  // scheduling tick by the worker thread itself based on observed timing,
  // so we follow CPU frequency scaling / thermal throttling automatically.
  int base_loop_count_;
  // EMA of microseconds spent per CriticalLoop(base_loop_count_) call;
  // initialized at Start() and refined every tick.
  double ema_us_per_call_;
  std::jthread thread_;

  void Loop(std::stop_token stoken);
  // Update ema_us_per_call_ from the latest sample, then re-derive
  // base_loop_count_ so that one call again targets ~1ms.
  void UpdateBaseLoopCount(int iterations, int64_t busy_us);
};

}  // namespace cpu
