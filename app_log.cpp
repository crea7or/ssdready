// clang-format off
#include "pch.h"
// clang-format on
#include "app_log.h"

#include <algorithm>

AppLog& AppLog::Get() {
  static AppLog instance;
  return instance;
}

void AppLog::SetViewer(HWND viewer) {
  std::lock_guard<std::mutex> lock(mutex_);
  viewer_ = viewer;
  update_posted_ = false;
}

INT_PTR AppLog::CopyLog(CStringArray& messages, INT_PTR first, INT_PTR* retained_first) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (retained_first)
    *retained_first = first_sequence_;
  const auto start = first < first_sequence_ ? 0 : (std::min)(static_cast<std::size_t>(first - first_sequence_), messages_.size());
  messages.SetSize(static_cast<INT_PTR>(messages_.size() - start));
  for (std::size_t index = start; index < messages_.size(); ++index)
    messages[static_cast<INT_PTR>(index - start)] = messages_[index];
  update_posted_ = false;
  return first_sequence_ + static_cast<INT_PTR>(messages_.size());
}

void AppLog::Add(LPCTSTR text) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  const auto formatted = std::format(L"{:04}.{:02}.{:02} {:02}:{:02}:{:02} {}",
                                     now.wYear,
                                     now.wMonth,
                                     now.wDay,
                                     now.wHour,
                                     now.wMinute,
                                     now.wSecond,
                                     std::wstring_view(text ? text : L"").substr(0, 4096));
  CString message(formatted.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  messages_.push_back(std::move(message));
  if (messages_.size() > kMaxMessages) {
    messages_.pop_front();
    ++first_sequence_;
  }
  // Keep posting under the mutex so detaching the window cannot race this post.
  if (viewer_ && !update_posted_)
    update_posted_ = ::PostMessage(viewer_, kLogUpdatedMessage, 0, 0) != FALSE;
}
