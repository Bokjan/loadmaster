#include "version_string.h"

#include <string>
#include <string_view>

#include "core/version.h"

namespace cli {

const char *VersionString() {
  // Built once on first use; cheaper than std::ostringstream and the address
  // remains valid for the lifetime of the program.
  static const std::string kCached = [] {
    using namespace core::version;
    std::string s;
    s.reserve(64);
    s.append(kVersionProject);
    s.push_back(' ');
    s.append(std::to_string(kVersionMajor));
    s.push_back('.');
    s.append(std::to_string(kVersionMinor));
    s.push_back('.');
    s.append(std::to_string(kVersionPatch));
    std::string_view suffix(kVersionSuffix);
    if (!suffix.empty()) {
      s.push_back('-');
      s.append(suffix);
    }
    return s;
  }();
  return kCached.c_str();
}

}  // namespace cli
