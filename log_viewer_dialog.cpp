// clang-format off
#include "pch.h"
// clang-format on
#include "log_viewer_dialog.h"

#include "app_log.h"

// LogViewerDialog dialog

LogViewerDialog::LogViewerDialog(CWnd* parent) : ResizableDialog(kDialogId, parent) {
}

void LogViewerDialog::DoDataExchange(CDataExchange* exchange) {
  ResizableDialog::DoDataExchange(exchange);
  DDX_Control(exchange, IDC_LIST_LOG, list_box_);
  DDX_Control(exchange, IDOK, ok_);
}

BEGIN_MESSAGE_MAP(LogViewerDialog, ResizableDialog)
ON_WM_DESTROY()
ON_MESSAGE(AppLog::kLogUpdatedMessage, &LogViewerDialog::OnLogUpdated)
END_MESSAGE_MAP()

// LogViewerDialog message handlers


BOOL LogViewerDialog::OnInitDialog() {
  CRect client;
  GetClientRect(client);
  client_size_ = client.Size();
  ResizableDialog::OnInitDialog();
  displayed_count_ = 0;
  Core()->GetLog()->SetViewer(GetSafeHwnd());
  RefreshLog();
  return TRUE;
}

void LogViewerDialog::OnDestroy() {
  Core()->GetLog()->SetViewer(nullptr);
  ResizableDialog::OnDestroy();
}

void LogViewerDialog::RecalculateLayout() {
  CRect client, button;
  GetClientRect(client);
  const int dx = client.Width() - client_size_.cx;
  const int dy = client.Height() - client_size_.cy;
  client_size_ = client.Size();
  MoveSizeSize(IDC_LIST_LOG, dx, dy);
  MoveSizeWidth(IDC_STATIC_LOG_LABEL, dx);
  ok_.GetWindowRect(button);
  ScreenToClient(button);
  PositionControl(ok_, (client.Width() - button.Width()) / 2, button.top + dy, button.Width(), button.Height());
}

void LogViewerDialog::RefreshLog() {
  CStringArray messages;
  INT_PTR retained_first = 0;
  Core()->GetLog()->CopyLog(messages, displayed_count_, &retained_first);
  if (retained_first != displayed_first_) {
    Core()->GetLog()->CopyLog(messages, 0, &retained_first);
    list_box_.ResetContent();
    displayed_first_ = displayed_count_ = retained_first;
  }
  // CopyLog releases its mutex before any calls into the window controls.
  if (messages.IsEmpty())
    return;
  CRect client, last_item;
  list_box_.GetClientRect(client);
  const int old_count = list_box_.GetCount();
  const bool follow_tail =
      old_count == 0 || (list_box_.GetItemRect(old_count - 1, &last_item) != LB_ERR && last_item.bottom <= client.bottom);
  list_box_.SetRedraw(FALSE);
  for (INT_PTR index = 0; index < messages.GetSize(); ++index) {
    if (list_box_.AddString(messages.GetAt(index)) < 0)
      break;
    ++displayed_count_;
  }
  if (follow_tail && list_box_.GetCount() > 0) {
    const int height = (std::max)(1, list_box_.GetItemHeight(0));
    const int visible_rows = (std::max)(1, client.Height() / height);
    list_box_.SetTopIndex((std::max)(0, list_box_.GetCount() - visible_rows));
  }
  list_box_.SetRedraw(TRUE);
  list_box_.Invalidate(FALSE);
}

LRESULT LogViewerDialog::OnLogUpdated(WPARAM, LPARAM) {
  RefreshLog();
  return 0;
}
