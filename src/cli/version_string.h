#pragma once

// Compile-time-built version string of the loadmaster binary.
//
// The string assembled here ("loadmaster X.Y.Z[-suffix]") is materialized
// entirely at compile time as a static-storage `std::array<char, N>`
// living in the binary's read-only data segment. There is no runtime
// allocation, no first-call initialization, and no global ctor.
//
// The public entry point `VersionString()` keeps the original
// `const char *` signature so existing callers (cli/cli.cc, tests)
// don't have to change.

#include <array>
#include <string_view>

#include "core/version.h"

namespace cli {

namespace version_internal {

// Decimal width of a non-negative int. Always at least 1 (so 0 -> 1).
consteval std::size_t IntDecimalLength(int value) {
  if (value < 0) {
    // We never feed negative version components, but be defensive.
    value = -value;
  }
  std::size_t n = 1;
  for (int v = value / 10; v > 0; v /= 10) {
    ++n;
  }
  return n;
}

// Total payload length (excluding the trailing NUL): the project name,
// a space, MAJOR.MINOR.PATCH, and -- if non-empty -- "-<suffix>".
consteval std::size_t TotalLength() {
  using namespace core::version;
  const std::string_view project{kVersionProject};
  const std::string_view suffix{kVersionSuffix};
  std::size_t n = project.size();
  n += 1;  // ' '
  n += IntDecimalLength(kVersionMajor);
  n += 1;  // '.'
  n += IntDecimalLength(kVersionMinor);
  n += 1;  // '.'
  n += IntDecimalLength(kVersionPatch);
  if (!suffix.empty()) {
    n += 1;  // '-'
    n += suffix.size();
  }
  return n;
}

// Write the decimal representation of `value` into `dst[pos..]` and
// advance `pos`. `consteval`-friendly: only mutates a runtime-of-the-
// constant-evaluation pos and dst, no library calls.
consteval void WriteInt(char *dst, std::size_t &pos, int value) {
  if (value < 0) {
    value = -value;
  }
  // Stash digits in reverse, then flip into dst.
  char tmp[12]{};  // enough for any 32-bit signed int
  std::size_t k = 0;
  if (value == 0) {
    tmp[k++] = '0';
  } else {
    for (int v = value; v > 0; v /= 10) {
      tmp[k++] = static_cast<char>('0' + (v % 10));
    }
  }
  while (k > 0) {
    dst[pos++] = tmp[--k];
  }
}

consteval void WriteSv(char *dst, std::size_t &pos, std::string_view s) {
  for (char c : s) {
    dst[pos++] = c;
  }
}

// Build the full version string, NUL-terminated, into a std::array.
// The +1 reserves room for the terminator so .data() is a valid C
// string usable as `const char *`.
consteval auto BuildVersionString() {
  using namespace core::version;
  constexpr std::size_t kLen = TotalLength();
  std::array<char, kLen + 1> out{};
  std::size_t pos = 0;
  WriteSv(out.data(), pos, kVersionProject);
  out[pos++] = ' ';
  WriteInt(out.data(), pos, kVersionMajor);
  out[pos++] = '.';
  WriteInt(out.data(), pos, kVersionMinor);
  out[pos++] = '.';
  WriteInt(out.data(), pos, kVersionPatch);
  const std::string_view suffix{kVersionSuffix};
  if (!suffix.empty()) {
    out[pos++] = '-';
    WriteSv(out.data(), pos, suffix);
  }
  out[pos] = '\0';  // value-init already gave us \0, but be explicit.
  return out;
}

// Backing storage for the version string. `inline constexpr` means it
// has unique address across TUs and lives in read-only static memory.
inline constexpr auto kVersionString = BuildVersionString();

}  // namespace version_internal

// Returns a NUL-terminated C string of the form
// "<project> <major>.<minor>.<patch>[-suffix]". The pointer is to
// static-storage data, valid for the lifetime of the program.
inline constexpr const char *VersionString() {
  return version_internal::kVersionString.data();
}

}  // namespace cli
