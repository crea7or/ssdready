#pragma once

#include <afxwin.h>

#include <format>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

template <>
struct std::formatter<CString, wchar_t> : std::formatter<std::wstring_view, wchar_t> {
  auto format(const CString& value, std::wformat_context& context) const {
    return std::formatter<std::wstring_view, wchar_t>::format(
        std::wstring_view(value.GetString(), static_cast<std::size_t>(value.GetLength())), context);
  }
};

class AppLog {
 public:

  static AppLog& Get();
  static constexpr UINT kLogUpdatedMessage = WM_APP + 20;
  void Add(LPCTSTR message);

  // A single message stays literal; calls with arguments use checked {} formatting.
  template <typename... Args>
    requires(sizeof...(Args) > 0)
  void Add(std::wformat_string<Args...> format, Args&&... args) {
    const auto message = std::format(format, std::forward<Args>(args)...);
    Add(message.c_str());
  }

  // Attach/detach on the UI thread before destroying the viewer window.
  void SetViewer(HWND viewer);
  void CopyLog(CStringArray& messages, INT_PTR first = 0);

 private:

  AppLog() = default;
  ~AppLog() = default;
  std::vector<CString> messages_;
  std::mutex mutex_;
  HWND viewer_ = nullptr;
  bool update_posted_ = false;
};
