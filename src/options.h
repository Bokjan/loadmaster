#pragma once

#include "constants.h"

class Options {
 public:
  int load_; // todo: rename to cpu_load_
  int count_; // todo: rename to cpu_concurrency_
  Options() : load_(kDefaultCpuLoad), count_(0) {}
};
