#pragma once

#include "resmgr.h"

namespace cpu {

class CpuWorkerContext final : public WorkerContext {
 public:
  int load_set_;
  CpuWorkerContext(int id, util::WaitGroup &wg) : WorkerContext(id, wg), load_set_(0) {}
  void Loop();
};

}  // namespace cpu
