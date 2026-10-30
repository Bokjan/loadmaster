#include "pdh_source.h"

#include "core/platform.h"

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "parse.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::contention {

namespace {

PDH_RAW_COUNTER ToPdh(const PdhSource::Raw &r) {
  PDH_RAW_COUNTER out{};
  out.CStatus = static_cast<DWORD>(r.status);
  out.FirstValue = r.first;
  out.SecondValue = r.second;
  out.MultiCount = r.multi_count;
  out.TimeStamp.dwLowDateTime = static_cast<DWORD>(r.time_stamp);
  out.TimeStamp.dwHighDateTime = static_cast<DWORD>(r.time_stamp >> 32);
  return out;
}

}  // namespace

PdhSource::PdhSource(unsigned long self_pid) : self_pid_(self_pid), available_(false) {
  available_ = Collect(last_, nullptr, nullptr);
  if (!available_) {
    LOG_DEBUG("GPU Engine performance counters unavailable");
  }
}

PdhSource::~PdhSource() = default;

bool PdhSource::Collect(std::map<std::wstring, Raw> &raw, const std::map<std::wstring, Raw> *prev,
                        std::map<std::wstring, double> *percent) {
  // A new query every poll: whether a wildcard counter picks up instances
  // (processes) that appear after it was added is undocumented, and
  // reported both ways. Rates come from our own previous raw values, so
  // nothing depends on the query living on.
  PDH_HQUERY query = nullptr;
  if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
    return false;
  }
  bool ok = false;
  PDH_HCOUNTER counter = nullptr;
  // The English name works on every display language.
  if (::PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) ==
          ERROR_SUCCESS &&
      ::PdhCollectQueryData(query) == ERROR_SUCCESS) {
    DWORD bytes = 0;
    DWORD count = 0;
    PDH_STATUS rc = ::PdhGetRawCounterArrayW(counter, &bytes, &count, nullptr);
    std::vector<unsigned char> buf;
    if (rc == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
      buf.resize(bytes);
      rc = ::PdhGetRawCounterArrayW(counter, &bytes, &count,
                                    reinterpret_cast<PDH_RAW_COUNTER_ITEM_W *>(buf.data()));
    } else if (rc == ERROR_SUCCESS || rc == static_cast<PDH_STATUS>(PDH_NO_DATA) ||
               rc == static_cast<PDH_STATUS>(PDH_CSTATUS_NO_INSTANCE)) {
      rc = ERROR_SUCCESS;
      count = 0;  // no instances: nothing has the GPU open
    }
    // Any other status is a real failure: the devices stay "unknown"
    // instead of passing for idle.
    ok = rc == ERROR_SUCCESS;
    if (!ok) {
      LOG_DEBUG("PdhGetRawCounterArrayW failed (0x%lx)", static_cast<unsigned long>(rc));
    }
    const auto *items = reinterpret_cast<const PDH_RAW_COUNTER_ITEM_W *>(buf.data());
    for (DWORD i = 0; ok && i < count; ++i) {
      if (items[i].szName == nullptr) {
        continue;
      }
      const PDH_RAW_COUNTER &r = items[i].RawValue;
      const Raw now{static_cast<long>(r.CStatus), r.FirstValue, r.SecondValue, r.MultiCount,
                    (static_cast<unsigned long long>(r.TimeStamp.dwHighDateTime) << 32) |
                        r.TimeStamp.dwLowDateTime};
      raw[items[i].szName] = now;
      if (prev == nullptr || percent == nullptr) {
        continue;
      }
      const auto before = prev->find(items[i].szName);
      if (before == prev->end()) {
        continue;  // new instance: no baseline yet
      }
      PDH_RAW_COUNTER cur = ToPdh(now);
      PDH_RAW_COUNTER old = ToPdh(before->second);
      PDH_FMT_COUNTERVALUE value{};
      if (::PdhCalculateCounterFromRawValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &cur, &old,
                                            &value) == ERROR_SUCCESS &&
          (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)) {
        (*percent)[items[i].szName] = value.doubleValue;
      }
    }
  }
  ::PdhCloseQuery(query);
  return ok;
}

void PdhSource::Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) {
  if (!available_) {
    return;
  }
  std::map<std::wstring, Raw> raw;
  std::map<std::wstring, double> percent;
  if (!Collect(raw, &last_, &percent)) {
    LOG_DEBUG("GPU Engine counters: collection failed");
    return;
  }
  last_ = std::move(raw);
  // Busy percent of each process's compute engines (ours included), per
  // adapter.
  std::map<std::pair<int, uint64_t>, double> busy;
  for (const auto &[wide_name, value] : percent) {
    // Instance names are plain ASCII.
    std::string name;
    for (wchar_t c : wide_name) {
      name.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    const auto instance = ParseGpuEngineInstance(name);
    if (!instance || !IsComputeEngineType(instance->engine_type)) {
      continue;
    }
    busy[{instance->pid, instance->luid}] += value;
  }
  for (size_t d = 0; d < devices.size(); ++d) {
    out[d].verdict = Observation::Verdict::kIdle;
    double total = 0.0;
    for (const auto &[key, value] : busy) {
      if (devices[d].luid != 0 && devices[d].luid != key.second) {
        continue;
      }
      total += value;
      if (key.first != static_cast<int>(self_pid_) && value >= kGpuYieldMinBusyPercent &&
          out[d].verdict != Observation::Verdict::kBusy) {
        out[d].verdict = Observation::Verdict::kBusy;
        out[d].pid = key.first;
      }
    }
    if (out[d].verdict == Observation::Verdict::kBusy) {
      // Several compute engines busy at once add up: a device is at most
      // fully busy. (Without a LUID this sums every adapter: errs on the
      // side of leaving more to the others.)
      out[d].device_busy = std::min(total, 100.0);
    }
  }
}

}  // namespace gpu::contention
