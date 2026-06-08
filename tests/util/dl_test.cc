// Unit tests for util::Dl{openAny,sym,close,error}.
//
// The dl-loader wraps POSIX dlopen/dlsym/dlclose and Win32
// LoadLibrary/GetProcAddress/FreeLibrary behind one interface. The
// happy-path (successfully loading a real vendor driver) is
// inherently host-dependent: there is no .so / .dll we can rely on
// existing in every CI worker. So this suite focuses on the
// observable contract that DOES hold on every host:
//
//   * Passing a list of names that none-of-them resolve returns
//     nullptr -- not a stale value, not a non-null sentinel.
//   * Dlsym(nullptr, ...) is safe and returns nullptr (the GPU
//     factory leans on this to make the "driver not present" path
//     branch-free).
//   * Dlclose(nullptr) is a no-op (same idiom; cleanup of an
//     unsuccessful load).
//   * Dlerror() always returns a non-null C string, even when no
//     error has occurred. Callers print it directly with %s.
//
// We deliberately do NOT try to load a known-good library here.
// Picking one that exists everywhere is surprisingly hard:
// libc / libSystem / msvcrt have different names across distros and
// over Windows versions; loading the test binary itself is platform
// specific (POSIX needs nullptr to dlopen the main program,
// Win32 wants GetModuleHandle). The success path is exercised
// indirectly by every real run of loadmaster on a host that has
// drivers -- the missing-driver path is what we can pin here
// portably.

#include "util/dl.h"

#include <cstring>

#include <gtest/gtest.h>

namespace {

TEST(DlopenAnyTest, ReturnsNullWhenNoNameResolves) {
  // A nonsense library name that no platform's loader can resolve:
  // no path separator, no extension, no chance of an accidental hit.
  // The trailing nullptr terminates the candidate list (the loader
  // contract documented in dl.h).
  const char *names[] = {
      "loadmaster-no-such-library-XYZZY-12345",
      nullptr,
  };
  EXPECT_EQ(util::DlopenAny(names), nullptr);
}

TEST(DlopenAnyTest, TriesAllCandidatesBeforeGivingUp) {
  // Pass several known-bad names; the loader must fall through every
  // one and ultimately return nullptr rather than stopping at the
  // first failure with a stale value.
  const char *names[] = {
      "loadmaster-no-such-library-AAA",
      "loadmaster-no-such-library-BBB",
      "loadmaster-no-such-library-CCC",
      nullptr,
  };
  EXPECT_EQ(util::DlopenAny(names), nullptr);
}

TEST(DlopenAnyTest, EmptyCandidateListReturnsNull) {
  // Just the sentinel: loop body never runs, function must return
  // nullptr cleanly (i.e. not segfault on the empty input).
  const char *names[] = {nullptr};
  EXPECT_EQ(util::DlopenAny(names), nullptr);
}

TEST(DlsymTest, NullHandleReturnsNull) {
  // The GPU factory writes:
  //   handle = DlopenAny(...);
  //   sym = Dlsym(handle, "...");  // even if handle is null
  // and relies on Dlsym(nullptr, ...) being safe -- not branching on
  // the handle first. Pin that idiom.
  EXPECT_EQ(util::Dlsym(nullptr, "anything"), nullptr);
}

TEST(DlcloseTest, NullHandleIsNoop) {
  // Symmetric with Dlsym(nullptr, ...). Cleanup paths that always
  // call Dlclose() (regardless of whether the open succeeded) lean
  // on this. Test just exercises the call -- no observable state to
  // assert, but ASAN / UBSAN runs would catch any null deref.
  util::Dlclose(nullptr);
  SUCCEED();
}

TEST(DlerrorTest, AlwaysReturnsNonNullCString) {
  // Callers print Dlerror() directly with %s. A nullptr return would
  // crash printf, so the contract is "always returns a valid C
  // string". The "<unknown>" / "<no error>" fallbacks in dl.cc are
  // what guarantee this.
  const char *e = util::Dlerror();
  ASSERT_NE(e, nullptr);
  EXPECT_NE(std::strlen(e), 0u) << "Dlerror() must return a non-empty string";
}

TEST(DlerrorTest, IsCallableAfterFailedOpen) {
  // After a known-failing DlopenAny, Dlerror() must still return a
  // valid string. This is the primary diagnostic path the GPU
  // factory uses when no driver could be loaded.
  const char *names[] = {"loadmaster-no-such-library-ZZZZZ", nullptr};
  (void)util::DlopenAny(names);
  const char *e = util::Dlerror();
  ASSERT_NE(e, nullptr);
  EXPECT_NE(std::strlen(e), 0u);
}

}  // namespace
