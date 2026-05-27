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

// Compile-time log threshold. Levels at or above LOADMASTER_LOG_MIN_LEVEL
// expand to real LOG_GENERAL_FORWARD calls; lower-priority macros expand
// to `((void)0)` so the format strings, the __FILE__ paths, and the
// helper code never make it into the binary at all.
//
// Level numbering matches util::Logger::LogLevel ordering (trace < debug
// < info < warn < error < fatal). The default is 1 (trace), i.e. all
// LOG_* calls remain compiled in -- runtime `-L <level>` then picks
// among them as before. Bump it via CMake's LOADMASTER_LOG_MIN_LEVEL
// cache variable to strip the lower-priority sites entirely (smaller
// binary, fewer rodata strings revealing module names / file paths).
#ifndef LOADMASTER_LOG_MIN_LEVEL
#  define LOADMASTER_LOG_MIN_LEVEL 1
#endif
#define LOADMASTER_LOG_LEVEL_TRACE 1
#define LOADMASTER_LOG_LEVEL_DEBUG 2
#define LOADMASTER_LOG_LEVEL_INFO 3
#define LOADMASTER_LOG_LEVEL_WARN 4
#define LOADMASTER_LOG_LEVEL_ERROR 5
#define LOADMASTER_LOG_LEVEL_FATAL 6

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
      _lm_logger->Log(lvl, "[%s] %s (%s:%d) " fmt "\n", _lm_logger->GetTimeCString(lvl), \
                      util::logger_internal::g_log_level_cstr[lvl], FILE_NAME(__FILE__), \
                      __LINE__ __VA_OPT__(, ) __VA_ARGS__);                              \
    }                                                                                    \
  } while (false)
#define LOG_DEFAULT_FORWARD(lvl, fmt, ...) \
  LOG_GENERAL_FORWARD(util::logger_internal::g_default_logger, lvl, fmt __VA_OPT__(, ) __VA_ARGS__)

// Per-level macros. Each one is gated at compile time by
// LOADMASTER_LOG_MIN_LEVEL: sites below the threshold expand to
// `((void)0)`, so the format string, __FILE__, and the per-call code
// never make it into the binary. Sites at or above the threshold
// retain the existing runtime gate via Logger::WillPrint, so users
// can still narrow further via `-L <level>`.
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_TRACE
#  define LOG_TRACE(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelTrace, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_TRACE(fmt, ...) ((void)0)
#endif
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_DEBUG
#  define LOG_DEBUG(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelDebug, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_DEBUG(fmt, ...) ((void)0)
#endif
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_INFO
#  define LOG_INFO(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelInfo, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_INFO(fmt, ...) ((void)0)
#endif
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_WARN
#  define LOG_WARN(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelWarn, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_WARN(fmt, ...) ((void)0)
#endif
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_ERROR
#  define LOG_ERROR(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelError, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_ERROR(fmt, ...) ((void)0)
#endif
// LOG_ALL is a debugging-time facility; it shares LOG_TRACE's threshold
// because it's never used to communicate user-visible information.
#if LOADMASTER_LOG_MIN_LEVEL <= LOADMASTER_LOG_LEVEL_TRACE
#  define LOG_ALL(fmt, ...) \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelAll, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#  define LOG_ALL(fmt, ...) ((void)0)
#endif
// LOG_FATAL logs and aborts immediately. Use it ONLY for unrecoverable bugs.
// For "startup failed, exit cleanly" cases, log at ERROR/WARN and return a
// status to the caller, then exit(EXIT_FAILURE) from main. LOG_FATAL is
// never compiled out -- it doubles as a [[noreturn]] crash primitive that
// callers may rely on for control flow.
#define LOG_FATAL(fmt, ...)                                                           \
  do {                                                                                \
    LOG_DEFAULT_FORWARD(::util::Logger::kLevelFatal, fmt __VA_OPT__(, ) __VA_ARGS__); \
    ::util::logger_internal::FatalAbort();                                            \
  } while (false)
