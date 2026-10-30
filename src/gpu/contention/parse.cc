#include "parse.h"

#include <array>
#include <cctype>
#include <cstdio>

namespace gpu::contention {

namespace {

// Parse a run of hex digits at the front of `s` (at most `max_digits`),
// advancing `s` past them. Fails on an empty run.
bool TakeHex(std::string_view &s, size_t max_digits, uint32_t &out) {
  size_t n = 0;
  uint64_t value = 0;
  while (n < s.size() && std::isxdigit(static_cast<unsigned char>(s[n]))) {
    if (n == max_digits) {
      return false;
    }
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[n])));
    value = value * 16 + static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    ++n;
  }
  if (n == 0) {
    return false;
  }
  s.remove_prefix(n);
  out = static_cast<uint32_t>(value);
  return true;
}

bool TakeChar(std::string_view &s, char c) {
  if (s.empty() || s.front() != c) {
    return false;
  }
  s.remove_prefix(1);
  return true;
}

bool TakePrefix(std::string_view &s, std::string_view prefix) {
  if (s.substr(0, prefix.size()) != prefix) {
    return false;
  }
  s.remove_prefix(prefix.size());
  return true;
}

bool TakeDecimal(std::string_view &s, int &out) {
  size_t n = 0;
  int64_t value = 0;
  while (n < s.size() && s[n] >= '0' && s[n] <= '9') {
    value = value * 10 + (s[n] - '0');
    if (value > INT32_MAX) {
      return false;
    }
    ++n;
  }
  if (n == 0) {
    return false;
  }
  s.remove_prefix(n);
  out = static_cast<int>(value);
  return true;
}

}  // namespace

std::string FormatPciBusId(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04x:%02x:%02x.%x", domain, bus, device, function);
  return buf;
}

std::string CanonicalPciBusId(std::string_view text) {
  // Up to three ':'-separated hex fields, then ".function". With three the
  // first is the domain (4 digits in sysfs, 8 in NVML).
  std::array<uint32_t, 3> fields{};
  size_t count = 0;
  std::string_view s = text;
  while (true) {
    if (count == fields.size() || !TakeHex(s, 8, fields[count])) {
      return {};
    }
    ++count;
    if (!TakeChar(s, ':')) {
      break;
    }
  }
  uint32_t function = 0;
  if (count < 2 || !TakeChar(s, '.') || !TakeHex(s, 1, function) || !s.empty()) {
    return {};
  }
  const uint32_t domain = count == 3 ? fields[0] : 0;
  const uint32_t bus = fields[count - 2];
  const uint32_t device = fields[count - 1];
  if (domain > 0xffff || bus > 0xff || device > 0x1f || function > 7) {
    return {};
  }
  return FormatPciBusId(domain, bus, device, function);
}

std::optional<uint64_t> KfdProperty(std::string_view properties, std::string_view key) {
  while (!properties.empty()) {
    const size_t eol = properties.find('\n');
    std::string_view line = properties.substr(0, eol);
    properties.remove_prefix(eol == std::string_view::npos ? properties.size() : eol + 1);
    if (!TakePrefix(line, key) || !TakeChar(line, ' ')) {
      continue;
    }
    uint64_t value = 0;
    size_t n = 0;
    while (n < line.size() && line[n] >= '0' && line[n] <= '9') {
      value = value * 10 + static_cast<uint64_t>(line[n] - '0');
      ++n;
    }
    if (n == 0) {
      return std::nullopt;
    }
    return value;
  }
  return std::nullopt;
}

std::string KfdPciBusId(uint32_t domain, uint32_t location_id) {
  return FormatPciBusId(domain, (location_id >> 8) & 0xff, (location_id >> 3) & 0x1f,
                        location_id & 0x7);
}

bool MapsComputeRuntime(std::string_view maps, std::string_view drm_driver) {
  // Driver-side libraries only: the OpenCL ICD loader (libOpenCL.so) and
  // the Level Zero loader get mapped by plenty of programs that never
  // touch a GPU. The vendor drivers below are loaded once a platform is
  // enumerated -- for OpenCL that is every installed ICD at the first
  // clGetPlatformIDs -- so this is only a first filter; DrmSource also
  // wants engine time (DrmBusyFraction).
  static constexpr std::string_view kMesa[] = {
      "/libRusticlOpenCL.so",  // Mesa rusticl (iris, radeonsi, v3d, panfrost, ...)
      "/libMesaOpenCL.so",     // Mesa clover
  };
  static constexpr std::string_view kIntel[] = {
      "/libze_intel_gpu.so",  // Intel Level Zero
      "/libigdrcl.so",        // Intel OpenCL (NEO)
  };
  static constexpr std::string_view kAmd[] = {
      "/libamdhip64.so",       // AMD HIP
      "/libhsa-runtime64.so",  // AMD ROCr (HIP, ROCm OpenCL)
      "/libamdocl64.so",       // AMD ROCm OpenCL
  };
  auto any_of = [maps](const auto &runtimes) {
    for (const auto runtime : runtimes) {
      if (maps.find(runtime) != std::string_view::npos) {
        return true;
      }
    }
    return false;
  };
  if (any_of(kMesa)) {
    return true;
  }
  if (drm_driver == "i915" || drm_driver == "xe") {
    return any_of(kIntel);
  }
  if (drm_driver == "amdgpu") {
    return any_of(kAmd);
  }
  return false;
}

std::optional<DrmFdinfo> ParseDrmFdinfo(std::string_view text) {
  DrmFdinfo out;
  bool drm = false;
  // Raw drm-cycles / drm-total-cycles per class, paired up at the end.
  std::map<std::string, uint64_t, std::less<>> cycles;
  std::map<std::string, uint64_t, std::less<>> total_cycles;
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    std::string_view key = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.remove_prefix(1);
    }
    if (key == "drm-driver") {
      drm = true;
      continue;
    }
    uint64_t number = 0;
    size_t n = 0;
    while (n < value.size() && value[n] >= '0' && value[n] <= '9') {
      number = number * 10 + static_cast<uint64_t>(value[n] - '0');
      ++n;
    }
    if (n == 0) {
      continue;
    }
    const std::string_view unit = value.substr(n);
    if (key == "drm-client-id") {
      out.client_id = number;
    } else if (TakePrefix(key, "drm-engine-")) {
      // drm-engine-capacity-<engine> is an engine count, not a time.
      if (key.rfind("capacity-", 0) != 0 && unit == " ns") {
        out.has_engine_ns = true;
        out.engine_ns += number;
      }
    } else if (TakePrefix(key, "drm-total-cycles-")) {
      total_cycles[std::string(key)] = number;
    } else if (TakePrefix(key, "drm-cycles-")) {
      cycles[std::string(key)] = number;
    }
  }
  if (!drm) {
    return std::nullopt;
  }
  for (const auto &[engine_class, count] : cycles) {
    const auto total = total_cycles.find(engine_class);
    if (total != total_cycles.end()) {
      out.cycles[engine_class] = {count, total->second};
    }
  }
  return out;
}

std::optional<double> DrmBusyFraction(const DrmFdinfo &before, const DrmFdinfo &after,
                                      double wall_ns) {
  if (after.has_engine_ns) {
    if (wall_ns <= 0.0) {
      return std::nullopt;
    }
    const uint64_t busy =
        after.engine_ns >= before.engine_ns ? after.engine_ns - before.engine_ns : 0;
    return static_cast<double>(busy) / wall_ns;
  }
  // xe: busy cycles against the class's own timestamp cycles, no wall time.
  if (after.cycles.empty()) {
    return std::nullopt;
  }
  double fraction = 0.0;
  for (const auto &[engine_class, now] : after.cycles) {
    const auto then = before.cycles.find(engine_class);
    if (then == before.cycles.end() || now.second <= then->second.second ||
        now.first < then->second.first) {
      continue;
    }
    fraction += static_cast<double>(now.first - then->second.first) /
                static_cast<double>(now.second - then->second.second);
  }
  return fraction;
}

std::optional<GpuEngineInstance> ParseGpuEngineInstance(std::string_view name) {
  GpuEngineInstance out;
  uint32_t luid_high = 0;
  uint32_t luid_low = 0;
  std::string_view s = name;
  if (!TakePrefix(s, "pid_") || !TakeDecimal(s, out.pid) || !TakePrefix(s, "_luid_0x") ||
      !TakeHex(s, 8, luid_high) || !TakePrefix(s, "_0x") || !TakeHex(s, 8, luid_low)) {
    return std::nullopt;
  }
  const size_t type_pos = s.find("_engtype_");
  if (type_pos == std::string_view::npos) {
    return std::nullopt;
  }
  out.luid = (static_cast<uint64_t>(luid_high) << 32) | luid_low;
  out.engine_type = s.substr(type_pos + std::string_view("_engtype_").size());
  return out;
}

bool IsComputeEngineType(std::string_view engine_type) {
  auto starts_with_ci = [&engine_type](std::string_view prefix) {
    if (engine_type.size() < prefix.size()) {
      return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(engine_type[i])) != prefix[i]) {
        return false;
      }
    }
    return true;
  };
  return starts_with_ci("compute") || starts_with_ci("cuda");
}

std::optional<UserClientCreator> ParseUserClientCreator(std::string_view text) {
  UserClientCreator out;
  std::string_view s = text;
  if (!TakePrefix(s, "pid ") || !TakeDecimal(s, out.pid) || !TakeChar(s, ',')) {
    return std::nullopt;
  }
  while (!s.empty() && s.front() == ' ') {
    s.remove_prefix(1);
  }
  out.name = s;
  return out;
}

}  // namespace gpu::contention
