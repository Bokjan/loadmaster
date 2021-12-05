#pragma once

enum class ErrCode : int {
  kOK = 0,
  kProcStatUnknown = 10000,
  kProcStatOpen = 10001,
  kProcStatReadValues = 10002,
  kProcStatFindCpuTotal = 10003,
};

inline int ErrCodeToInt(ErrCode code) { return static_cast<int>(code); }
