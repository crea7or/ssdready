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

void AppLog::CopyLog(CStringArray& messages, INT_PTR first) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto start = first < 0 ? 0 : (std::min)(static_cast<std::size_t>(first), messages_.size());
  messages.SetSize(static_cast<INT_PTR>(messages_.size() - start));
  for (std::size_t index = start; index < messages_.size(); ++index)
    messages[static_cast<INT_PTR>(index - start)] = messages_[index];
  update_posted_ = false;
}

void AppLog::Add(LPCTSTR text) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  const auto formatted =
      std::format(L"{:04}.{:02}.{:02} {:02}:{:02}:{:02} {}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, text);
  CString message(formatted.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  messages_.push_back(std::move(message));
  // Keep posting under the mutex so detaching the window cannot race this post.
  if (viewer_ && !update_posted_)
    update_posted_ = ::PostMessage(viewer_, kLogUpdatedMessage, 0, 0) != FALSE;
}
