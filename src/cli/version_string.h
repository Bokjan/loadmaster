#pragma once

// Compile-time-built version string of the loadmaster binary.
//
// The string assembled here ("loadmaster X.Y.Z[-suffix]") is materialized
// entirely at compile time. Two layout variants exist depending on the
// LOADMASTER_OBFUSCATE build option (see util/obfuscate.h):
//
//   * Obfuscation OFF (debug bisecting): the string lives verbatim in
//     a static-storage `std::array<char, N>` in the binary's read-only
//     data segment. There is no runtime allocation, no first-call
//     initialization, and no global ctor.
//
//   * Obfuscation ON (default): the string is XOR-encoded into a
//     `Holder<>` byte array via util::obfuscate::Encode at compile
//     time, and decoded into a function-local `static` buffer the
//     first time VersionString() is invoked. The plaintext never
//     appears in .rodata, so `strings ./loadmaster | grep loadmaster`
//     does not immediately identify the binary.
//
// In either mode the public entry point keeps the original
// `const char *` signature so existing callers (cli/cli.cc, tests)
// don't have to change.

#include <array>
#include <cstdint>
#include <string_view>

#include "core/version.h"
#include "util/obfuscate.h"

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

// Build the full version string into a plaintext NUL-terminated
// std::array. Used as an internal scratch step during the obfuscated
// build below; we deliberately do NOT expose this as an
// `inline constexpr` because that would put the plaintext into
// .rodata and defeat the whole point.
consteval auto BuildVersionStringPlain() {
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
  out[pos] = '\0';
  return out;
}

// Per-payload XOR key for the version string. Picked at random.
inline constexpr std::uint32_t kVersionStringKey = 0xA8B14F62u;

// Build the obfuscated form directly: assemble the plaintext as a
// `consteval` local (lives only on the constant-evaluation stack,
// never in .rodata), XOR-encode it, and return only the encoded
// bytes. The encoded array is what gets emitted into .rodata.
consteval auto BuildVersionStringEncoded() {
  constexpr auto plain = BuildVersionStringPlain();
  return util::obfuscate::Encode(plain, kVersionStringKey);
}

// Encoded form of "loadmaster X.Y.Z[-suffix]". Lives in .rodata as a
// byte array; the plaintext is never assembled into a static at
// compile time when LOADMASTER_OBFUSCATE is on.
inline constexpr auto kVersionStringEncoded = BuildVersionStringEncoded();

}  // namespace version_internal

// Returns a NUL-terminated C string of the form
// "<project> <major>.<minor>.<patch>[-suffix]". The pointer is to
// thread-local storage that is decoded once on first call and reused
// for the lifetime of the thread; it stays valid as long as the
// thread is alive, which matches every existing caller's assumption.
inline const char *VersionString() {
  // thread_local instead of static so any concurrent first-callers
  // each populate their own private copy without an init mutex. The
  // buffer is small (~17 chars) and there are only a handful of
  // threads ever calling this, so the duplication is negligible.
  thread_local char cached[version_internal::kVersionStringEncoded.size() + 1] = {0};
  if (cached[0] == '\0') {
    util::obfuscate::Decode(version_internal::kVersionStringEncoded,
                            version_internal::kVersionStringKey, cached);
  }
  return cached;
}

}  // namespace cli
