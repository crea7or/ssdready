#pragma once
#include <cstdint>

// Labels are string literals owned for the lifetime of the application.
class DriveSizeOption {
 public:

  constexpr DriveSizeOption(const wchar_t* label, std::uint64_t gibibytes) : label_(label), bytes_(gibibytes * (std::uint64_t{1} << 30)) {
  }

  const wchar_t* GetDisplaySize() const {
    return label_;
  }

  std::uint64_t GetSize() const {
    return bytes_;
  }

 private:

  const wchar_t* label_;
  std::uint64_t bytes_;
};
