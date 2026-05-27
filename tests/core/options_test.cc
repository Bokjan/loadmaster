// Unit tests for core::Options::ProcessCliArguments.
//
// Options is the validated, type-safe view of CLI input. The bulk of
// the per-flag rules live here (range checks, enum mapping, GPU index
// list parsing, etc.) and they are exactly the kind of thing that
// silently rots when someone tweaks the CLI surface. We test the
// validation matrix directly against a hand-built CliArgument, without
// going through argv parsing.

#include "core/options.h"

#include <cstdint>

#include "cli/cli_argument.h"
#include "core/constants.h"
#include "cpu/stat.h"
#include "gpu/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using cli::CliArgument;
using core::Options;

// Silence the LOG_ERROR noise that ProcessCliArguments emits on
// validation failures: we deliberately exercise those paths and don't
// want a wall of red text in the gtest output. Restored on teardown.
class OptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    saved_level_ = util::logger_internal::g_default_logger;  // pointer is enough
    // Drop the level to "off" without touching the logger object itself.
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kOff);
  }
  void TearDown() override {
    // Restore the project's default level so subsequent tests / binaries
    // are unaffected. The default Logger constructor uses kWarn.
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kWarn);
    (void)saved_level_;
  }

 private:
  util::Logger *saved_level_ = nullptr;
};

TEST_F(OptionsTest, DefaultsMatchConstants) {
  Options opts;
  EXPECT_EQ(opts.GetCpuLoad(), kDefaultCpuLoad);
  EXPECT_EQ(opts.GetCpuCount(), 0);
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kDefault);
  EXPECT_EQ(opts.GetMemoryBytes(), static_cast<int64_t>(kDefaultMemoryLoadMiB) * kMebiByte);
  EXPECT_EQ(opts.GetGpuLoad(), kDefaultGpuLoad);
  EXPECT_EQ(opts.GetGpuMemoryBytes(), static_cast<int64_t>(kDefaultGpuMemoryMiB) * kMebiByte);
  EXPECT_TRUE(opts.GpuUseAllDevices());
  EXPECT_EQ(opts.GetGpuVendor(), Options::GpuVendor::kAuto);
}

TEST_F(OptionsTest, EmptyArgumentsAreNoop) {
  Options opts;
  CliArgument args;  // all std::optional are empty
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  // Everything should still be at its constructor-set default.
  EXPECT_EQ(opts.GetCpuLoad(), kDefaultCpuLoad);
  EXPECT_EQ(opts.GetGpuVendor(), Options::GpuVendor::kAuto);
}

// ---- CPU ------------------------------------------------------------------

TEST_F(OptionsTest, CpuLoadIsForwardedRawWithoutBoundsCheck) {
  // The current implementation does NOT clamp cpu_load; it trusts the
  // value. This test pins that behavior so a future change becomes a
  // deliberate decision rather than an accidental tightening.
  Options opts;
  CliArgument args;
  args.cpu_load = 350;
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetCpuLoad(), 350);
}

TEST_F(OptionsTest, CpuCountAcceptsBelowHardwareLimit) {
  Options opts;
  CliArgument args;
  // 1 thread is universally available on any host that can run gtest.
  args.cpu_count = 1;
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetCpuCount(), 1);
}

TEST_F(OptionsTest, CpuCountRejectsAboveHardwareLimit) {
  Options opts;
  CliArgument args;
  // hardware_concurrency() can theoretically return 0 on exotic
  // hosts; pick something that overflows any realistic CI machine.
  args.cpu_count = cpu::CoreCount() + 1000;
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, CpuAlgorithmDefault) {
  Options opts;
  CliArgument args;
  args.cpu_algorithm = std::string_view("default");
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kDefault);
}

TEST_F(OptionsTest, CpuAlgorithmRandomNormal) {
  Options opts;
  CliArgument args;
  args.cpu_algorithm = std::string_view("rand_normal");
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kRandomNormal);
}

TEST_F(OptionsTest, CpuAlgorithmUnknownRejected) {
  Options opts;
  CliArgument args;
  args.cpu_algorithm = std::string_view("totally-not-real");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- Memory --------------------------------------------------------------

TEST_F(OptionsTest, MemoryMibConvertsToBytes) {
  Options opts;
  CliArgument args;
  args.memory_mb = 4;
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetMemoryBytes(), int64_t{4} * kMebiByte);
}

TEST_F(OptionsTest, MemoryZeroIsValid) {
  Options opts;
  CliArgument args;
  args.memory_mb = 0;
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetMemoryBytes(), 0);
}

TEST_F(OptionsTest, MemoryNegativeRejected) {
  Options opts;
  CliArgument args;
  args.memory_mb = -1;
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- GPU load / memory ---------------------------------------------------

TEST_F(OptionsTest, GpuLoadInRangeAccepted) {
  for (int v : {0, 1, 50, kGpuMaxLoadPerDevice}) {
    Options opts;
    CliArgument args;
    args.gpu_load = v;
    EXPECT_TRUE(opts.ProcessCliArguments(args)) << "v=" << v;
    EXPECT_EQ(opts.GetGpuLoad(), v);
  }
}

TEST_F(OptionsTest, GpuLoadOutOfRangeRejected) {
  for (int v : {-1, kGpuMaxLoadPerDevice + 1, 1000}) {
    Options opts;
    CliArgument args;
    args.gpu_load = v;
    EXPECT_FALSE(opts.ProcessCliArguments(args)) << "v=" << v;
  }
}

TEST_F(OptionsTest, GpuMemoryMibConvertsToBytes) {
  Options opts;
  CliArgument args;
  args.gpu_memory_mb = 256;
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetGpuMemoryBytes(), int64_t{256} * kMebiByte);
}

TEST_F(OptionsTest, GpuMemoryNegativeRejected) {
  Options opts;
  CliArgument args;
  args.gpu_memory_mb = -5;
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- GPU indices ---------------------------------------------------------

TEST_F(OptionsTest, GpuIndicesAllMeansUseEveryDevice) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("all");
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_TRUE(opts.GpuUseAllDevices());
  EXPECT_TRUE(opts.GetGpuIndices().empty());
}

TEST_F(OptionsTest, GpuIndicesSingleValue) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("3");
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_FALSE(opts.GpuUseAllDevices());
  ASSERT_EQ(opts.GetGpuIndices().size(), 1u);
  EXPECT_EQ(opts.GetGpuIndices()[0], 3);
}

TEST_F(OptionsTest, GpuIndicesCommaSeparated) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,2,7");
  EXPECT_TRUE(opts.ProcessCliArguments(args));
  ASSERT_EQ(opts.GetGpuIndices().size(), 3u);
  EXPECT_EQ(opts.GetGpuIndices()[0], 0);
  EXPECT_EQ(opts.GetGpuIndices()[1], 2);
  EXPECT_EQ(opts.GetGpuIndices()[2], 7);
}

TEST_F(OptionsTest, GpuIndicesRejectsEmptyString) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsTrailingComma) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,1,");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsLeadingComma) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view(",0");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsConsecutiveCommas) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,,1");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsNonNumericToken) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,abc,2");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsPartiallyNumericToken) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,1x,2");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsNegativeIndex) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,-1");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuIndicesRejectsDuplicates) {
  Options opts;
  CliArgument args;
  args.gpu_indices = std::string_view("0,2,0");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- GPU vendor ----------------------------------------------------------

TEST_F(OptionsTest, GpuVendorAllAcceptedTokens) {
  struct Case {
    std::string_view spec;
    Options::GpuVendor expected;
  };
  const Case cases[] = {
      {"auto", Options::GpuVendor::kAuto},
      {"nvidia", Options::GpuVendor::kNvidia},
      {"amd", Options::GpuVendor::kAmd},
      {"apple", Options::GpuVendor::kApple},
      {"intel", Options::GpuVendor::kIntel},
      {"opencl", Options::GpuVendor::kOpenCL},
  };
  for (const auto &c : cases) {
    Options opts;
    CliArgument args;
    args.gpu_vendor = c.spec;
    EXPECT_TRUE(opts.ProcessCliArguments(args)) << "spec=" << c.spec;
    EXPECT_EQ(opts.GetGpuVendor(), c.expected) << "spec=" << c.spec;
  }
}

TEST_F(OptionsTest, GpuVendorUnknownRejected) {
  Options opts;
  CliArgument args;
  args.gpu_vendor = std::string_view("vulkan");  // not a recognized backend
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

TEST_F(OptionsTest, GpuVendorIsCaseSensitive) {
  // Current behavior is exact-match on the lowercase tokens. Pin it.
  Options opts;
  CliArgument args;
  args.gpu_vendor = std::string_view("NVIDIA");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- GPU algorithm -------------------------------------------------------

TEST_F(OptionsTest, GpuAlgorithmDefaultsToDefault) {
  Options opts;
  EXPECT_EQ(opts.GetGpuAlgorithm(), Options::GpuAlgorithm::kDefault);
}

TEST_F(OptionsTest, GpuAlgorithmAllAcceptedTokens) {
  struct Case {
    std::string_view spec;
    Options::GpuAlgorithm expected;
  };
  const Case cases[] = {
      {"default", Options::GpuAlgorithm::kDefault},
      {"rand_normal", Options::GpuAlgorithm::kRandomNormal},
  };
  for (const auto &c : cases) {
    Options opts;
    CliArgument args;
    args.gpu_algorithm = c.spec;
    EXPECT_TRUE(opts.ProcessCliArguments(args)) << "spec=" << c.spec;
    EXPECT_EQ(opts.GetGpuAlgorithm(), c.expected) << "spec=" << c.spec;
  }
}

TEST_F(OptionsTest, GpuAlgorithmUnknownRejected) {
  Options opts;
  CliArgument args;
  args.gpu_algorithm = std::string_view("uniform");
  EXPECT_FALSE(opts.ProcessCliArguments(args));
}

// ---- Multi-field combinations --------------------------------------------

TEST_F(OptionsTest, AllFieldsTogether) {
  Options opts;
  CliArgument args;
  args.cpu_load = 150;
  args.cpu_count = 1;
  args.cpu_algorithm = std::string_view("rand_normal");
  args.memory_mb = 64;
  args.gpu_load = 80;
  args.gpu_memory_mb = 512;
  args.gpu_indices = std::string_view("0,1");
  args.gpu_vendor = std::string_view("nvidia");

  ASSERT_TRUE(opts.ProcessCliArguments(args));
  EXPECT_EQ(opts.GetCpuLoad(), 150);
  EXPECT_EQ(opts.GetCpuCount(), 1);
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kRandomNormal);
  EXPECT_EQ(opts.GetMemoryBytes(), int64_t{64} * kMebiByte);
  EXPECT_EQ(opts.GetGpuLoad(), 80);
  EXPECT_EQ(opts.GetGpuMemoryBytes(), int64_t{512} * kMebiByte);
  ASSERT_EQ(opts.GetGpuIndices().size(), 2u);
  EXPECT_EQ(opts.GetGpuIndices()[0], 0);
  EXPECT_EQ(opts.GetGpuIndices()[1], 1);
  EXPECT_EQ(opts.GetGpuVendor(), Options::GpuVendor::kNvidia);
}

}  // namespace
