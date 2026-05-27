#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <algorithm>
#include <string_view>

#include "core/platform.h"  // brings in <windows.h> on MSVC

#if IS_WINDOWS
#    include <cstdint>
// `struct timeval` lives in <winsock2.h>, but we don't want to bring in the
// rest of winsock; declare it locally.
struct timeval {
  long tv_sec;
  long tv_usec;
};
#else
#    include <sys/time.h>
#endif

#if IS_WINDOWS
static int gettimeofday(timeval *tp, struct timezone * /*tzp*/) {
  constexpr uint64_t kEpoch = ((uint64_t)116444736000000000ULL);

  SYSTEMTIME system_time;
  FILETIME file_time;
  uint64_t time;

  GetSystemTime(&system_time);
  SystemTimeToFileTime(&system_time, &file_time);
  time = ((uint64_t)file_time.dwLowDateTime);
  time += ((uint64_t)file_time.dwHighDateTime) << 32;

  tp->tv_sec = (long)((time - kEpoch) / 10000000L);
  tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
  return 0;
}
#endif

namespace util {

namespace logger_internal {

StderrLogger g_default_stderr_logger;
Logger *g_default_logger = &g_default_stderr_logger;
const char *g_log_level_cstr[] = {"<UNKNOWN>", "<TRACE>", "<DEBUG>", "<INFO> ", "<WARN> ",
                                  "<ERROR>",   "<FATAL>", "<ALL>  ", "<OFF>  "};

void SetDefaultLogger(Logger *ptr) { g_default_logger = ptr; }

[[noreturn]] void FatalAbort() {
  std::fflush(stderr);
  std::abort();
}

}  // namespace logger_internal

Logger::~Logger() {}

void Logger::Log(LogLevel level, const char *format, ...) {
  if (!WillPrint(level)) {
    return;
  }
  va_list args;
  va_start(args, format);
  this->Log(format, args);
  va_end(args);
}

const char *Logger::GetTimeCString(LogLevel level) {
  constexpr size_t kBufferLen = 128;
  thread_local char buffer[kBufferLen];
  if (!this->WillPrint(level)) {
    return buffer;
  }
  struct timeval time_val;
  gettimeofday(&time_val, nullptr);
  struct tm time_struct {};
#if IS_WINDOWS
  time_t tsec = time_val.tv_sec;
  (void)localtime_s(&time_struct, &tsec);
#else
  // POSIX: use localtime_r for thread safety; the global `tzset()` call inside
  // is fine to be invoked concurrently because it only touches process-wide
  // mutable state guarded by libc.
  time_t tsec = time_val.tv_sec;
  localtime_r(&tsec, &time_struct);
#endif
  snprintf(buffer, sizeof(buffer), "%04d%02d%02d %02d:%02d:%02d.%.6d", 1900 + time_struct.tm_year,
           1 + time_struct.tm_mon, time_struct.tm_mday, time_struct.tm_hour, time_struct.tm_min,
           time_struct.tm_sec, static_cast<int>(time_val.tv_usec));
  return buffer;
}

bool Logger::SetLevel(const char *target) {
  using SvLevelPair = std::pair<std::string_view, LogLevel>;
  static const SvLevelPair level_pairs[] = {{"trace", kLevelTrace}, {"debug", kLevelDebug}, {"info", kLevelInfo},
                                            {"warn", kLevelWarn},   {"error", kLevelError}, {"fatal", kLevelFatal},
                                            {"all", kLevelAll},     {"off", kLevelOff}};
  const std::string_view sv(target);
  auto find = std::find_if(std::begin(level_pairs), std::end(level_pairs),
                           [&sv](const SvLevelPair &pair) { return pair.first == sv; });
  if (find == std::end(level_pairs)) {
    return false;
  }
  const auto [_, level] = *find;
  this->SetLevel(level);
  return true;
}

void StderrLogger::Log(const char *format, va_list args) { vfprintf(stderr, format, args); }

}  // namespace util
