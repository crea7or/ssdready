// clang-format off
#include "pch.h"
// clang-format on
#include "system_error.h"

#include <memory>

#include "app_log.h"

SystemError::SystemError(LPCWSTR context, DWORD error) : error_(error) {
  struct RestoreError {
    DWORD error;

    ~RestoreError() {
      SetLastError(error);
    }
  } restore{error};

  wchar_t* text = nullptr;
  const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,
                                      error,
                                      0,
                                      reinterpret_cast<LPWSTR>(&text),
                                      0,
                                      nullptr);
  const std::unique_ptr<wchar_t, decltype(&LocalFree)> owner(text, &LocalFree);
  CString description;
  if (length) {
    description.SetString(text, static_cast<int>(length));
    description.Remove(L'\r');
    description.Remove(L'\n');
  } else {
    description = L"Unknown system error";
  }
  if (context && *context)
    AppLog::Get().Add(L"{} :: {} (Windows error {})", context, description, error);
  else
    AppLog::Get().Add(L"{} (Windows error {})", description, error);
}

SystemError::~SystemError() noexcept {
  SetLastError(error_);
}
