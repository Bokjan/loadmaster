#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <algorithm>
#include <string_view>

#include "core/platform.h"  // brings in <windows.h> on MSVC

#if IS_WINDOWS
#  include <cstdint>
// `struct timeval` lives in <winsock2.h>, but we don't want to bring in the
// rest of winsock; declare it locally. tv_sec is 64-bit so it survives 2038
// (Windows `long` is 32-bit even on x64, and the old 32-bit tv_sec wrapped
// in 2038).
struct timeval {
  long long tv_sec;
  long long tv_usec;
};
#else
#  include <sys/time.h>
#endif

#if IS_WINDOWS
static int gettimeofday(timeval *tp, struct timezone * /*tzp*/) {
  // FILETIME is 100ns ticks since 1601-01-01. Derive both tv_sec and
  // tv_usec from a single GetSystemTimeAsFileTime sample: the old code
  // took two samples (GetSystemTime for microseconds, SystemTimeToFileTime
  // for seconds), so the two fields could straddle a second boundary and
  // disagree by up to ~1 ms.
  constexpr uint64_t kEpoch100ns = 116444736000000000ULL;  // 1601->1970, 100ns ticks
  FILETIME ft;
  ::GetSystemTimeAsFileTime(&ft);
  uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  const uint64_t since_epoch = t - kEpoch100ns;  // 100ns ticks since 1970
  tp->tv_sec = static_cast<long long>(since_epoch / 10000000ULL);
  tp->tv_usec = static_cast<long long>((since_epoch / 10ULL) % 1000000ULL);
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
  struct tm time_struct{};
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
  static const SvLevelPair level_pairs[] = {
      {"trace", kLevelTrace}, {"debug", kLevelDebug}, {"info", kLevelInfo}, {"warn", kLevelWarn},
      {"error", kLevelError}, {"fatal", kLevelFatal}, {"all", kLevelAll},   {"off", kLevelOff}};
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
