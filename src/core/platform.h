#pragma once

// Platform detection macros. Keep this header small and free of project
// types so it can be pulled in by low-level utilities (util/dl.cc,
// util/clock.cc, ...) without creating include cycles.

#ifndef IS_WINDOWS
#  if defined(_WIN32) || defined(_WIN64)
#    define IS_WINDOWS (1)
#  else
#    define IS_WINDOWS (0)
#  endif
#endif

// macOS / Darwin (XNU). Has POSIX dlopen/sigaction/sysconf, but no /proc
// filesystem -- CPU / memory stats use Mach + libproc instead.
#ifndef IS_MACOS
#  if defined(__APPLE__) && defined(__MACH__)
#    define IS_MACOS (1)
#  else
#    define IS_MACOS (0)
#  endif
#endif

// BSD family (FreeBSD, DragonFly, OpenBSD, NetBSD). Like macOS these are
// POSIX (dlopen/sigaction/sysconf available) but they have no /proc-based
// stats path either: CPU / per-process counters come from sysctl(3).
// Note: __APPLE__ also #defines __MACH__ but never any of the *BSD macros
// below, so IS_MACOS and IS_BSD are mutually exclusive without further
// gating.
#ifndef IS_BSD
#  if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#    define IS_BSD (1)
#  else
#    define IS_BSD (0)
#  endif
#endif

// Linux (specifically: /proc-based stats path). Now matched explicitly
// against __linux__ -- previously this was a catch-all for "not Windows,
// not macOS", which silently lumped any new POSIX target (BSDs) into the
// Linux branch and made them try to read /proc files that may not exist.
#ifndef IS_LINUX
#  if defined(__linux__)
#    define IS_LINUX (1)
#  else
#    define IS_LINUX (0)
#  endif
#endif

#if IS_WINDOWS
// Avoid <windows.h> dragging in winsock1, GDI, and the min/max macros that
// would clobber std::min / std::max. These #defines must precede every
// inclusion of <windows.h> in the project.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
