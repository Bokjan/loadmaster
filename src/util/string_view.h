#pragma once

#include <cstring>

#include <string>

namespace util {

class string_view final {
 private:
  using size_type = std::string::size_type;

 public:
  constexpr string_view() noexcept : data_(nullptr), size_(0) {}
  constexpr string_view(const char *data, size_type size) noexcept : data_(data), size_(size) {}
  string_view(const char *data) : data_(data), size_(strlen(data)) {}
  string_view(const std::string &s) : data_(s.data()), size_(s.size()) {}
  constexpr const char *c_str() const noexcept { return data_; }
  constexpr const char *data() const noexcept { return data_; }
  constexpr size_type size() const noexcept { return size_; }

 private:
  const char *data_;
  size_type size_;
};

}  // namespace util
