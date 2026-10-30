#include "ioreg_source.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <chrono>
#include <string>

#include "parse.h"

#include "gpu/constants.h"
#include "util/log.h"

namespace gpu::contention {

namespace {

std::string CfString(CFTypeRef value) {
  if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
    return {};
  }
  char buf[256] = {};
  if (!CFStringGetCString(static_cast<CFStringRef>(value), buf, sizeof(buf),
                          kCFStringEncodingUTF8)) {
    return {};
  }
  return buf;
}

uint64_t CfU64(CFTypeRef value) {
  int64_t out = 0;
  if (value != nullptr && CFGetTypeID(value) == CFNumberGetTypeID()) {
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &out);
  }
  return out > 0 ? static_cast<uint64_t>(out) : 0;
}

// GPU ns of one user client: the sum over its AppUsage entries (one per
// command queue), or a top-level accumulatedGPUTime on older systems.
uint64_t ClientGpuNs(io_registry_entry_t client) {
  uint64_t total = 0;
  CFTypeRef usage =
      IORegistryEntryCreateCFProperty(client, CFSTR("AppUsage"), kCFAllocatorDefault, 0);
  if (usage != nullptr) {
    if (CFGetTypeID(usage) == CFArrayGetTypeID()) {
      const auto array = static_cast<CFArrayRef>(usage);
      for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
        CFTypeRef entry = CFArrayGetValueAtIndex(array, i);
        if (entry != nullptr && CFGetTypeID(entry) == CFDictionaryGetTypeID()) {
          total += CfU64(CFDictionaryGetValue(static_cast<CFDictionaryRef>(entry),
                                              CFSTR("accumulatedGPUTime")));
        }
      }
    }
    CFRelease(usage);
    return total;
  }
  CFTypeRef direct =
      IORegistryEntryCreateCFProperty(client, CFSTR("accumulatedGPUTime"), kCFAllocatorDefault, 0);
  if (direct != nullptr) {
    total = CfU64(direct);
    CFRelease(direct);
  }
  return total;
}

}  // namespace

IoRegSource::IoRegSource(int self_pid) : self_pid_(self_pid), last_time_{}, primed_(false) {}

void IoRegSource::Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) {
  const TimePoint now = std::chrono::steady_clock::now();
  const double wall_ns =
      primed_ ? std::chrono::duration<double, std::nano>(now - last_time_).count() : 0.0;
  std::map<std::pair<uint64_t, int>, uint64_t> current;
  for (size_t d = 0; d < devices.size(); ++d) {
    const uint64_t registry_id = devices[d].registry_id;
    if (registry_id == 0) {
      continue;
    }
    // MACH_PORT_NULL selects the default main port on every macOS version
    // (kIOMainPortDefault is 12+, kIOMasterPortDefault deprecated there).
    io_service_t accel =
        IOServiceGetMatchingService(MACH_PORT_NULL, IORegistryEntryIDMatching(registry_id));
    if (accel == IO_OBJECT_NULL) {
      continue;
    }
    io_iterator_t it = IO_OBJECT_NULL;
    if (IORegistryEntryGetChildIterator(accel, kIOServicePlane, &it) != KERN_SUCCESS) {
      IOObjectRelease(accel);
      continue;
    }
    out[d].verdict = Observation::Verdict::kIdle;
    double best_share = 0.0;
    for (io_registry_entry_t client = IOIteratorNext(it); client != IO_OBJECT_NULL;
         client = IOIteratorNext(it)) {
      CFTypeRef creator_ref = IORegistryEntryCreateCFProperty(client, CFSTR("IOUserClientCreator"),
                                                              kCFAllocatorDefault, 0);
      const std::string creator_text = CfString(creator_ref);
      if (creator_ref != nullptr) {
        CFRelease(creator_ref);
      }
      const auto creator = ParseUserClientCreator(creator_text);
      if (creator && creator->pid != self_pid_ && creator->name != "WindowServer") {
        // A process can have several clients: sum them.
        current[{registry_id, creator->pid}] += ClientGpuNs(client);
      }
      IOObjectRelease(client);
    }
    IOObjectRelease(it);
    IOObjectRelease(accel);
    if (wall_ns <= 0.0) {
      continue;
    }
    for (const auto &[key, ns] : current) {
      if (key.first != registry_id) {
        continue;
      }
      const auto prev = last_ns_.find(key);
      if (prev == last_ns_.end() || ns < prev->second) {
        continue;  // new process (or restarted pid): no baseline yet
      }
      const double share = 100.0 * static_cast<double>(ns - prev->second) / wall_ns;
      if (share >= kGpuYieldMinBusyPercent && share > best_share) {
        best_share = share;
        out[d].verdict = Observation::Verdict::kBusy;
        out[d].pid = key.second;
      }
    }
  }
  last_ns_ = std::move(current);
  last_time_ = now;
  primed_ = true;
}

}  // namespace gpu::contention
