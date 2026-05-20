#include "dl.h"

#include "core/constants.h"

#if IS_WINDOWS
#    include <windows.h>
#    include <cstdio>
#    include <cstring>
#else
#    include <dlfcn.h>
#endif

namespace util {

#if IS_WINDOWS
namespace {
thread_local char g_err_buf[256];

const char *FormatLastError() {
  const DWORD err = ::GetLastError();
  if (err == 0) {
    g_err_buf[0] = '\0';
    return "<no error>";
  }
  // FormatMessage may write a trailing CRLF; we'll trim it.
  char msg_buf[192] = {};
  DWORD len = ::FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg_buf,
      static_cast<DWORD>(sizeof(msg_buf)), nullptr);
  if (len == 0) {
    std::snprintf(g_err_buf, sizeof(g_err_buf), "Win32 error %lu",
                  static_cast<unsigned long>(err));
  } else {
    while (len > 0 && (msg_buf[len - 1] == '\r' || msg_buf[len - 1] == '\n' ||
                       msg_buf[len - 1] == ' ')) {
      msg_buf[--len] = '\0';
    }
    std::snprintf(g_err_buf, sizeof(g_err_buf), "%s (Win32 error %lu)", msg_buf,
                  static_cast<unsigned long>(err));
  }
  return g_err_buf;
}
}  // namespace

DlHandle DlopenAny(const char *const *names) {
  // Use LoadLibraryExA with explicit search flags so that the system DLL
  // directory (where vendor GPU drivers live, e.g. nvcuda.dll) is always
  // considered, regardless of any process-wide default search-path
  // restrictions inherited from the launcher.
  constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                           LOAD_LIBRARY_SEARCH_SYSTEM32 |
                           LOAD_LIBRARY_SEARCH_USER_DIRS |
                           LOAD_LIBRARY_SEARCH_APPLICATION_DIR;
  for (int i = 0; names[i] != nullptr; ++i) {
    const char *name = names[i];
    HMODULE h = ::LoadLibraryExA(name, nullptr, kFlags);
    if (h != nullptr) {
      return reinterpret_cast<DlHandle>(h);
    }
    // Fallback: plain LoadLibraryA, which also follows %PATH%. Some
    // vendor SDKs (e.g. AMD HIP) install into a directory that is only
    // reachable via PATH.
    h = ::LoadLibraryA(name);
    if (h != nullptr) {
      return reinterpret_cast<DlHandle>(h);
    }
    // Final fallback for short names: try an absolute path under
    // %SystemRoot%\System32. This handles environments where the
    // process default DLL search path has been hardened so much that
    // even System32 is excluded, or where some intermediary has
    // redirected the System32 search to a non-existent location.
    const bool has_sep = (std::strchr(name, '\\') != nullptr) ||
                         (std::strchr(name, '/') != nullptr);
    if (!has_sep) {
      char abs_path[MAX_PATH];
      const UINT n = ::GetSystemDirectoryA(abs_path, sizeof(abs_path));
      if (n > 0 && n < sizeof(abs_path)) {
        if (std::snprintf(abs_path + n, sizeof(abs_path) - n, "\\%s", name) > 0) {
          h = ::LoadLibraryExA(abs_path, nullptr, kFlags);
          if (h != nullptr) {
            return reinterpret_cast<DlHandle>(h);
          }
          h = ::LoadLibraryA(abs_path);
          if (h != nullptr) {
            return reinterpret_cast<DlHandle>(h);
          }
        }
      }
    }
  }
  return nullptr;
}

void *Dlsym(DlHandle handle, const char *name) {
  if (handle == nullptr) {
    return nullptr;
  }
  FARPROC p = ::GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
  return reinterpret_cast<void *>(p);
}

void Dlclose(DlHandle handle) {
  if (handle != nullptr) {
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
  }
}

const char *Dlerror() { return FormatLastError(); }

#else  // POSIX

DlHandle DlopenAny(const char *const *names) {
  for (int i = 0; names[i] != nullptr; ++i) {
    void *h = ::dlopen(names[i], RTLD_LAZY | RTLD_LOCAL);
    if (h != nullptr) {
      return h;
    }
  }
  return nullptr;
}

void *Dlsym(DlHandle handle, const char *name) {
  if (handle == nullptr) {
    return nullptr;
  }
  return ::dlsym(handle, name);
}

void Dlclose(DlHandle handle) {
  if (handle != nullptr) {
    ::dlclose(handle);
  }
}

const char *Dlerror() {
  const char *s = ::dlerror();
  return (s != nullptr) ? s : "<unknown>";
}

#endif

}  // namespace util
