#include "log.h"

#include <sys/time.h>
#include <ctime>

namespace util {

const char *GetTimeCString() {
  constexpr ssize_t kBufferLen = 128;
  thread_local char buffer[kBufferLen];
  struct timeval time_val;
  gettimeofday(&time_val, nullptr);
  struct tm *TM = localtime(&time_val.tv_sec);
  snprintf(buffer, sizeof(buffer), "%04d%02d%02d %02d:%02d:%02d.%.6d", 1900 + TM->tm_year,
           1 + TM->tm_mon, TM->tm_mday, TM->tm_hour, TM->tm_min, TM->tm_sec,
           static_cast<int>(time_val.tv_usec));  // type of `tv_usec` varies on platforms
  return buffer;
}

}  // namespace util
