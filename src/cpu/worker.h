#pragma once

#include "resmgr.h"

namespace cpu {

class CpuWorkerContext final : public WorkerContext {
 public:
  CpuWorkerContext(int id, util::WaitGroup &wg, int base_loop_count)
      : WorkerContext(id, wg), load_set_(0), base_loop_count_(base_loop_count) {}
  void Loop();
  int GetLoadSet() const { return load_set_; }
  void SetLoadSet(int load) { load_set_ = load; }

 private:
  int load_set_;
  int base_loop_count_;
};

}  // namespace cpu
