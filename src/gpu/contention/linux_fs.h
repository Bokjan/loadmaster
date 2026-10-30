#pragma once

// Small POSIX file helpers for the Linux contention sources (procfs /
// sysfs). Every path is used as given; callers prepend their test root.

#include <optional>
#include <string>
#include <vector>

namespace gpu::contention::linux_fs {

// Whole file, or nullopt if it can't be opened / read.
std::optional<std::string> ReadFile(const std::string &path);

// Target of a symlink, or empty.
std::string ReadLink(const std::string &path);

// Last component of a path ("../../../0000:03:00.0" -> "0000:03:00.0").
std::string BaseName(const std::string &path);

// Entry names of a directory (without "." / ".."); empty if unreadable.
std::vector<std::string> ListDir(const std::string &path);

// Entries of `path` whose names are all digits, as numbers (pids, ids).
std::vector<int> ListNumericDir(const std::string &path);

}  // namespace gpu::contention::linux_fs
