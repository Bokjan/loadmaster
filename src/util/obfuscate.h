#pragma once

// Compile-time XOR obfuscation for string / byte literals embedded in
// the loadmaster binary. Two clients today:
//
//   * GPU kernel sources / SPIR-V (gpu/nvidia/ptx_kernel.h, etc.): hide
//     the busy-kernel IL so `strings` / `rabin2 -z` don't lead an
//     analyst straight to the GPU stress logic, and so anti-malware
//     heuristics that flag "binary contains GPU compiler IL" stop
//     firing on us.
//   * CLI-facing strings (cli/version_string.h, cli/cli.cc): hide the
//     project name + help text so a quick `strings` doesn't
//     immediately identify the binary as loadmaster. The runtime
//     output of `-v` / `-h` is unchanged; only the static .rodata
//     copy is masked.
//
// Anyone determined to reverse the binary can still recover the source:
// the XOR loop is right there in `objdump -d`, the key is in the
// .rodata segment, and the runtime sees the plaintext anyway. This
// header raises the bar from "trivially visible to grep" to "five
// minutes of static analysis", which is exactly what we want.
//
// Toggle: define LOADMASTER_OBFUSCATE to 0 at build time (CMake
// `-DLOADMASTER_OBFUSCATE_KERNELS=OFF`, kept under the historical
// option name for backwards compatibility) to make Encode / Decode /
// DecodeBytes behave as the identity. Useful when bisecting a kernel
// build / JIT failure: the embedded strings are then identical to
// what you see in the .h file, so `cuobjdump` / `spirv-dis` / Metal
// compiler error messages line up with the source.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#ifndef LOADMASTER_OBFUSCATE
#  define LOADMASTER_OBFUSCATE 1
#endif

namespace util::obfuscate {

// 4-byte rolling key XORed across the input. The key is per-payload
// (passed in by the caller) so a single recovered key doesn't decode
// every embedded blob. We also XOR the byte index in to break the
// "two consecutive identical bytes -> two identical ciphertext bytes"
// pattern that would let a casual reader spot run-length structure
// (e.g. PTX has long stretches of '%' or ' ').
//
// `constexpr` (not consteval): callers from both compile-time
// (Encode<>) and runtime (Decode<> / DecodeBytes) contexts share this
// helper. With consteval the runtime decoders would fail to compile
// under MSVC's stricter immediate-call enforcement.
constexpr std::uint8_t MixByte(std::uint8_t plain, std::uint32_t key, std::size_t index) {
  const std::uint8_t k = static_cast<std::uint8_t>((key >> (8 * (index & 3u))) & 0xffu);
  const std::uint8_t i = static_cast<std::uint8_t>(index & 0xffu);
#if LOADMASTER_OBFUSCATE
  return static_cast<std::uint8_t>(plain ^ k ^ i);
#else
  (void)k;
  (void)i;
  return plain;
#endif
}

// Encode a string literal at compile time into a byte array. The
// returned array does NOT include a trailing NUL -- payloads are
// often passed to APIs as (pointer, size) pairs, and we want to avoid
// the implicit '\0' showing up as a known-plaintext anchor.
template <std::size_t N>
consteval auto Encode(const char (&literal)[N], std::uint32_t key) {
  // N includes the NUL terminator; we strip it.
  constexpr std::size_t kSize = N - 1;
  std::array<std::uint8_t, kSize> out{};
  for (std::size_t i = 0; i < kSize; ++i) {
    out[i] = MixByte(static_cast<std::uint8_t>(literal[i]), key, i);
  }
  return out;
}

// std::array<char, N> overload. Used for strings produced by other
// consteval helpers (e.g. cli::version_string) that don't have a C
// array form. The caller must pass a NUL-terminated array; we drop
// that NUL exactly like the C-array overload does.
template <std::size_t N>
consteval auto Encode(const std::array<char, N> &literal, std::uint32_t key) {
  static_assert(N >= 1, "Encode() needs at least the NUL terminator");
  constexpr std::size_t kSize = N - 1;
  std::array<std::uint8_t, kSize> out{};
  for (std::size_t i = 0; i < kSize; ++i) {
    out[i] = MixByte(static_cast<std::uint8_t>(literal[i]), key, i);
  }
  return out;
}

// Reverse the encoding into a caller-supplied buffer. `dst` must hold
// at least `src.size() + 1` bytes; we NUL-terminate so the result is
// usable as a plain `const char *`. Runtime is O(N) byte XORs.
template <std::size_t N>
inline void Decode(const std::array<std::uint8_t, N> &src, std::uint32_t key, char *dst) {
  for (std::size_t i = 0; i < N; ++i) {
    dst[i] = static_cast<char>(MixByte(src[i], key, i));
  }
  dst[N] = '\0';
}

// Same as Decode<>, but for a runtime-sized byte array (used by the
// SPIR-V path: the .spv length isn't known to the .h, only to CMake).
inline void DecodeBytes(const std::uint8_t *src, std::size_t size, std::uint32_t key,
                        std::uint8_t *dst) {
#if LOADMASTER_OBFUSCATE
  for (std::size_t i = 0; i < size; ++i) {
    const std::uint8_t k = static_cast<std::uint8_t>((key >> (8 * (i & 3u))) & 0xffu);
    const std::uint8_t idx = static_cast<std::uint8_t>(i & 0xffu);
    dst[i] = static_cast<std::uint8_t>(src[i] ^ k ^ idx);
  }
#else
  (void)key;
  std::memcpy(dst, src, size);
#endif
}

// RAII container for a freshly-decoded payload. Holds an std::array
// sized at compile time so there is no heap allocation; clears the
// buffer on destruction with a best-effort `volatile` write so the
// plaintext does not linger in stack frames longer than necessary.
//
// Typical use:
//
//     constexpr auto enc = util::obfuscate::Encode("loadmaster", kKey);
//     util::obfuscate::Scoped<enc.size()> plain(enc, kKey);
//     std::puts(plain.c_str());
//     // plain goes out of scope here -> buffer is wiped.
template <std::size_t N>
class Scoped {
 public:
  Scoped(const std::array<std::uint8_t, N> &src, std::uint32_t key) {
    Decode(src, key, buf_.data());
  }
  ~Scoped() {
    // volatile pointer prevents the compiler from removing the wipe
    // as a dead store. Not as strong as memset_s / SecureZeroMemory
    // (which we'd have to platform-shim), but good enough for the
    // threat model of "make `strings` on a core dump less obvious".
    volatile char *p = buf_.data();
    for (std::size_t i = 0; i <= N; ++i) {
      p[i] = 0;
    }
  }
  Scoped(const Scoped &) = delete;
  Scoped &operator=(const Scoped &) = delete;

  const char *c_str() const { return buf_.data(); }
  std::size_t size() const { return N; }

 private:
  // +1 for the NUL terminator that Decode writes.
  std::array<char, N + 1> buf_{};
};

// Convenience bundle: a Holder<> ties an obfuscated payload to its
// per-payload key, so call sites only have to spell one identifier.
//   constexpr auto kFoo = util::obfuscate::Make("hello", 0xC0DEC0DEu);
//   util::obfuscate::Scoped scoped(kFoo);
//   std::puts(scoped.c_str());
template <std::size_t N>
struct Holder {
  std::array<std::uint8_t, N> data;
  std::uint32_t key;
  static constexpr std::size_t kSize = N;
};

template <std::size_t N>
consteval auto Make(const char (&literal)[N], std::uint32_t key) {
  Holder<N - 1> h{Encode(literal, key), key};
  return h;
}

// Scoped overload that unpacks a Holder for the caller. Same lifetime
// semantics as the array-based ctor; the buffer is wiped on destruction.
template <std::size_t N>
class ScopedHolder {
 public:
  explicit ScopedHolder(const Holder<N> &h) : inner_(h.data, h.key) {}
  const char *c_str() const { return inner_.c_str(); }
  std::size_t size() const { return inner_.size(); }

 private:
  Scoped<N> inner_;
};

// Class template argument deduction guide so callers can write
// `util::obfuscate::Scoped scoped(kFoo)` without spelling the size.
template <std::size_t N>
ScopedHolder(const Holder<N> &) -> ScopedHolder<N>;

}  // namespace util::obfuscate
