// Unit tests for cli::ParseCommandLineArguments.
//
// This is the end-to-end shape: argv -> Options. We exercise the
// argument lexer in `cli.cc` together with the validation pipeline in
// `core::Options`. Failures here usually mean either a flag was renamed
// without updating the spec table, or an argument value passed the
// lexer but failed validation downstream -- both regressions we want
// to catch loudly.

#include "cli/cli.h"

#include <cstdlib>
#include <vector>

#include "core/options.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using cli::ParseCommandLineArguments;
using cli::ParseResult;
using core::Options;

// Helper: turn a brace-list of C-string literals into a (argc, argv)
// pair suitable for ParseCommandLineArguments. The returned vector
// owns the argv pointers; keep it alive for the duration of the call.
struct Argv {
  std::vector<const char *> ptrs;
  int argc() const { return static_cast<int>(ptrs.size()); }
  const char **argv() { return ptrs.data(); }
};

Argv MakeArgv(std::initializer_list<const char *> args) {
  Argv a;
  a.ptrs.reserve(args.size());
  for (const char *s : args) {
    a.ptrs.push_back(s);
  }
  return a;
}

class CliTest : public ::testing::Test {
 protected:
  void SetUp() override { util::logger_internal::g_default_logger->SetLevel(util::Logger::kOff); }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kWarn);
  }
};

// ---- Basic shapes --------------------------------------------------------

TEST_F(CliTest, NoArgumentsContinuesWithDefaults) {
  Options opts;
  auto a = MakeArgv({"loadmaster"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_FALSE(r.should_exit) << "no args -> run normally";
}

TEST_F(CliTest, UnknownFlagIsRejected) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "--not-a-real-flag"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_FAILURE);
  EXPECT_TRUE(r.should_exit);
}

TEST_F(CliTest, HelpRequestsCleanExit) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-h"});
  // -h prints usage to stdout; we don't care about the output here,
  // just the exit signal.
  testing::internal::CaptureStdout();
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  (void)testing::internal::GetCapturedStdout();
  EXPECT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_TRUE(r.should_exit) << "-h must signal the caller to exit";
}

TEST_F(CliTest, VersionRequestsCleanExit) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-v"});
  testing::internal::CaptureStdout();
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  const std::string out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_TRUE(r.should_exit);
  EXPECT_FALSE(out.empty()) << "-v should print *something* to stdout";
}

// ---- Integer-valued flags ------------------------------------------------

TEST_F(CliTest, CpuLoadFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-l", "120"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_FALSE(r.should_exit);
  EXPECT_EQ(opts.GetCpuLoad(), 120);
}

TEST_F(CliTest, MissingValueAfterFlagFails) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-l"});  // no value
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_FAILURE);
  EXPECT_TRUE(r.should_exit);
}

TEST_F(CliTest, NonIntegerValueRejected) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-l", "abc"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_FAILURE);
}

TEST_F(CliTest, MemoryFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-m", "16"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_EQ(opts.GetMemoryBytes(), int64_t{16} * 1024 * 1024);
}

// ---- String-valued flags -------------------------------------------------

TEST_F(CliTest, CpuAlgorithmFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-ca", "rand_normal"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kRandomNormal);
}

TEST_F(CliTest, GpuVendorFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-gv", "amd"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_EQ(opts.GetGpuVendor(), Options::GpuVendor::kAmd);
}

TEST_F(CliTest, GpuIndicesFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-gi", "0,2"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  ASSERT_EQ(opts.GetGpuIndices().size(), 2u);
  EXPECT_EQ(opts.GetGpuIndices()[0], 0);
  EXPECT_EQ(opts.GetGpuIndices()[1], 2);
}

TEST_F(CliTest, GpuIndicesAllKeepsVectorEmpty) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-gi", "all"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_TRUE(opts.GpuUseAllDevices());
}

TEST_F(CliTest, GpuAlgorithmFlagParsed) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-ga", "rand_normal"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_EQ(opts.GetGpuAlgorithm(), Options::GpuAlgorithm::kRandomNormal);
}

// ---- Cross-flag combinations --------------------------------------------

TEST_F(CliTest, FullArgumentSet) {
  Options opts;
  auto a = MakeArgv({
      "loadmaster",
      "-l",
      "150",
      "-c",
      "1",
      "-ca",
      "rand_normal",
      "-m",
      "32",
      "-g",
      "60",
      "-gm",
      "256",
      "-gi",
      "0",
      "-gv",
      "nvidia",
  });
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_FALSE(r.should_exit);

  EXPECT_EQ(opts.GetCpuLoad(), 150);
  EXPECT_EQ(opts.GetCpuCount(), 1);
  EXPECT_EQ(opts.GetCpuAlgorithm(), Options::CpuAlgorithm::kRandomNormal);
  EXPECT_EQ(opts.GetMemoryBytes(), int64_t{32} * 1024 * 1024);
  EXPECT_EQ(opts.GetGpuLoad(), 60);
  EXPECT_EQ(opts.GetGpuMemoryBytes(), int64_t{256} * 1024 * 1024);
  ASSERT_EQ(opts.GetGpuIndices().size(), 1u);
  EXPECT_EQ(opts.GetGpuIndices()[0], 0);
  EXPECT_EQ(opts.GetGpuVendor(), Options::GpuVendor::kNvidia);
}

TEST_F(CliTest, LaterFlagOverridesEarlierFlag) {
  // The current parser walks argv left-to-right and writes into the
  // optional unconditionally; the last write wins. Pin this so any
  // future "duplicate flag is an error" rework is an intentional
  // breaking change.
  Options opts;
  auto a = MakeArgv({"loadmaster", "-l", "100", "-l", "200"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  ASSERT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_EQ(opts.GetCpuLoad(), 200);
}

TEST_F(CliTest, ValidationFailureBubblesUp) {
  // -g 999 makes the lexer happy (it's an integer) but the Options
  // validation rejects it (out of [0, kGpuMaxLoadPerDevice]).
  Options opts;
  auto a = MakeArgv({"loadmaster", "-g", "999"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_FAILURE);
  EXPECT_TRUE(r.should_exit);
}

TEST_F(CliTest, LogLevelFlagAccepted) {
  // -L mutates the global logger; the fixture restores the level on
  // teardown so subsequent tests don't see "off" or whatever was set.
  Options opts;
  auto a = MakeArgv({"loadmaster", "-L", "error"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_SUCCESS);
  EXPECT_FALSE(r.should_exit);
}

TEST_F(CliTest, LogLevelInvalidRejected) {
  Options opts;
  auto a = MakeArgv({"loadmaster", "-L", "louder"});
  const ParseResult r = ParseCommandLineArguments(opts, a.argc(), a.argv());
  EXPECT_EQ(r.exit_code, EXIT_FAILURE);
}

}  // namespace
