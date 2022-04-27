#include "critical_loop.h"

#include <random>

namespace cpu {

void CriticalLoop(int count) {
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<> udist(0, count - 1);
  int factor = udist(gen);
  thread_local int val = udist(gen);
  for (int i = 0; i < count; ++i) {
    val *= factor;
  }
}

}  // namespace cpu
