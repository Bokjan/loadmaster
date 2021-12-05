#pragma once

#include <cstdio>

namespace util {
const char *GetTimeCString();
}

#ifndef SOURCE_PATH_SIZE  // this should be defined by CMake scripts
#define SOURCE_PATH_SIZE 0
#endif

#ifdef SRC_PREFIX_SIZE
#undef SRC_PREFIX_SIZE
#endif
#define SRC_PREFIX_SIZE (sizeof("src/") - 1)

#define __FILENAME__ (__FILE__ + SOURCE_PATH_SIZE + SRC_PREFIX_SIZE)

#define LOG_FILE(fp, fmt, args...) \
  fprintf(fp, "[%s] (%s:%d) " fmt "\n", util::GetTimeCString(), __FILENAME__, __LINE__, ##args)
#define LOG(fmt, args...) LOG_FILE(stdout, fmt, ##args)
#define LOG_E(fmt, args...) LOG_FILE(stderr, "[ERR] " fmt, ##args)
