// Unit tests for util::obfuscate.
//
// The obfuscation layer is intentionally weak (XOR + index mix); its job
// is to keep static-`strings` analysis from leading directly to the
// loadmaster project name and to the embedded GPU kernel sources, not
// to resist a determined reverse engineer. What we pin here are the
// behavioural contracts the rest of the codebase relies on:
//
//   * Encode is a pure function of (literal, key) at compile time.
//   * Decode (and DecodeBytes) is the exact inverse of Encode for any
//     payload up to a few KiB -- this is the round-trip used by every
//     caller (version string, GPU kernel sources, SPIR-V blob).
//   * The trailing NUL of a C string literal is dropped before
//     encoding, so the encoded array length is `N-1` for a literal of
//     length N. Several call sites rely on this to size buffers.
//   * Key changes produce different ciphertext for non-empty payloads,
//     so a single recovered key does not decode every blob.
//   * Index-mixing actually fires: two runs of identical bytes produce
//     non-identical ciphertext bytes. This is what defeats casual
//     pattern spotting on long stretches of repeated characters in
//     PTX / SPIR-V sources.
//   * Scoped / ScopedHolder hand back the plaintext via `c_str()`.

#include "util/obfuscate.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using util::obfuscate::Decode;
using util::obfuscate::DecodeBytes;
using util::obfuscate::Encode;
using util::obfuscate::Holder;
using util::obfuscate::Make;
using util::obfuscate::MixByte;
using util::obfuscate::Scoped;
using util::obfuscate::ScopedHolder;

// Helper: decode an Encode<>'d array back into a std::string for easy
// gtest comparison. Strips the trailing NUL that Decode writes.
template <std::size_t N>
std::string DecodeToString(const std::array<std::uint8_t, N> &encoded, std::uint32_t key) {
  char buf[N + 1] = {0};
  Decode(encoded, key, buf);
  return std::string(buf, N);  // exact length: Decode does not embed NULs mid-payload
}

// ---- MixByte --------------------------------------------------------------

TEST(MixByteTest, IsSelfInverse) {
  // The whole obfuscation scheme relies on MixByte being its own
  // inverse: applying it twice with the same (key, index) returns
  // the original byte. If this ever stops holding, Decode silently
  // produces garbage.
  for (std::uint32_t key : {0u, 1u, 0xDEADBEEFu, 0xA8B14F62u}) {
    for (std::size_t i = 0; i < 32; ++i) {
      for (int b = 0; b < 256; ++b) {
        const auto plain = static_cast<std::uint8_t>(b);
        const auto cipher = MixByte(plain, key, i);
        EXPECT_EQ(MixByte(cipher, key, i), plain)
            << "byte=" << b << " key=" << key << " index=" << i;
      }
    }
  }
}

TEST(MixByteTest, DependsOnIndex) {
  // Index-mixing must actually contribute. Two adjacent indices with
  // the same input and same key should produce different output for
  // most bytes, otherwise long runs of identical plaintext bytes (PTX
  // has lots of spaces) leak as identical ciphertext runs.
  constexpr std::uint32_t key = 0xCAFEBABEu;
  int differ = 0;
  for (int b = 0; b < 256; ++b) {
    const auto plain = static_cast<std::uint8_t>(b);
    if (MixByte(plain, key, 0) != MixByte(plain, key, 1)) {
      ++differ;
    }
  }
  // Index 0 and 1 differ in both the index-XOR byte (0 vs 1) and the
  // key-byte position (byte 0 vs byte 1 of the key word). Empirically
  // every byte differs; require at least most of them.
  EXPECT_GE(differ, 200);
}

TEST(MixByteTest, DependsOnKey) {
  // Different keys must produce different ciphertext for at least
  // some bytes at the same index. Otherwise per-payload keys are
  // pointless.
  int differ = 0;
  for (int b = 0; b < 256; ++b) {
    const auto plain = static_cast<std::uint8_t>(b);
    if (MixByte(plain, 0xAAAAAAAAu, 0) != MixByte(plain, 0x55555555u, 0)) {
      ++differ;
    }
  }
  EXPECT_EQ(differ, 256);  // 0xAA ^ 0x55 = 0xFF -> every byte flips
}

// ---- Encode / Decode round-trip ------------------------------------------

TEST(EncodeDecodeTest, RoundTripsAsciiLiteral) {
  constexpr std::uint32_t kKey = 0x12345678u;
  constexpr auto encoded = Encode("hello", kKey);
  static_assert(encoded.size() == 5, "trailing NUL must be stripped");
  EXPECT_EQ(DecodeToString(encoded, kKey), "hello");
}

TEST(EncodeDecodeTest, EncodedDiffersFromPlaintext) {
  // The whole point of the layer: encoded bytes must NOT match the
  // plaintext byte-for-byte. If they do, the build flipped
  // LOADMASTER_OBFUSCATE off without the test noticing, or someone
  // refactored MixByte into the identity.
  constexpr std::uint32_t kKey = 0xA8B14F62u;
  constexpr auto encoded = Encode("loadmaster", kKey);
  const char plain[] = "loadmaster";
  int same = 0;
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] == static_cast<std::uint8_t>(plain[i])) {
      ++same;
    }
  }
  // Allow a handful of accidental fixed points but reject "all 10
  // bytes identical" -- that's the identity transform.
  EXPECT_LT(same, static_cast<int>(encoded.size()));
}

TEST(EncodeDecodeTest, RoundTripsAcrossManyKeys) {
  // Sample a spread of keys; for each, encode + decode and check
  // bit-exact recovery.
  constexpr const char kPayload[] = "The quick brown fox jumps over the lazy dog.";
  for (std::uint32_t key : {0u, 1u, 0xFFFFFFFFu, 0xDEADBEEFu, 0x00010203u, 0xA8B14F62u}) {
    const auto encoded = Encode(kPayload, key);
    char decoded[sizeof(kPayload)] = {0};
    Decode(encoded, key, decoded);
    EXPECT_STREQ(decoded, kPayload) << "key=" << key;
  }
}

TEST(EncodeDecodeTest, WrongKeyDoesNotRecoverPlaintext) {
  constexpr std::uint32_t kRight = 0x12345678u;
  constexpr std::uint32_t kWrong = 0x12345679u;  // differ in low byte
  constexpr auto encoded = Encode("loadmaster", kRight);
  char decoded[encoded.size() + 1] = {0};
  Decode(encoded, kWrong, decoded);
  EXPECT_STRNE(decoded, "loadmaster");
}

TEST(EncodeDecodeTest, IndexMixingBreaksIdenticalByteRuns) {
  // A long run of identical bytes should NOT encode to a long run of
  // identical ciphertext bytes. This is the property that makes a
  // visual scan of `strings` output less revealing.
  constexpr std::uint32_t kKey = 0xA8B14F62u;
  constexpr auto encoded = Encode("AAAAAAAAAAAAAAAA", kKey);  // 16 'A's
  int distinct = 0;
  std::array<bool, 256> seen{};
  for (std::uint8_t b : encoded) {
    if (!seen[b]) {
      seen[b] = true;
      ++distinct;
    }
  }
  // 16 different positions feed 16 different index-XOR values, and
  // the key word cycles every 4 bytes, so we expect at least several
  // distinct ciphertext bytes. Bound loosely to avoid coupling to the
  // exact key.
  EXPECT_GE(distinct, 4);
}

// ---- Empty / single-byte payloads ----------------------------------------

TEST(EncodeDecodeTest, EmptyLiteralEncodesToEmptyArray) {
  constexpr auto encoded = Encode("", 0xDEADBEEFu);
  static_assert(encoded.size() == 0, "empty literal yields empty array");
  // Decode<0> should still NUL-terminate dst[0].
  char buf[1] = {'X'};
  Decode(encoded, 0xDEADBEEFu, buf);
  EXPECT_EQ(buf[0], '\0');
}

TEST(EncodeDecodeTest, SingleByteRoundTrips) {
  constexpr std::uint32_t kKey = 0xA8B14F62u;
  constexpr auto encoded = Encode("X", kKey);
  static_assert(encoded.size() == 1, "single char literal -> single byte");
  EXPECT_EQ(DecodeToString(encoded, kKey), "X");
}

// ---- std::array<char, N> Encode overload ---------------------------------

TEST(EncodeArrayTest, RoundTripsStdArrayLiteral) {
  // The std::array<char> overload exists so that consteval helpers
  // (notably cli::version_string::BuildVersionStringPlain) can hand
  // their output back into Encode. Verify it strips the NUL exactly
  // like the C-array overload.
  constexpr std::array<char, 6> src{'h', 'e', 'l', 'l', 'o', '\0'};
  constexpr std::uint32_t kKey = 0xC0DEC0DEu;
  constexpr auto encoded = Encode(src, kKey);
  static_assert(encoded.size() == 5, "trailing NUL must be stripped");
  EXPECT_EQ(DecodeToString(encoded, kKey), "hello");
}

// ---- DecodeBytes ---------------------------------------------------------

TEST(DecodeBytesTest, RoundTripsByteBlob) {
  // DecodeBytes is the runtime-sized variant used for SPIR-V blobs.
  // It must agree with the templated Decode<> on the byte level.
  constexpr std::uint32_t kKey = 0xA8B14F62u;
  std::array<std::uint8_t, 8> plain{0, 1, 2, 0xFF, 0x80, 0x7F, 'a', 'Z'};
  std::array<std::uint8_t, 8> encoded{};
  for (std::size_t i = 0; i < plain.size(); ++i) {
    encoded[i] = MixByte(plain[i], kKey, i);
  }
  std::array<std::uint8_t, 8> decoded{};
  DecodeBytes(encoded.data(), encoded.size(), kKey, decoded.data());
  EXPECT_EQ(decoded, plain);
}

TEST(DecodeBytesTest, ZeroLengthIsNoop) {
  // size == 0 must not touch dst nor read from src. Pass nullptr for
  // both to make any accidental access trip ASAN immediately.
  DecodeBytes(nullptr, 0, 0xDEADBEEFu, nullptr);
  SUCCEED();
}

// ---- Holder + Scoped -----------------------------------------------------

TEST(MakeHolderTest, ProducesEncodedBytesAndKey) {
  constexpr auto h = Make("loadmaster", 0xA8B14F62u);
  static_assert(h.data.size() == 10, "10 chars without NUL");
  static_assert(h.kSize == 10);
  EXPECT_EQ(h.key, 0xA8B14F62u);
  // The encoded data should differ from the literal (same property
  // as EncodedDiffersFromPlaintext above; locks in the bundling).
  EXPECT_NE(std::memcmp(h.data.data(), "loadmaster", 10), 0);
}

TEST(ScopedTest, DecodesAndExposesCString) {
  constexpr std::uint32_t kKey = 0xA8B14F62u;
  constexpr auto encoded = Encode("loadmaster", kKey);
  Scoped<encoded.size()> scoped(encoded, kKey);
  EXPECT_STREQ(scoped.c_str(), "loadmaster");
  EXPECT_EQ(scoped.size(), 10u);
}

TEST(ScopedHolderTest, UnpacksHolder) {
  constexpr auto kHolder = Make("loadmaster 0.7.13", 0xA8B14F62u);
  ScopedHolder scoped(kHolder);
  EXPECT_STREQ(scoped.c_str(), "loadmaster 0.7.13");
  EXPECT_EQ(scoped.size(), std::string_view("loadmaster 0.7.13").size());
}

}  // namespace
