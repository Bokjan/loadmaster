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
  enum LogLevel : int {
    kLevelUnknown = 0,
    kLevelTrace,
    kLevelDebug,
    kLevelInfo,
    kLevelWarn,
    kLevelError,
    kLevelFatal,
    kLevelAll,
    kLevelOff
  };

  Logger() : current_level_(kLevelWarn) {}
  virtual ~Logger();
  virtual void Log(const char *format, va_list args) = 0;
  const char *GetTimeCString(LogLevel level);
  void Log(LogLevel level, const char *format, ...);
  bool SetLevel(const char *target);
  bool SetLevel(LogLevel target) {
    if (target <= kLevelUnknown || target > kLevelOff) {
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

// Logs a fatal message and aborts the process immediately. Never returns.
[[noreturn]] void FatalAbort();

}  // namespace logger_internal

}  // namespace util

// LOG_* helpers. We use C++20's __VA_OPT__(,) instead of GCC's
// `, ##__VA_ARGS__` extension so that all three supported toolchains
// (Clang, GCC, MSVC with /Zc:preprocessor) compile these macros without
// any compiler-specific extension warnings:
//   * Clang would warn `-Wgnu-zero-variadic-macro-arguments` on `, ##__VA_ARGS__`.
//   * GCC accepts it but it's non-standard.
//   * MSVC's standards-conformant preprocessor (/Zc:preprocessor) does
//     not implement the `##` swallow-the-comma trick.
// __VA_OPT__ is part of C++20 and is supported by Clang 9+, GCC 8+
// (full in 10+), and MSVC 19.25+ (VS 2019 16.5+) under /Zc:preprocessor.
#define LOG_GENERAL_FORWARD(p, lvl, fmt, ...)                                            \
  do {                                                                                   \
    auto *_lm_logger = (p);                                                              \
    if (_lm_logger->WillPrint(lvl)) {                                                    \
      _lm_logger->Log(lvl, "[%s] %s (%s:%d) " fmt "\n",                                  \
                      _lm_logger->GetTimeCString(lvl),                                   \
                      util::logger_internal::g_log_level_cstr[lvl],                      \
                      FILE_NAME(__FILE__), __LINE__                                      \
                      __VA_OPT__(,) __VA_ARGS__);                                        \
    }                                                                                    \
  } while (false)
#define LOG_DEFAULT_FORWARD(lvl, fmt, ...) \
  LOG_GENERAL_FORWARD(util::logger_internal::g_default_logger, lvl, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelTrace, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelDebug, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelInfo, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelWarn, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelError, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ALL(fmt, ...) LOG_DEFAULT_FORWARD(::util::Logger::kLevelAll, fmt __VA_OPT__(,) __VA_ARGS__)
// LOG_FATAL logs and aborts immediately. Use it ONLY for unrecoverable bugs.
// For "startup failed, exit cleanly" cases, log at ERROR/WARN and return a
// status to the caller, then exit(EXIT_FAILURE) from main.
#define LOG_FATAL(fmt, ...)                                                             \
  do {                                                                                  \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelFatal, fmt __VA_OPT__(,) __VA_ARGS__);    \
    ::util::logger_internal::FatalAbort();                                              \
  } while (false)
