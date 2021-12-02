#pragma once

#include "cpu/worker.h"
#include "util/waitgroup.h"

#include <atomic>
#include <thread>
#include <vector>

class Options;

class Runtime {
 public:
  int jiffy_ms_;
  const Options &options_;
  util::WaitGroup<int> wg_;
  std::vector<cpu::ThreadContext> cpu_threads_;

  Runtime(const Options &options);
  void InitThreads();
  void CreateWorkerThreads();
  void JoinThreads();
};
