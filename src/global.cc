#include "global.h"

namespace global {

volatile bool keep_loop = true;

void StopLoop() {
  keep_loop = false;
}

}
