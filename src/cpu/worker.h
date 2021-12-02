#pragma once

#include <thread>

class Runtime;

namespace cpu {

class ThreadContext {  // todo: rename to CpuWorkerContext
 public:
  int id_;
  int load_max_;
  int load_set_;
  std::thread thread_;
  ThreadContext() : id_(0), load_max_(0), load_set_(0) {}
};

void WorkerThreadProcedure(Runtime &runtime, ThreadContext &ctx);

}  // namespace cpu
