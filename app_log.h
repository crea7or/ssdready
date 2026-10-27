#pragma once

#include <afxwin.h>

#include <deque>
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
  static constexpr std::size_t kMaxMessages = 4096;
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
  INT_PTR CopyLog(CStringArray& messages, INT_PTR first = 0, INT_PTR* retained_first = nullptr);

 private:

  AppLog() = default;
  ~AppLog() = default;
  std::deque<CString> messages_;
  INT_PTR first_sequence_ = 0;
  std::mutex mutex_;
  HWND viewer_ = nullptr;
  bool update_posted_ = false;
};
