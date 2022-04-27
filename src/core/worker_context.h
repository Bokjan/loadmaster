#pragma once

#include <thread>

namespace core {

class WorkerContext {
 public:
  int id_;
  std::jthread jthread_;

  explicit WorkerContext(int id) : id_(id) {}
  virtual void Loop(std::stop_token &stoken) = 0;
};

}
