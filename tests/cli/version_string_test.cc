// Unit tests for cli::VersionString().
//
// VersionString() is the public entry point that callers (cli.cc, the
// `-v` handler, tests) rely on to print "loadmaster X.Y.Z[-suffix]".
// The string is assembled at compile time (consteval) and -- when
// LOADMASTER_OBFUSCATE is on -- materialised on first call by
// decoding an XOR-encoded byte array. We don't poke at the
// obfuscation internals here (that's util/obfuscate_test.cc); we
// only pin the externally visible contract:
//
//   * Returns a NUL-terminated string of the form
//     "<project> <major>.<minor>.<patch>[-<suffix>]" using the
//     current `core::version` constants. We rebuild the expected
//     string from those constants so a deliberate version bump
//     (the only legitimate way the output changes) doesn't break
//     the test.
//   * Returns the same pointer on every call within a thread (the
//     implementation caches into a `thread_local` buffer).
//   * Different threads each get a valid, equal string (each
//     thread populates its own cache on first call).

#include "cli/version_string.h"

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/version.h"

#include <gtest/gtest.h>

namespace {

// Rebuild the expected version string from the current core::version
// constants. Mirrors the consteval assembly inside version_string.h
// (project + ' ' + maj + '.' + min + '.' + patch + optional "-suffix")
// so this test passes through any legitimate version bump without
// hardcoding the digits.
std::string ExpectedVersionString() {
  using namespace core::version;
  std::string s;
  s.reserve(32);
  s.append(kVersionProject);
  s.push_back(' ');
  s.append(std::to_string(kVersionMajor));
  s.push_back('.');
  s.append(std::to_string(kVersionMinor));
  s.push_back('.');
  s.append(std::to_string(kVersionPatch));
  const std::string_view suffix{kVersionSuffix};
  if (!suffix.empty()) {
    s.push_back('-');
    s.append(suffix);
  }
  return s;
}

TEST(VersionStringTest, MatchesExpectedComposition) {
  const char *got = cli::VersionString();
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(std::string(got), ExpectedVersionString());
}

TEST(VersionStringTest, StartsWithProjectName) {
  // Cheap separate assertion so a regression in just the project-name
  // prefix (e.g. someone changes kVersionProject and breaks branding)
  // points at the right spot in the diff.
  const std::string_view got = cli::VersionString();
  EXPECT_TRUE(got.starts_with(core::version::kVersionProject)) << "got: " << got;
}

TEST(VersionStringTest, IsNulTerminatedAndNonEmpty) {
  const char *got = cli::VersionString();
  ASSERT_NE(got, nullptr);
  // strlen against the declared length: the cached buffer is sized to
  // hold exactly the assembled string + NUL; reading strlen must not
  // run past the buffer.
  const std::size_t len = std::char_traits<char>::length(got);
  EXPECT_GT(len, 0u);
  EXPECT_EQ(got[len], '\0');
}

TEST(VersionStringTest, RepeatedCallsAreStableWithinThread) {
  // The cache is thread_local: every call within the same thread
  // must return the SAME pointer (no per-call decode, no copy).
  const char *first = cli::VersionString();
  const char *second = cli::VersionString();
  const char *third = cli::VersionString();
  EXPECT_EQ(first, second);
  EXPECT_EQ(second, third);
  // And of course the contents stay equal.
  EXPECT_STREQ(first, ExpectedVersionString().c_str());
}

TEST(VersionStringTest, ContentsAreCorrectAcrossThreads) {
  // Each worker thread populates its own thread_local cache on first
  // call. Pointers may differ (and that's fine), but every thread
  // must see the same string contents. Run a handful of workers in
  // parallel to catch any latent data-race on a shared static.
  constexpr int kThreads = 8;
  std::vector<std::string> results(kThreads);
  std::vector<std::thread> ts;
  ts.reserve(kThreads);
  std::atomic<int> ready{0};
  for (int i = 0; i < kThreads; ++i) {
    ts.emplace_back([i, &results, &ready] {
      // Spin briefly so all threads hit VersionString() roughly
      // together -- maximises the chance of catching a race if one
      // were to be introduced (e.g. by replacing thread_local with
      // a plain static without a guard).
      ready.fetch_add(1, std::memory_order_relaxed);
      while (ready.load(std::memory_order_relaxed) < kThreads) {
        std::this_thread::yield();
      }
      results[i] = cli::VersionString();
    });
  }
  for (auto &t : ts) {
    t.join();
  }
  const std::string expected = ExpectedVersionString();
  for (int i = 0; i < kThreads; ++i) {
    EXPECT_EQ(results[i], expected) << "thread " << i;
  }
}

}  // namespace
