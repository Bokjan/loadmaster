// Unit test for the build-time SPIR-V embedding of the Intel busy kernel.
//
// src/CMakeLists.txt re-implements util::obfuscate::MixByte in CMake
// script to XOR-encode busy_kernel.spv into a generated header, and the
// key is written down twice (CMake and spirv_kernel.h). Nothing else
// checks that the two halves agree: a mismatch would only surface as a
// zeModuleCreate failure on real Intel hardware. Here we decode the
// generated array exactly as IntelDevice::Init() does and compare it with
// the .spv on disk.

#include "gpu/intel/spirv_kernel.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

namespace {

using gpu::intel::kBusyKernelSpirv;
using gpu::intel::kBusyKernelSpirvAvailable;
using gpu::intel::kBusyKernelSpirvKey;
using gpu::intel::kBusyKernelSpirvSize;

std::vector<std::uint8_t> Decoded() {
  std::vector<std::uint8_t> out(kBusyKernelSpirvSize);
  util::obfuscate::DecodeBytes(kBusyKernelSpirv, kBusyKernelSpirvSize, kBusyKernelSpirvKey,
                               out.data());
  return out;
}

TEST(SpirvEmbedTest, DecodesToASpirvModule) {
  if (!kBusyKernelSpirvAvailable) {
    GTEST_SKIP() << "no busy_kernel.spv embedded in this build";
  }
  ASSERT_EQ(kBusyKernelSpirvSize % 4, 0u) << "SPIR-V is a stream of 32-bit words";
  const auto bytes = Decoded();
  std::uint32_t magic = 0;
  std::memcpy(&magic, bytes.data(), sizeof(magic));
  EXPECT_EQ(magic, 0x07230203u) << "SPIR-V magic (little-endian)";
}

TEST(SpirvEmbedTest, MatchesTheSpvFileOnDisk) {
  if (!kBusyKernelSpirvAvailable) {
    GTEST_SKIP() << "no busy_kernel.spv embedded in this build";
  }
  std::ifstream in(LOADMASTER_SPIRV_SOURCE, std::ios::binary);
  ASSERT_TRUE(in) << "cannot open " << LOADMASTER_SPIRV_SOURCE;
  const std::vector<std::uint8_t> on_disk((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
  EXPECT_EQ(Decoded(), on_disk);
}

#if LOADMASTER_OBFUSCATE
TEST(SpirvEmbedTest, EmbeddedBytesAreMasked) {
  if (!kBusyKernelSpirvAvailable) {
    GTEST_SKIP() << "no busy_kernel.spv embedded in this build";
  }
  std::uint32_t first_word = 0;
  std::memcpy(&first_word, kBusyKernelSpirv, sizeof(first_word));
  EXPECT_NE(first_word, 0x07230203u) << "the raw magic must not appear in the binary";
}
#endif

}  // namespace
