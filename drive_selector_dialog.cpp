// drive_selector_dialog.cpp : implementation file
//

// clang-format off
#include "pch.h"
// clang-format on
#include "drive_selector_dialog.h"

#include "afxdialogex.h"
#include "ssd_ready_app.h"

// DriveSelectorDialog dialog

DriveSelectorDialog::DriveSelectorDialog(CWnd* parent /*=nullptr*/) : DialogBase(DriveSelectorDialog::kDialogId, parent) {
}

void DriveSelectorDialog::DoDataExchange(CDataExchange* exchange) {
  DialogBase::DoDataExchange(exchange);
  DDX_Control(exchange, IDC_LIST_MONITORING_DRIVES, list_drives_);
  DDX_Control(exchange, IDOK, ok_);
}

BEGIN_MESSAGE_MAP(DriveSelectorDialog, DialogBase)
ON_BN_CLICKED(IDOK, &DriveSelectorDialog::OnBnClickedOk)
END_MESSAGE_MAP()

BOOL DriveSelectorDialog::OnInitDialog() {
  DialogBase::OnInitDialog();

  CRect list_rect;
  list_drives_.GetWindowRect(&list_rect);
  int list_ctrl_size = list_rect.Width() - (GetSystemMetrics(SM_CXEDGE) * 2);

  list_drives_.SetBkColor(Core()->GetWindowBk());
  list_drives_.SetTextColor(Core()->GetTextColor());

  list_drives_.InsertColumn(0, _T( "" ), LVCFMT_LEFT, list_ctrl_size);
  volumes_ = GetVolumes();
  if (volumes_) {
    for (const auto& drive : *volumes_) {
      list_drives_.AddDrive(drive, drive.instances > 0);
    }
  }

  list_drives_.AttachList();

  list_drives_.SetColumnsCallback(&SetColumns);

  list_drives_.AllowAutoResize(TRUE);
  list_drives_.CheckColumnsWidth();

  return TRUE;
}

void DriveSelectorDialog::SetColumns(SortableListCtrl* list_ctrl) {
  CRect list_rect;
  list_ctrl->GetClientRect(&list_rect);
  int list_ctrl_size = list_rect.Width();  // - ( GetSystemMetrics( SM_CXEDGE ) * 2 );
  list_ctrl->SetColumnWidth(0, list_ctrl_size);
}

void DriveSelectorDialog::OnBnClickedOk() {
  if (volumes_) {
    auto* manager = Core()->GetManager();
    auto lock = manager->LockDrives();
    for (auto& drive : *volumes_) {
      const int row = list_drives_.FindByData(reinterpret_cast<DWORD_PTR>(&drive));
      if (row == -1)
        continue;
      if (list_drives_.GetItemState(row, LVIS_SELECTED) & LVIS_SELECTED) {
        if (drive.instances == 0)
          manager->AttachDeviceToFilter(drive);
      } else if (drive.instances > 0) {
        manager->DetachDeviceFromFilter(drive);
      }
    }
    // Keep this dialog's entries alive through the final paint/destruction.
    manager->UpdateVolumes(std::make_unique<DriveInfoList>(*volumes_));
  }
  DialogBase::OnOK();
}
