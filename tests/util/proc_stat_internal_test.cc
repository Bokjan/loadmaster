// Unit tests for util::internal::ParseProcPidStat.
//
// The parser handles real-world /proc/<pid>/stat lines. Its single
// non-trivial responsibility is delimiting the `comm` field, which
// can contain spaces, parentheses, or both: the kernel renders comm
// inside parens but does NOT escape inner parens, so a naive
// "tokenize on whitespace" or "strchr(line, ')')" both go wrong on
// pathological process names. We feed in hand-built fixtures that
// exercise each of those cases.
//
// Linux-only: the entire helper compiles out on macOS / Windows and
// `fmemopen` is glibc-specific.

#include "core/constants.h"

#if IS_LINUX

#  include "util/proc_stat_internal.h"

#  include <cstdio>
#  include <cstring>
#  include <string>

#  include <gtest/gtest.h>

namespace {

using util::internal::kTaskCommLen;
using util::internal::ParseProcPidStat;
using util::internal::StatFields;

// RAII wrapper that owns both the backing buffer and the FILE* returned
// by fmemopen. We need this because fmemopen does NOT copy the buffer;
// it only stores the pointer. If we let the std::string die before the
// FILE*, fgets() ends up reading from freed memory -- which on most
// libcs silently returns stale or zeroed bytes and looked, in an earlier
// iteration of this test, like a real parser bug.
class StatStream final {
 public:
  explicit StatStream(std::string content)
      : buf_(std::move(content)),
        fp_(::fmemopen(buf_.data(), buf_.size(), "r")) {}
  ~StatStream() {
    if (fp_ != nullptr) {
      std::fclose(fp_);
    }
  }
  StatStream(const StatStream &) = delete;
  StatStream &operator=(const StatStream &) = delete;

  FILE *get() const { return fp_; }

 private:
  std::string buf_;  // outlives fp_
  FILE *fp_;
};

// Synthesize a /proc/<pid>/stat line with a caller-chosen comm and
// otherwise sane field values. Field order and semantics follow
// `man 5 proc`.
//
// We emit exactly the 22 fields ParseProcPidStat consumes plus a
// trailing newline. The extra kernel-side fields (vsize, rss, ...)
// are not required for parsing success.
std::string MakeStatLine(int pid, const std::string &comm) {
  // pid (comm) state ppid pgrp session tty_nr tpgid flags
  // minflt cminflt majflt cmajflt utime stime cutime cstime
  // priority nice num_threads itrealvalue starttime
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "%d (%s) R 1 1 1 0 -1 0 100 0 5 0 1234 5678 0 0 20 0 1 0 999999\n",
                pid, comm.c_str());
  return std::string(buf);
}

// Pretty-printer for failing assertions on the comm field. The struct
// stores comm as a fixed-size char array; gtest's default printer
// would dump it as a pointer, which is unhelpful.
std::string CommAsString(const StatFields &s) {
  // comm is always NUL-terminated by ParseProcPidStat (it writes
  // '\0' at copy_len <= kTaskCommLen - 1).
  return std::string(s.comm);
}

// ---- Happy path -----------------------------------------------------------

TEST(ParseProcPidStatTest, SimpleCommNoSpaces) {
  StatStream stream(MakeStatLine(4321, "loadmaster"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 4321);
  EXPECT_EQ(CommAsString(s), "loadmaster");
  EXPECT_EQ(s.state, 'R');
  EXPECT_EQ(s.ppid, 1);
  EXPECT_EQ(s.utime, 1234u);
  EXPECT_EQ(s.stime, 5678u);
  EXPECT_EQ(s.num_threads, 1);
  EXPECT_EQ(s.starttime, 999999u);
}

TEST(ParseProcPidStatTest, SignedFieldsCarryNegativeValues) {
  // Build a line where `nice` is negative (-5) and cutime is too.
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "%d (proc) S 1 1 1 0 -1 0 0 0 0 0 0 0 -10 0 20 -5 4 0 0\n",
                42);
  StatStream stream(buf);
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  ASSERT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 42);
  EXPECT_EQ(s.cutime, -10);
  EXPECT_EQ(s.nice, -5);
  EXPECT_EQ(s.num_threads, 4);
}

// ---- comm field edge cases ------------------------------------------------
//
// These are the cases the parser exists to handle correctly. A naive
// implementation that splits on whitespace, or that uses the FIRST
// ')' instead of the LAST, will fail one or more of these tests.

TEST(ParseProcPidStatTest, CommWithSpaces) {
  StatStream stream(MakeStatLine(1, "weird name with spaces"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 1);
  EXPECT_EQ(CommAsString(s), "weird name with");  // truncated -- see next test
  // ^ kTaskCommLen is 16 incl. NUL; the kernel itself truncates comm
  // to 15 chars, but our parser also caps the copy to fit the struct.
  EXPECT_EQ(s.state, 'R');
}

TEST(ParseProcPidStatTest, CommExactlyFifteenCharsFits) {
  // kTaskCommLen == 16 -> 15 visible chars + '\0'.
  const std::string max_comm = "abcdefghijklmno";  // 15 chars
  ASSERT_EQ(max_comm.size(), static_cast<size_t>(kTaskCommLen - 1));

  StatStream stream(MakeStatLine(7, max_comm));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(CommAsString(s), max_comm);
}

TEST(ParseProcPidStatTest, CommLongerThanBufferIsTruncated) {
  // 20-char comm; parser must truncate to 15 and NUL-terminate, never
  // overflow the destination.
  StatStream stream(MakeStatLine(7, "abcdefghijklmnopqrst"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(CommAsString(s).size(), static_cast<size_t>(kTaskCommLen - 1));
  EXPECT_EQ(CommAsString(s), "abcdefghijklmno");
}

TEST(ParseProcPidStatTest, CommContainsClosingParenInMiddle) {
  // The critical case: comm contains its own ')'. The parser must
  // pick the LAST ')' on the line, not the first, otherwise every
  // subsequent field shifts by one.
  StatStream stream(MakeStatLine(99, "weird) name"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 99);
  EXPECT_EQ(CommAsString(s), "weird) name");
  // If the parser had picked the FIRST ')', state would now hold ' '
  // and ppid parsing would have collapsed -- so checking state suffices.
  EXPECT_EQ(s.state, 'R');
  EXPECT_EQ(s.ppid, 1);
  EXPECT_EQ(s.utime, 1234u);
}

TEST(ParseProcPidStatTest, CommContainsOpeningParen) {
  StatStream stream(MakeStatLine(100, "foo(bar)baz"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 100);
  EXPECT_EQ(CommAsString(s), "foo(bar)baz");
  EXPECT_EQ(s.state, 'R');
}

TEST(ParseProcPidStatTest, CommWithSpacesAndInnerParens) {
  // The full nightmare: spaces AND inner parens.
  StatStream stream(MakeStatLine(101, "a (b) c"));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 101);
  EXPECT_EQ(CommAsString(s), "a (b) c");
  EXPECT_EQ(s.state, 'R');
  EXPECT_EQ(s.utime, 1234u);
}

TEST(ParseProcPidStatTest, EmptyComm) {
  // /proc/<pid>/stat with an empty comm: "12345 () R ...". Not
  // observable from userspace today (kernel always writes at least the
  // process name) but the parser should still handle it.
  StatStream stream(MakeStatLine(12345, ""));
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_TRUE(ParseProcPidStat(stream.get(), s));
  EXPECT_EQ(s.pid, 12345);
  EXPECT_EQ(CommAsString(s), "");
}

// ---- Malformed input -----------------------------------------------------

TEST(ParseProcPidStatTest, EmptyStreamFails) {
  StatStream stream("");
  // fmemopen("", 0) returns NULL on some libcs; tolerate both.
  if (stream.get() == nullptr) {
    GTEST_SKIP() << "fmemopen rejects zero-length buffer on this libc";
  }
  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

TEST(ParseProcPidStatTest, NoOpeningParenFails) {
  StatStream stream("42 no_parens here R 1 1\n");
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

TEST(ParseProcPidStatTest, NoClosingParenFails) {
  StatStream stream("42 (oops R 1 1\n");
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

TEST(ParseProcPidStatTest, ClosingBeforeOpeningFails) {
  // ')' before '(' -- malformed; parser must reject.
  StatStream stream("42 ) ( R 1 1\n");
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

TEST(ParseProcPidStatTest, NonNumericPidFails) {
  StatStream stream("nope (proc) R 1 1 1 0 -1 0 0 0 0 0 0 0 0 0 0 20 0 1 0 0\n");
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

TEST(ParseProcPidStatTest, MissingTrailingFieldsFails) {
  // Truncate the line after `flags` -- the inner sscanf must see fewer
  // than 20 conversions and the function must return false.
  StatStream stream("42 (proc) R 1 1 1 0 -1 0\n");
  ASSERT_NE(stream.get(), nullptr);

  StatFields s{};
  EXPECT_FALSE(ParseProcPidStat(stream.get(), s));
}

}  // namespace

#endif  // IS_LINUX
