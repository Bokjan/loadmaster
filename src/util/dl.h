#pragma once

// Cross-platform shared-library loading. Wraps POSIX dlopen/dlsym/dlclose
// (Linux) and Win32 LoadLibraryA/GetProcAddress/FreeLibrary (Windows)
// behind the same minimal interface. Used by the GPU module to load
// vendor drivers on demand.

namespace util {

using DlHandle = void *;

// Try to load each candidate library name in order. Returns the handle
// of the first one that succeeds, or nullptr if all fail.
//   names: nullptr-terminated array of library names
//          (e.g. "libcuda.so.1" on Linux, "nvcuda.dll" on Windows).
DlHandle DlopenAny(const char *const *names);

// Resolve a symbol; returns nullptr if not present.
void *Dlsym(DlHandle handle, const char *name);

// Close a handle; safe on nullptr.
void Dlclose(DlHandle handle);

// Last-error message; valid until the next util::Dl* call on this thread.
// Always returns a non-null C string ("<unknown>" if no error info).
const char *Dlerror();

}  // namespace util
