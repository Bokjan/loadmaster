#pragma once

#include "core/resource_manager.h"
#include "core/worker_context.h"

namespace cpu {

class CpuWorkerContext final : public core::WorkerContext {
 public:
  CpuWorkerContext(int id, int base_loop_count)
      : WorkerContext(id), load_set_(0), base_loop_count_(base_loop_count) {}
  void Loop(std::stop_token &stoken);
  int GetLoadSet() const { return load_set_; }
  void SetLoadSet(int load) { load_set_ = load; }

 private:
  int load_set_;
  int base_loop_count_;
};

}  // namespace cpu
