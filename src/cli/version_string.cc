#include "version_string.h"

#include <sstream>

#include "core/version.h"

namespace version_impl {

class VersionStringGenerator final {
 public:
  static VersionStringGenerator &GetInstance() {
    static VersionStringGenerator instance;
    return instance;
  }
  const std::string &GetVersionString() { return version_string_; }

 private:
  std::string version_string_;
  VersionStringGenerator() {
    std::ostringstream oss;
    using namespace core::version;
    oss << kVersionProject << ' ';
    oss << kVersionMajor << '.' << kVersionMinor << '.' << kVersionPatch;
    if (kVersionSuffix[0] != '\0') {
      oss << '-' << kVersionSuffix;
    }
    version_string_.assign(std::move(oss.str()));
  }
};

}  // namespace version_impl

namespace cli {

const char *VersionString() {
  return version_impl::VersionStringGenerator::GetInstance().GetVersionString().c_str();
}

}  // namespace cli
