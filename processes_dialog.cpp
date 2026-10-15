// processes_dialog.cpp : implementation file
//

// clang-format off
#include "pch.h"
// clang-format on
#include "processes_dialog.h"

#include "ssd_ready_app.h"

// ProcessesDialog dialog

ProcessesDialog::ProcessesDialog(TreeItem* tree_item, BOOL all, CWnd* parent /*=nullptr*/)
    : DialogBase(ProcessesDialog::kDialogId, parent) {
  tree_item_ = tree_item;
  all_ = all;
}

void ProcessesDialog::DoDataExchange(CDataExchange* exchange) {
  DialogBase::DoDataExchange(exchange);
  DDX_Control(exchange, IDC_LIST_PROCESS, list_);
  DDX_Control(exchange, IDOK, ok_);
}

BEGIN_MESSAGE_MAP(ProcessesDialog, DialogBase)
END_MESSAGE_MAP()

// ProcessesDialog message handlers
BOOL ProcessesDialog::OnInitDialog() {
  DialogBase::OnInitDialog();

  CRect rect;
  list_.GetClientRect(rect);
  const int written_width = rect.Width() / 4;
  list_.InsertColumn(0, L"Process Name", LVCFMT_LEFT, rect.Width() - written_width);
  list_.InsertColumn(1, L"Written", LVCFMT_LEFT, written_width);

  list_.SetBkColor(Core()->GetControlBk());
  list_.SetTextColor(Core()->GetTextColor());
  list_.SetTextBkColor(Core()->GetControlBk());

  list_.ApplySettings();

  if (tree_item_ == nullptr)
    all_ = TRUE;
  auto snapshot = Core()->GetManager()->GetProcessStatistics(tree_item_, all_ != FALSE);
  list_.SetProcesses(std::move(snapshot.processes));

  list_.SetColumnsCallback(&SetColumns);

  list_.AllowAutoResize(TRUE);
  list_.CheckColumnsWidth();
  list_.UpdateWindow();

  const auto written = string_utils::FormatBytes(snapshot.written_bytes);
  CString summary;
  summary.Format(all_ ? L"Item: %s and its subitems, data written: %s" : L"Item name: %s, data written: %s",
                 snapshot.item_name.GetString(),
                 written.GetString());
  SetDlgItemText(IDC_STATIC_ITEM, summary);

  return TRUE;
}

void ProcessesDialog::SetColumns(SortableListCtrl* list_ctrl) {
  CRect list_rect;
  list_ctrl->GetClientRect(&list_rect);
  int list_ctrl_size = list_rect.Width();  // - ( GetSystemMetrics( SM_CXEDGE ) * 2 );

  int width1_5 = (list_ctrl_size / 4);

  list_ctrl->SetColumnWidth(0, list_ctrl_size - width1_5);
  list_ctrl->SetColumnWidth(1, width1_5);
}
