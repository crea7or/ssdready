// main_dialog.cpp : implementation file
//
// clang-format off
#include "pch.h"
// clang-format on
#include "main_dialog.h"

#include "about_dialog.h"
#include "drive_selector_dialog.h"
#include "log_viewer_dialog.h"
#include "options_dialog.h"
#include "processes_dialog.h"
#include "ssd_ready_app.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include "ui_statistics.h"

namespace {
constexpr std::uint64_t kEnduranceCycles = 600;
}

MainDialog::MainDialog(CWnd* parent /*=nullptr*/) : ResizableDialog(MainDialog::kDialogId, parent) {
  // Note that LoadIcon does not require a subsequent DestroyIcon in Win32

  tree_update_counter_ = 0;
  main_window_ = TRUE;
}

#pragma region DataExchange and Message Map

void MainDialog::DoDataExchange(CDataExchange* exchange) {
  ResizableDialog::DoDataExchange(exchange);
  DDX_Control(exchange, IDC_TREE_FILES, tree_ctrl_);
  DDX_Control(exchange, IDC_BUTTON_STARTSTOP, start_stop_);
  DDX_Control(exchange, IDC_BUTTON_ESIMATE, estimate_);
  DDX_Control(exchange, IDC_BUTTON_BUY, buy_);
  DDX_Control(exchange, IDC_BUTTON_OPTIONS, options_);
  DDX_Control(exchange, IDC_BUTTON_PROCESSES, processes_);
  DDX_Control(exchange, IDC_BUTTON_MENU, menu_);
  DDX_Control(exchange, IDC_COMBO_SIZES, combo_size_);
}

BEGIN_MESSAGE_MAP(MainDialog, ResizableDialog)
ON_WM_DESTROY()
ON_BN_CLICKED(IDC_BUTTON_STARTSTOP, OnButtonStartStop)
ON_WM_TIMER()
ON_BN_CLICKED(IDC_BUTTON_BUY, OnBnClickedButtonBuy)
ON_BN_CLICKED(IDC_BUTTON_ESIMATE, OnBnClickedButtonEstimate)
ON_BN_CLICKED(IDOK, OnOK)
ON_BN_CLICKED(IDC_BUTTON_OPTIONS, OnBnClickedButtonOptions)
ON_NOTIFY(NM_DBLCLK, IDC_TREE_FILES, &MainDialog::OnTreeProcessesCheck)
ON_BN_CLICKED(IDC_BUTTON_PROCESSES, &MainDialog::OnBnClickedButtonProcesses)
ON_WM_QUERYENDSESSION()
ON_NOTIFY(NM_RCLICK, IDC_TREE_FILES, &MainDialog::OnTreeRightClick)
ON_BN_CLICKED(IDC_BUTTON_MENU, &MainDialog::OnBnClickedButtonMenu)
ON_CBN_SELCHANGE(IDC_COMBO_SIZES, &MainDialog::OnCbnSelchangeComboSizes)
END_MESSAGE_MAP()

#pragma endregion

BOOL MainDialog::OnInitDialog() {
  ResizableDialog::OnInitDialog();

  tree_ctrl_.SetBkColor(COLORREF(RGB(0x3d, 0x3d, 0x40)));
  tree_ctrl_.SetTextColor(COLORREF(RGB(0xFF, 0xFF, 0xF0)));
  tree_ctrl_.SetLineColor(COLORREF(RGB(0x3d, 0x3d, 0x40)));

  start_stop_.SetIcon(IDI_ICON_START);
  estimate_.SetIcon(IDI_ICON_MEASURE);
  buy_.SetIcon(IDI_ICON_BUY);
  options_.SetIcon(IDI_ICON_SETTINGS);
  processes_.SetIcon(IDI_ICON_PROCESSES);
  menu_.SetIcon(IDI_ICON_MENU);

  Tray(NIM_ADD, Core()->GetMainIcon(), L"SsdReady");

#pragma region Sizes Combobox

  combo_size_.ResetContent();
  for (auto& drive_size : drive_sizes_) {
    if (!combo_size_.AddOption(&drive_size))
      AfxThrowMemoryException();
  }
#pragma endregion

  Core()->SetMainWindow(GetSafeHwnd());
  Core()->SetTreeControl(&tree_ctrl_);

  if (Core()->GetManager()->Install())
    Core()->GetManager()->AttachTo(Core()->GetInterfaceSettings()->selected_drives_);

#pragma region Detect first drive size

  std::size_t selected_size = 0;
  const auto volumes = GetVolumes();
  if (volumes && !volumes->empty() && volumes->front().volume_size > 0) {
    const auto actual_size = volumes->front().volume_size;
    selected_size = drive_sizes_.size() - 1;
    for (std::size_t index = 0; index < drive_sizes_.size(); ++index) {
      if (drive_sizes_[index].GetSize() >= actual_size) {
        selected_size = index;
        break;
      }
    }
  }
  auto* settings = Core()->GetInterfaceSettings();
  if (settings->selected_disk_size_ >= 0 && static_cast<std::size_t>(settings->selected_disk_size_) < drive_sizes_.size())
    selected_size = static_cast<std::size_t>(settings->selected_disk_size_);
  settings->selected_disk_size_ = static_cast<int>(selected_size);
  combo_size_.SetCurSel(static_cast<int>(selected_size));

#pragma endregion

  estimated_endurance_bytes_ = drive_sizes_[selected_size].GetSize();
  estimated_endurance_bytes_ *= kEnduranceCycles;

  timer_ = SetTimer(0, 1000, nullptr);

  if (Core()->GetInterfaceSettings()->start_minimized_ == FALSE) {
    RestoreWindow();
  }

  if (Core()->GetInterfaceSettings()->autostart_session_) {
    OnButtonStartStop();
  }

  Core()->SetStartingUp(FALSE);

  BeginLayout();
  RecalculateToolbar();
  EndLayout();

  CWnd::EnableToolTips();

  return TRUE;
}

void MainDialog::OnOK() {
  // WTF? Close on ENTER
}

void MainDialog::OnCancel() {
  // WTF? Close on ESC
  ResizableDialog::OnCancel();
}

void MainDialog::OnDestroy() {
  CloseApp();
  combo_size_.ResetContent();
  ResizableDialog::OnDestroy();
}

void MainDialog::CloseApp() {
  KillTimer(timer_);

  {
    auto lock = Core()->GetManager()->LockDrives();
    const auto volumes = GetVolumes();
    if (volumes) {
      Core()->GetInterfaceSettings()->selected_drives_.Empty();
      for (auto& drive : *volumes) {
        if (drive.instances > 0) {
          if (drive.HasDriveLetter())
            Core()->GetInterfaceSettings()->selected_drives_ += drive.drive_letter.data();
          Core()->GetManager()->DetachDeviceFromFilter(drive);
        }
      }
    }
  }

  Core()->SetTreeControl(nullptr);
  Core()->SetMainWindow(nullptr);
  Tray(NIM_DELETE, Core()->GetMainIcon(), L"SSD Ready");
  Core()->GetManager()->Uninstall();
}

BOOL MainDialog::PreTranslateMessage(MSG* message) {
  if (message->message == Core()->GetGlobalMessage()) {
    RestoreWindow();
    return TRUE;
  }
  return ResizableDialog::PreTranslateMessage(message);
}

void MainDialog::OnButtonStartStop() {
  auto* manager = Core()->GetManager();
  if (manager->GetLogStatus() && !manager->GetCleanUpStatus()) {
    manager->SetLogStatus(FALSE);
    SetDlgItemText(IDC_BUTTON_STARTSTOP, L"Start");
    start_stop_.SetIcon(IDI_ICON_START);
  } else {
    if (manager->GetCleanUpStatus()) {
      if (!manager->Install()) {
        AfxMessageBox(L"Monitoring could not start. See the application log for the driver error.", MB_OK | MB_ICONERROR);
        return;
      }
      manager->AttachTo(Core()->GetInterfaceSettings()->selected_drives_);
    }
    manager->SetLogStatus(TRUE);
    SetDlgItemText(IDC_BUTTON_STARTSTOP, L"Stop");
    start_stop_.SetIcon(IDI_ICON_STOP);
  }
}

void MainDialog::OnTimer(UINT_PTR timer_id) {
  const auto statistics = Core()->GetManager()->GetStatistics();
  const auto monitored_seconds = (std::max)(statistics.monitored_seconds, std::uint64_t(1));
  const DWORD64 written_bytes = statistics.written_bytes;
  const DWORD64 rounded_bytes = statistics.rounded_written_bytes;
  DWORD64 write_per, write_per_day;

  CString formatted_value;
  formatted_value = string_utils::FormatBytes(written_bytes);
  SetDlgItemText(IDC_STATIC_TOTAL_WRITTEN, formatted_value);

  formatted_value = string_utils::FormatBytes(rounded_bytes);
  SetDlgItemText(IDC_STATIC_TOTAL_WRITTEN_WORST, formatted_value);
  formatted_value.Format(L"%I64u", statistics.operations);
  SetDlgItemText(IDC_STATIC_TOTAL_OPERATIONS, formatted_value);
  formatted_value.Format(L"%I64u", statistics.items);
  SetDlgItemText(IDC_STATIC_TOTAL_FILES, formatted_value);

  CTimeSpan time_span{static_cast<time_t>(monitored_seconds)};
  formatted_value = time_span.Format(L"%D %H:%M:%S");
  SetDlgItemText(IDC_STATIC_MONITORING_TIME, formatted_value);

  // day values
  write_per_day = ui_statistics::DailyBytes(written_bytes, monitored_seconds);
  formatted_value = string_utils::FormatBytes(write_per_day);
  SetDlgItemText(IDC_STATIC_WRITES_DAY, formatted_value);

  write_per = ui_statistics::DailyBytes(rounded_bytes, monitored_seconds);
  formatted_value = string_utils::FormatBytes(write_per);
  SetDlgItemText(IDC_STATIC_WRITES_DAYW, formatted_value);

  write_per = statistics.read_bytes;
  formatted_value = string_utils::FormatBytes(write_per);
  SetDlgItemText(IDC_STATIC_TOTAL_READ, formatted_value);

  double write = double(DWORD64(written_bytes));  /// ( 1024 * 1024 )));
  double read = double(DWORD64(write_per));       // / ( 1024 * 1024 )));

  if (read > 0) {
    formatted_value.Format(L"%.04g", (write / read));
  } else {
    formatted_value = L"0";
  }
  SetDlgItemText(IDC_STATIC_WRITE_READ_RAITO, formatted_value);

  if ((write_per_day > 0) && (monitored_seconds > 60 * 5)) {
    write_per = estimated_endurance_bytes_ / write_per_day;

    double estimated_time;
    if (write_per < 365)  // year
    {
      if (write_per < 30) {
        formatted_value.Format(L"%d days", (DWORD)write_per);
      } else {
        estimated_time = double(write_per) / 30.4375;
        formatted_value.Format(L"%.02g months", estimated_time);
      }
    } else {
      estimated_time = double(write_per) / 365;
      formatted_value.Format(L"%.02g years", estimated_time);
    }
  } else {
    formatted_value = L"Calculating";
  }
  SetDlgItemText(IDC_STATIC_EST, formatted_value);

  if (Core()->GetManager()->GetCleanUpStatus()) {
    formatted_value = L"Monitoring unavailable; see log";
    SetDlgItemText(IDC_BUTTON_STARTSTOP, L"Start");
    start_stop_.SetIcon(IDI_ICON_START);
  } else if (Core()->GetManager()->GetLogStatus()) {
    formatted_value = L"Monitoring in progress...";
  } else {
    formatted_value = L"Awaiting";
  }
  SetDlgItemText(IDC_STATIC_LEFT_STATUSBAR, formatted_value);

  formatted_value.Format(L"%.0f items/s", Core()->GetManager()->GetProcessedItemsPerSecond());
  SetDlgItemText(IDC_STATIC_RIGHT_STATUSBAR, formatted_value);
  formatted_value.Format(L"%.0f items/s", Core()->GetManager()->GetMaxProcessedItemsPerSecond());
  SetDlgItemText(IDC_STATIC_MAX_PROCESSING_RATE, formatted_value);

  if (tree_update_counter_ == 0) {
    tree_update_counter_ = 3;
    if (Core()->GetManager()->GetLogStatus()) {
      tree_ctrl_.Invalidate(FALSE);
    }
  } else {
    tree_update_counter_--;
  }

  ResizableDialog::OnTimer(timer_id);
}

void MainDialog::RecalculateLayout() {
  CRect rect_ctrl;
  int dx, dy;
  GetNewOffset(dx, dy);

  MoveSizeSize(IDC_TREE_FILES, dx, dy);
  MoveSizeTopWidth(IDC_STATIC_LEFT_STATUSBAR, dy, dx);
  MoveSizePos(IDC_STATIC_PROCESSING_RATE_LABEL, dx, dy);
  MoveSizePos(IDC_STATIC_RIGHT_STATUSBAR, dx, dy);
  MoveSizePos(IDC_STATIC_MAX_PROCESSING_RATE_LABEL, dx, dy);
  MoveSizePos(IDC_STATIC_MAX_PROCESSING_RATE, dx, dy);
  MoveSizeWidth(IDC_STATIC_DIVIDER, dx);

  if (dx != 0) {
    RecalculateToolbar();
  }
}

void MainDialog::RecalculateToolbar() {
  CRect client_rect, first_button_rect;

  GetClientRect(client_rect);

  start_stop_.GetWindowRect(first_button_rect);
  ScreenToClient(first_button_rect);
  int intButtonStep = (client_rect.Width() - first_button_rect.left) / 6;

  int dx = intButtonStep;
  int button_width = intButtonStep - first_button_rect.left;

  MoveControlLeftTo(IDC_BUTTON_STARTSTOP, first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_BUTTON_ESIMATE, dx + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_BUTTON_PROCESSES, (dx * 2) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_BUTTON_OPTIONS, (dx * 3) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_BUTTON_BUY, (dx * 4) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_BUTTON_MENU, (dx * 5) + first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_00_00, first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_00_01, first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_00_02, first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_00_03, first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_TOTAL_WRITTEN, dx + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_TOTAL_WRITTEN_WORST, dx + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_TOTAL_READ, dx + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_COMBO_SIZES, dx + first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_01_00, (dx * 2) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_01_01, (dx * 2) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_01_02, (dx * 2) + first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_WRITES_DAY, (dx * 3) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_WRITES_DAYW, (dx * 3) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_WRITE_READ_RAITO, (dx * 3) + first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_02_00, (dx * 4) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_02_01, (dx * 4) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_02_02, (dx * 4) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_02_03, (dx * 4) + first_button_rect.left, button_width);

  MoveControlLeftTo(IDC_STATIC_TOTAL_OPERATIONS, (dx * 5) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_TOTAL_FILES, (dx * 5) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_MONITORING_TIME, (dx * 5) + first_button_rect.left, button_width);
  MoveControlLeftTo(IDC_STATIC_EST, (dx * 5) + first_button_rect.left, button_width);
}

void MainDialog::MoveControlLeftTo(UINT id, int x, int width) {
  CWnd* control = GetDlgItem(id);
  if (!control)
    return;
  CRect rect;
  control->GetWindowRect(rect);
  ScreenToClient(rect);
  PositionControl(*control, x, rect.top, (std::max)(0, width), rect.Height());
}

void MainDialog::OnBnClickedButtonClearData() {
  Core()->GetManager()->ClearData();
}

void MainDialog::OnBnClickedButtonHelp() {
  ShellExecute(nullptr, L"open", L"http://www.ssdready.com/help/?inref=ssdready", nullptr, nullptr, SW_SHOWNORMAL);
}

void MainDialog::OnBnClickedButtonBuy() {
  AboutDialog dialog;
  dialog.DoModal();
}

void MainDialog::OnBnClickedButtonEstimate() {
  CString written;
  // IDC_STATIC_WRITES_DAY
  // IDC_STATIC_TOTAL_WRITTEN

  GetDlgItemText(IDC_STATIC_WRITES_DAYW, written);

  written.Replace(L" ", L"&msr=");
  written = L"http://www.ssdready.com/measure/?inref=ssdready&value=" + written;

  ShellExecute(nullptr, L"open", written, nullptr, nullptr, SW_SHOWNORMAL);
}

void MainDialog::OnBnClickedButtonOptions() {
  OptionsDialog dialog;
  dialog.DoModal();
}

void MainDialog::OnTreeProcessesCheck(NMHDR*, LRESULT* result) {
  *result = 0;
  CPoint point(GetMessagePos());
  tree_ctrl_.ScreenToClient(&point);
  TreeItem* tree_item = tree_ctrl_.GetModelItemAt(point);
  if (!tree_item)
    return;
  // Opening the dialog consumes activation; do not also toggle this folder.
  *result = 1;
  ProcessesDialog dialog(tree_item, TRUE);
  dialog.DoModal();
}

void MainDialog::OnBnClickedButtonProcesses() {
  ProcessesDialog dlg(nullptr, TRUE);
  dlg.DoModal();
}

BOOL MainDialog::OnQueryEndSession() {
  Core()->GetManager()->SaveMonitoredItems();

  if (!ResizableDialog::OnQueryEndSession())
    return FALSE;

  return TRUE;
}

void MainDialog::OnTreeRightClick(NMHDR*, LRESULT* result) {
  TreeItem* tree_item = tree_ctrl_.GetSelectedModelItem();
  if (tree_item != nullptr) {
    CPoint point;
    GetCursorPos(&point);

    // Process menu
    CMenu menu;
    menu.CreatePopupMenu();
    int selected;

    menu.AppendMenu(MF_STRING, 1, L"Writes to this item only");
    menu.AppendMenu(MF_STRING, 2, L"This item and its children");
    menu.AppendMenu(MF_STRING, 3, L"Open containing folder");
    SetForegroundWindow();
    selected =
        TrackPopupMenuEx(menu.GetSafeHmenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, point.x, point.y, GetSafeHwnd(), nullptr);
    if (selected) {
      switch (selected) {
        case 1: {
          ProcessesDialog dlg(tree_item, FALSE);
          dlg.DoModal();
        } break;
        case 2: {
          ProcessesDialog dlg(tree_item, TRUE);
          dlg.DoModal();
        } break;
        case 3: {
          CString path;
          if (tree_item->GetParent() != nullptr) {
            tree_item->GetParent()->GetFullPath(&path);
            if (path.GetLength() < 1) {
              tree_item->GetFullPath(&path);
            }
          }
          ShellExecute(nullptr, L"explore", path, nullptr, nullptr, SW_NORMAL);
        } break;
      }
    }
  }
  *result = 0;
}

void MainDialog::OnTrayRightClick() {
  CMenu menu;
  menu.LoadMenu(IDR_MENU_SYSTEM_TRAY);
  CMenu* popup;
  popup = menu.GetSubMenu(0);
  if (!popup)
    return;

  POINT cursor_position;
  GetCursorPos(&cursor_position);
  SetForegroundWindow();
  int selected = TrackPopupMenuEx(
      popup->GetSafeHmenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, cursor_position.x, cursor_position.y, GetSafeHwnd(), nullptr);
  if (selected) {
    switch (selected) {
      case ID_CONTEXTMENU_SHOWSSDREADYWINDOW:
        RestoreWindow();
        break;
      case ID_CONTEXTMENU_ABOUTSSDREADY:
        OnBnClickedButtonBuy();
        break;
      case ID_CONTEXTMENU_EXIT:
        OnCancel();
        break;
    }
  }
}

void MainDialog::OnBnClickedButtonMenu() {
  CMenu menu;
  menu.LoadMenuW(IDR_MENU_MAIN);
  CMenu* popup;
  popup = menu.GetSubMenu(0);
  if (!popup)
    return;

  CRect button_rect;
  menu_.GetWindowRect(button_rect);

  SetForegroundWindow();
  int selected = TrackPopupMenuEx(popup->GetSafeHmenu(),
                                  TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                  button_rect.left,
                                  button_rect.top + button_rect.Height(),
                                  GetSafeHwnd(),
                                  nullptr);
  if (selected) {
    switch (selected) {
      case IDM_MENU_SELECT_DRIVES: {
        DriveSelectorDialog dlg;
        dlg.DoModal();
      } break;
      case IDM_MENU_SHOW_LOG: {
        LogViewerDialog dlg;
        dlg.DoModal();
      } break;
      case IDM_MENU_CLEARCOLLECTEDDATA: {
        if (AfxMessageBox(L"Are you sure that you want to clear all collected data?", MB_YESNO) == IDYES) {
          OnBnClickedButtonClearData();
        }
      } break;
      case IDM_MENU_HELP: {
        OnBnClickedButtonHelp();
      } break;
    }
  }
}

void MainDialog::OnCbnSelchangeComboSizes() {
  int selected = combo_size_.GetCurSel();
  if (selected < 0 || static_cast<std::size_t>(selected) >= drive_sizes_.size())
    return;
  Core()->GetInterfaceSettings()->selected_disk_size_ = selected;
  estimated_endurance_bytes_ = drive_sizes_[static_cast<std::size_t>(selected)].GetSize();
  estimated_endurance_bytes_ *= kEnduranceCycles;
}

INT_PTR MainDialog::OnToolHitTest(CPoint point, TOOLINFO* tool_info) const {
  INT_PTR i_hit = CWnd::OnToolHitTest(point, tool_info);
  CString tooltip;
  if (tool_info != nullptr) {
    tool_info->lpszText = nullptr;
    switch (i_hit) {
      case IDC_STATIC_EST:
      case IDC_STATIC_02_03:
        tooltip = L"SSD life based on aligned writes per day, selected size and average life of TLC drive";
        break;
      case IDC_COMBO_SIZES:
      case IDC_STATIC_00_03:
        tooltip = L"Size of your SSD (current or planned) - SSD life depends on it";
        break;
      case IDC_STATIC_00_01:
      case IDC_STATIC_TOTAL_WRITTEN_WORST:
        tooltip = L"Amount of written data aligned to 4096 bytes (the minimum possible for SSD)";
        break;
      case IDC_STATIC_01_01:
      case IDC_STATIC_WRITES_DAYW:
        tooltip = L"Amount of written data aligned to 4096 bytes (the minimum possible for SSD) per day";
        break;
      case IDC_STATIC_00_00:
      case IDC_STATIC_TOTAL_WRITTEN:
        tooltip = L"Amount of written data by system to selected drives";
        break;
      case IDC_STATIC_00_02:
      case IDC_STATIC_TOTAL_READ:
        tooltip = L"Amount of read data by system from selected drives";
        break;
      case IDC_STATIC_01_00:
      case IDC_STATIC_WRITES_DAY:
        tooltip = L"Amount of written data by system to selected drives per day";
        break;
      case IDC_STATIC_01_02:
      case IDC_STATIC_WRITE_READ_RAITO:
        tooltip = L"Written data divided by read data";
        break;
      case IDC_STATIC_02_00:
      case IDC_STATIC_TOTAL_OPERATIONS:
        tooltip = L"Total number of write operations";
        break;
      case IDC_STATIC_02_01:
      case IDC_STATIC_TOTAL_FILES:
        tooltip = L"Total number of unique files that had some writes";
        break;
      case IDC_STATIC_02_02:
      case IDC_STATIC_MONITORING_TIME:
        tooltip = L"The time that SsdReady monitors the drives (should be at least 8-12 hours for valid data)";
        break;
    }

    if (tooltip.GetLength() > 0) {
      tool_info->lpszText = LPWSTR(malloc((tooltip.GetLength() * sizeof(TCHAR)) + 2));
      if (tool_info->lpszText)
        wcscpy(tool_info->lpszText, LPCWSTR(tooltip));
    }
  }
  return i_hit;
}
