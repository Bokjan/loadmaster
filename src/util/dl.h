#pragma once

// Cross-platform shared-library loading. Wraps POSIX dlopen/dlsym/dlclose
// (Linux) and Win32 LoadLibraryA/GetProcAddress/FreeLibrary (Windows)
// behind the same minimal interface. Used by the GPU module to load
// vendor drivers on demand.

#include <cstring>

namespace util {

using DlHandle = void *;

// Try to load each candidate library name in order. Returns the handle
// of the first one that succeeds, or nullptr if all fail.
//
// Windows search order per name: System32, then the absolute directories
// in %PATH% -- never the current directory or the executable's directory,
// so a DLL planted next to the binary or in the working directory is not
// picked up. POSIX: plain dlopen() search (LD_LIBRARY_PATH, ld.so cache).
//   names: nullptr-terminated array of library names
//          (e.g. "libcuda.so.1" on Linux, "nvcuda.dll" on Windows).
DlHandle DlopenAny(const char *const *names);

// Resolve a symbol; returns nullptr if not present.
void *Dlsym(DlHandle handle, const char *name);

// Resolve a symbol into the function pointer `slot`. Returns false (and
// leaves `slot` alone) if not present; Dlerror() says why.
template <typename Fn>
bool DlsymInto(DlHandle handle, const char *name, Fn &slot) {
  void *sym = Dlsym(handle, name);
  if (sym == nullptr) {
    return false;
  }
  // void* -> function pointer: reinterpret_cast is conditionally-supported
  // (implementation-defined) per the C++ standard, so the conversion is
  // not portable well-formed; copy the bytes for a defined conversion.
  static_assert(sizeof(Fn) == sizeof(sym), "pointer size mismatch");
  std::memcpy(&slot, &sym, sizeof(slot));
  return true;
}

// Close a handle; safe on nullptr.
void Dlclose(DlHandle handle);

// Last-error message; valid until the next util::Dl* call on this thread.
// Always returns a non-null C string ("<unknown>" if no error info).
const char *Dlerror();

}  // namespace util
