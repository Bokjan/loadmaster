#include "dl.h"

#include "core/platform.h"

#if IS_WINDOWS
#  include <windows.h>
#  include <cstdio>
#  include <cstring>
#  include <string>
#else
#  include <dlfcn.h>
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
  DWORD len = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                               err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg_buf,
                               static_cast<DWORD>(sizeof(msg_buf)), nullptr);
  if (len == 0) {
    std::snprintf(g_err_buf, sizeof(g_err_buf), "Win32 error %lu", static_cast<unsigned long>(err));
  } else {
    while (len > 0 &&
           (msg_buf[len - 1] == '\r' || msg_buf[len - 1] == '\n' || msg_buf[len - 1] == ' ')) {
      msg_buf[--len] = '\0';
    }
    std::snprintf(g_err_buf, sizeof(g_err_buf), "%s (Win32 error %lu)", msg_buf,
                  static_cast<unsigned long>(err));
  }
  return g_err_buf;
}

// Try `name` in every absolute directory listed in %PATH%. Relative
// entries (".", "bin", empty) are skipped: they resolve against the
// current directory, which is exactly the DLL-planting vector we avoid.
// Some vendor SDKs (notably the AMD HIP SDK) are only reachable this way.
HMODULE LoadFromPath(const char *name) {
  const DWORD needed = ::GetEnvironmentVariableA("PATH", nullptr, 0);
  if (needed == 0) {
    return nullptr;
  }
  std::string path(needed, '\0');
  const DWORD len = ::GetEnvironmentVariableA("PATH", path.data(), needed);
  path.resize(len);
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find(';', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    std::string dir = path.substr(start, end - start);
    start = end + 1;
    if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') {
      dir = dir.substr(1, dir.size() - 2);
    }
    // Absolute only: "C:\..." or a UNC "\\server\...".
    const bool absolute = (dir.size() >= 3 && dir[1] == ':' && (dir[2] == '\\' || dir[2] == '/')) ||
                          (dir.size() >= 2 && dir[0] == '\\' && dir[1] == '\\');
    if (!absolute) {
      continue;
    }
    if (dir.back() != '\\' && dir.back() != '/') {
      dir += '\\';
    }
    const std::string full = dir + name;
    if (::GetFileAttributesA(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
      continue;
    }
    // DLL_LOAD_DIR lets the library find its sibling DLLs (e.g. HIP's
    // amd_comgr) next to it.
    HMODULE h = ::LoadLibraryExA(
        full.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (h != nullptr) {
      return h;
    }
  }
  return nullptr;
}
}  // namespace

DlHandle DlopenAny(const char *const *names) {
  // Take the current directory out of the legacy search order for this
  // process, so neither our loads nor the vendor libraries' own by-name
  // loads can pick up a DLL planted in the working directory.
  static const bool kCwdRemoved = ::SetDllDirectoryA("") != 0;
  (void)kCwdRemoved;

  // Vendor GPU drivers (nvcuda.dll, OpenCL.dll, ze_loader.dll) live in
  // System32. The search is restricted to it on purpose: the previous
  // flags also searched the application directory, and a plain
  // LoadLibraryA() fallback followed the legacy order (current
  // directory, then %PATH%) -- with `-gv auto` probing every vendor, a
  // nvcuda.dll dropped next to the exe or in the working directory (say,
  // Downloads) on a machine without NVIDIA would have been loaded and run.
  constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;
  for (int i = 0; names[i] != nullptr; ++i) {
    const char *name = names[i];
    HMODULE h = ::LoadLibraryExA(name, nullptr, kFlags);
    if (h != nullptr) {
      return reinterpret_cast<DlHandle>(h);
    }
    const bool has_sep =
        (std::strchr(name, '\\') != nullptr) || (std::strchr(name, '/') != nullptr);
    if (has_sep) {
      continue;
    }
    // Absolute path under %SystemRoot%\System32, for environments where
    // the search-flag variant above is unavailable or redirected.
    // snprintf returns the length it *would* have written; a value >= the
    // room left means the path was truncated, so don't load it.
    char abs_path[MAX_PATH];
    const UINT n = ::GetSystemDirectoryA(abs_path, sizeof(abs_path));
    const int written =
        (n > 0 && n < sizeof(abs_path))
            ? std::snprintf(abs_path + n, sizeof(abs_path) - n, "\\%s", name)
            : -1;
    if (written > 0 && static_cast<size_t>(written) < sizeof(abs_path) - n) {
      h = ::LoadLibraryExA(abs_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (h != nullptr) {
        return reinterpret_cast<DlHandle>(h);
      }
    }
    // Last resort: absolute %PATH% directories (HIP SDK).
    h = LoadFromPath(name);
    if (h != nullptr) {
      return reinterpret_cast<DlHandle>(h);
    }
  }
  return nullptr;
}

void *Dlsym(DlHandle handle, const char *name) {
  if (handle == nullptr) {
    return nullptr;
  }
  FARPROC p = ::GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
  // FARPROC (function pointer) -> void* (object pointer): a direct
  // reinterpret_cast is conditionally-supported / implementation-defined
  // per the C++ standard, so copy the bytes for a well-defined conversion.
  void *sym = nullptr;
  static_assert(sizeof(sym) == sizeof(p), "pointer size mismatch");
  std::memcpy(&sym, &p, sizeof(sym));
  return sym;
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
