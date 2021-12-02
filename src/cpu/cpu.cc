#include "cpu.h"

#include <thread>

namespace cpu {

int Count() {
  static int count = static_cast<int>(std::thread::hardware_concurrency());
  return count;
}

}  // namespace cpu
