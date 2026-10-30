#include "linux_fs.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "gpu/constants.h"

namespace gpu::contention::linux_fs {

std::optional<std::string> ReadFile(const std::string &path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  std::string out;
  char buf[4096];
  while (true) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n < 0) {
      ::close(fd);
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    out.append(buf, static_cast<size_t>(n));
  }
  ::close(fd);
  return out;
}

std::string ReadLink(const std::string &path) {
  char buf[4096];
  const ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
    return {};
  }
  return std::string(buf, static_cast<size_t>(n));
}

std::string BaseName(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::vector<std::string> ListDir(const std::string &path) {
  std::vector<std::string> out;
  DIR *dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    return out;
  }
  while (const dirent *entry = ::readdir(dir)) {
    const std::string name = entry->d_name;
    if (name != "." && name != "..") {
      out.push_back(name);
    }
  }
  ::closedir(dir);
  return out;
}

std::vector<int> ListNumericDir(const std::string &path) {
  std::vector<int> out;
  for (const auto &name : ListDir(path)) {
    if (name.empty() || name.size() > 9 ||
        name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    out.push_back(std::atoi(name.c_str()));
  }
  return out;
}

std::map<std::string, double> AverageGpuBusyPercent(const std::string &root,
                                                    const std::set<std::string> &pcis) {
  std::map<std::string, std::string> files;
  for (const auto &pci : pcis) {
    const std::string path = root + "/sys/bus/pci/devices/" + pci + "/gpu_busy_percent";
    if (ReadFile(path)) {
      files[pci] = path;
    }
  }
  std::map<std::string, double> sum;
  std::map<std::string, int> count;
  for (int i = 0; i < kGpuBusyPercentSamples && !files.empty(); ++i) {
    if (i > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kGpuBusyPercentSampleIntervalMS));
    }
    for (const auto &[pci, path] : files) {
      const auto text = ReadFile(path);
      if (!text) {
        continue;
      }
      char *end = nullptr;
      const double value = std::strtod(text->c_str(), &end);
      if (end != text->c_str()) {
        sum[pci] += value;
        ++count[pci];
      }
    }
  }
  std::map<std::string, double> out;
  for (const auto &[pci, total] : sum) {
    out[pci] = total / count[pci];
  }
  return out;
}

}  // namespace gpu::contention::linux_fs
