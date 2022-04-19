#include "log.h"

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>

#include <map>
#include <string>

#include <sys/time.h>

namespace util {

namespace logger_internal {

StderrLogger g_default_stderr_logger;
Logger *g_logger = &g_default_stderr_logger;
const char *g_log_level_cstr[] = {"<UNKNOWN>", "<TRACE>", "<DEBUG>", "<INFO> ", "<WARN> ",
                                  "<ERROR>",   "<FATAL>", "<ALL>  ", "<OFF>  "};
void SetLogger(Logger *ptr) { g_logger = ptr; }

void FatalTrigger() {
  raise(SIGTERM);
}

}  // namespace logger_internal

Logger::~Logger() {}

void Logger::Log(LogLevel level, const char *Format, ...) {
  if (!WillPrint(level)) {
    return;
  }
  va_list Arguments;
  va_start(Arguments, Format);
  this->Log(Format, Arguments);
  va_end(Arguments);
}

const char *Logger::GetTimeCString(LogLevel level) {
  constexpr size_t kBufferLen = 128;
  thread_local char buffer[kBufferLen];
  if (!this->WillPrint(level)) {
    return buffer;
  }
  struct timeval time_val;
  gettimeofday(&time_val, nullptr);
  struct tm *time_struct = localtime(&time_val.tv_sec);
  snprintf(buffer, sizeof(buffer), "%04d%02d%02d %02d:%02d:%02d.%.6d", 1900 + time_struct->tm_year,
           1 + time_struct->tm_mon, time_struct->tm_mday, time_struct->tm_hour, time_struct->tm_min,
           time_struct->tm_sec,
           static_cast<int>(time_val.tv_usec));  // type of `tv_usec` varies on platforms
  return buffer;
}

bool Logger::SetLevel(const char *target) {
  static std::map<std::string, LogLevel> level_map = {
      {"trace", kTrace}, {"debug", kDebug}, {"info", kInfo}, {"warn", kWarn},
      {"error", kError}, {"fatal", kFatal}, {"off", kOff}};
  auto find = level_map.find(target);
  if (find == level_map.end()) {
    return false;
  }
  this->SetLevel(find->second);
  return true;
}

void StderrLogger::Log(const char *Format, va_list Arguments) {
  vfprintf(stderr, Format, Arguments);
}

}  // namespace util
