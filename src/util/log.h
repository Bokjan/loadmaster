#pragma once

#include <cstdarg>

#ifndef SOURCE_PATH_SIZE  // this should be defined by CMake scripts
#  define SOURCE_PATH_SIZE 0
#endif

#ifdef SRC_PREFIX_SIZE
#  undef SRC_PREFIX_SIZE
#endif
#ifndef SRC_PREFIX_SIZE
#  define STR_LITERAL_LEN(x) (sizeof(x) - 1)
#  define SRC_PREFIX_SIZE STR_LITERAL_LEN("src/")
#endif

#define FILE_NAME(full_path) ((full_path) + SOURCE_PATH_SIZE)

namespace util {

class Logger {
 public:
  enum LogLevel : int { kUnknown = 0, kTrace, kDebug, kInfo, kWarn, kError, kFatal, kAll, kOff };

  Logger() : current_level_(kWarn) {}
  virtual ~Logger();
  virtual void Log(const char *format, va_list args) = 0;
  const char *GetTimeCString(LogLevel level);
  void Log(LogLevel level, const char *format, ...);
  bool SetLevel(const char *target);
  bool SetLevel(LogLevel target) {
    if (target <= kUnknown || target > kOff) {
      return false;
    }
    current_level_ = target;
    return true;
  }
  bool WillPrint(LogLevel level) const { return level >= current_level_; }

 private:
  LogLevel current_level_;
};

class StderrLogger final : public Logger {
 public:
  void Log(const char *format, va_list args);
};

namespace logger_internal {

extern Logger *g_default_logger;
extern const char *g_log_level_cstr[];

void SetDefaultLogger(Logger *ptr);
void FatalTrigger();

}  // namespace logger_internal

}  // namespace util

#define LOG_GENERAL_FORWARD(p, lvl, fmt, ...)                      \
  (p)->Log(lvl, "[%s] %s (%s:%d) " fmt "\n", (p)->GetTimeCString(lvl), \
           util::logger_internal::g_log_level_cstr[lvl], FILE_NAME(__FILE__), __LINE__, ##__VA_ARGS__)
#define LOG_DEFAULT_FORWARD(lvl, fmt, ...) \
  LOG_GENERAL_FORWARD(util::logger_internal::g_default_logger, lvl, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kTrace, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kDebug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kInfo, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kWarn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kError, fmt, ##__VA_ARGS__)
#define LOG_ALL(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kAll, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)                               \
  do {                                                        \
    LOG_DEFAULT_FORWARD(::util::Logger::kFatal, fmt, ##__VA_ARGS__); \
    ::util::logger_internal::FatalTrigger();                  \
  } while (false)
