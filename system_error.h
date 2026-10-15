#pragma once

#include <afxstr.h>

// Logs one Windows error and preserves its code across formatting and scope exit.
class SystemError {
 public:

  explicit SystemError(LPCWSTR context = nullptr, DWORD error = GetLastError());
  ~SystemError() noexcept;
  SystemError(const SystemError&) = delete;
  SystemError& operator=(const SystemError&) = delete;

 private:

  DWORD error_;
};
