// clang-format off
#include "pch.h"
// clang-format on

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "buffered_device_context.h"
#include "dialog_base.h"
#include "drive_list_ctrl.h"
#include "drive_size_combo_box.h"
#include "file_tree_item.h"
#include "folder_tree_item.h"
#include "log_viewer_dialog.h"
#include "monitoring_tree_ctrl.h"
#include "resizable_dialog.h"
#include "styled_button.h"
#include "ui_statistics.h"

#pragma comment(linker,                                                                                          \
                "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
                "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
CWinApp application;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class CompletingDialog : public DialogBase {
 public:

  CompletingDialog() : DialogBase(IDD_ABOUTBOX) {
  }

  BOOL OnInitDialog() override {
    EndDialog(IDOK);
    return FALSE;
  }
};

class LayoutDialog : public ResizableDialog {
 public:

  LayoutDialog() : ResizableDialog(IDD_ABOUTBOX) {
  }

 protected:

  void RecalculateLayout() override {
    int dx = 0, dy = 0;
    GetNewOffset(dx, dy);
    MoveSizeControl(IDOK, dx, dy, 0, 0);
  }
};

void TestTree(CWnd& parent) {
  MonitoringTreeCtrl tree;
  Check(tree.Create(WS_CHILD | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT, CRect(0, 0, 300, 200), &parent, 201), "Create tree");
  FileTreeItem first, second;
  first.SetAlias(L"first");
  second.SetAlias(L"second");
  Check(tree.AddModelItem(&first) && tree.AddModelItem(&second), "Insert model items");
  tree.SelectItem(first.GetTreeItem());
  Check(tree.GetSelectedModelItem() == &first, "Programmatic/keyboard selection follows native caret");
  CRect second_label;
  Check(tree.GetItemRect(second.GetTreeItem(), second_label, TRUE), "Get unselected label bounds");
  Check(tree.GetModelItemAt(second_label.CenterPoint()) == &second, "Activation uses the clicked item rather than the selected item");
  Check(tree.GetModelItemAt(CPoint(second_label.right + 10, second_label.CenterPoint().y)) == nullptr,
        "Row whitespace does not activate the selected item");
  Check(tree.GetModelItemAt(CPoint(150, 190)) == nullptr, "Empty tree space does not activate an item");
  tree.SelectItem(second.GetTreeItem());
  Check(tree.GetSelectedModelItem() == &second, "Selection follows changed native caret");
  tree.DeleteItem(second.GetTreeItem());
  Check(second.GetTreeItem() == nullptr, "Delete notification clears borrowed model handle");
  Check(tree.GetSelectedModelItem() != &second, "Deleted selection is not retained");
  FolderTreeItem folder;
  folder.SetAlias(L"folder");
  first.AddWrite(std::uint64_t{1} << 63);
  Core()->GetInterfaceSettings()->sort_tree_by_write_size_ = true;
  Check(MonitoringTreeCtrl::CompareProc(reinterpret_cast<LPARAM>(&folder), reinterpret_cast<LPARAM>(&first), 0) < 0,
        "Folder priority holds above 2^63 bytes");
  Check(tree.GetExtendedStyle() & TVS_EX_DOUBLEBUFFER, "Tree enables native double buffering");
  FileTreeItem child;
  child.SetAlias(L"child");
  child.SetParent(&folder);
  Check(tree.AddModelItem(&folder) && tree.AddModelItem(&child), "Add folder with native child");
  TVITEM state{};
  state.mask = TVIF_CHILDREN;
  state.hItem = folder.GetTreeItem();
  state.cChildren = 1;
  Check(tree.SetItem(&state), "Enable folder expansion button");
  for (int repeat = 0; repeat < 3; ++repeat) {
    for (UINT action : {TVE_EXPAND, TVE_COLLAPSE}) {
      Check(tree.Expand(folder.GetTreeItem(), action), "Expand/collapse folder");
      CRect label;
      Check(tree.GetItemRect(folder.GetTreeItem(), label, TRUE), "Get folder label bounds");
      Check(tree.GetModelItemAt(label.CenterPoint()) == &folder, "Folder label activates in either expansion state");
      bool found_button = false;
      for (int x = 0; x < label.left; ++x) {
        const CPoint point(x, label.CenterPoint().y);
        UINT flags = 0;
        if (tree.HitTest(point, &flags) == folder.GetTreeItem() && (flags & TVHT_ONITEMBUTTON)) {
          Check(tree.GetModelItemAt(point) == nullptr, "Expand/collapse button never activates the process dialog");
          found_button = true;
          break;
        }
      }
      Check(found_button, "Test exercised a real native expansion button");
    }
  }
  tree.DeleteAllItems();
  tree.DestroyWindow();
}

void TestCombo(CWnd& parent) {
  DriveSizeComboBox combo;
  Check(combo.Create(WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE, CRect(0, 0, 120, 150), &parent, 202), "Create owner-drawn combo");
  std::array<DriveSizeOption, 3> sizes{{{L"32Gb", 32}, {L"1Tb", 1024}, {L"4Tb", 4096}}};
  for (auto& size : sizes)
    Check(combo.AddOption(&size), "Insert drive option");
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    Check(combo.GetItemDataPtr(static_cast<int>(index)) == &sizes[index], "Combo borrows stable value address");
    Check(combo.GetItemHeight(static_cast<int>(index)) > 0, "Measurement receives valid item data");
  }
  Check(!combo.AddOption(nullptr), "Null option rejected without adding a row");
  Check(static_cast<std::size_t>(combo.GetCount()) == sizes.size(), "Insertion count unchanged after failure");
  combo.ResetContent();
  Check(sizes.back().GetSize() == (std::uint64_t{4} << 40), "Clearing control does not delete borrowed values");
  combo.DestroyWindow();
}

void TestDriveList(CWnd& parent) {
  DriveListCtrl list;
  Check(list.Create(WS_CHILD | LVS_REPORT, CRect(0, 0, 200, 100), &parent, 204), "Create drive list");
  list.InsertColumn(0, L"Drive", LVCFMT_LEFT, 180);
  DriveInfo drive;
  drive.drive_letter[0] = L'C';
  Check(list.AddDrive(drive), "Add borrowed drive record");
  Check(list.GetItemText(0, 0) == L"C:", "Drive callbacks expose real text to native control");
  Check(list.GetItemData(0) == reinterpret_cast<DWORD_PTR>(&drive), "Drive row preserves full pointer width");
  list.DeleteAllItems();
  Check(drive.drive_letter[0] == L'C', "Removing rows preserves borrowed snapshot");
  list.DestroyWindow();
}

void TestIcons(CWnd& parent) {
  {
    StyledButton warm;
    warm.SetIcon(IDI_ICON_START);
    warm.SetIcon(IDI_ICON_STOP);
  }
  const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  for (int index = 0; index < 100; ++index) {
    StyledButton button;
    Check(button.Create(L"Test", WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 100, 80), &parent, 203), "Create button");
    button.SetFont(CFont::FromHandle(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
    button.SetIcon(IDI_ICON_START);
    button.SetIcon(IDI_ICON_STOP);
    button.SetIcon(IDI_ICON_STOP);
    button.SetIcon(65000);  // Missing resources take the null-icon path.
    button.SetIcon(IDI_ICON_START);
    button.DestroyWindow();
  }
  Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 2, "Repeated icons do not leak GDI handles");
  BufferedDeviceContext no_target(nullptr);  // Destruction is safe on the failure path.
  CDC target;
  Check(target.CreateCompatibleDC(nullptr), "Create target DC");
  CBitmap bitmap;
  CWindowDC screen(nullptr);
  Check(bitmap.CreateCompatibleBitmap(&screen, 32, 32), "Create target bitmap");
  CBitmap* previous = target.SelectObject(&bitmap);
  {
    BufferedDeviceContext buffered(&target);
    buffered.FillSolidRect(0, 0, 32, 32, RGB(10, 20, 30));
  }
  Check(target.GetPixel(0, 0) == RGB(10, 20, 30), "Buffered draw flushes to target");
  target.SelectObject(previous);
}

void TestDialogs(CWnd& parent) {
  CompletingDialog completing;
  Check(completing.DoModal() == IDOK, "Completed modal dialog retains result and releases its window");
  Check(completing.GetSafeHwnd() == nullptr, "Completed modal window is destroyed");
  DialogBase missing(65000);
  Check(missing.DoModal() == -1, "Missing dialog template fails deterministically");
  LayoutDialog dialog;
  Check(dialog.Create(IDD_ABOUTBOX, &parent), "Create layout dialog");
  CRect before, window, after;
  dialog.GetDlgItem(IDOK)->GetWindowRect(before);
  dialog.GetWindowRect(window);
  Check(dialog.SetWindowPos(nullptr, 0, 0, window.Width() + 60, window.Height() + 40, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
        "Resize dialog");
  dialog.GetDlgItem(IDOK)->GetWindowRect(after);
  Check(after.left - before.left == 60 && after.top - before.top == 40, "Deferred relative layout applied once");
  Check(after.Size() == before.Size(), "Moving controls preserves their sizes");
  dialog.DestroyWindow();
}

void TestProcessingRateControls(CWnd& parent) {
  DialogBase dialog(IDD_SSDREADY_DIALOG);
  Check(dialog.Create(IDD_SSDREADY_DIALOG, &parent), "Create hidden main dialog resource without starting the driver");
  const std::array<UINT, 5> controls = {IDC_STATIC_LEFT_STATUSBAR,
                                        IDC_STATIC_PROCESSING_RATE_LABEL,
                                        IDC_STATIC_RIGHT_STATUSBAR,
                                        IDC_STATIC_MAX_PROCESSING_RATE_LABEL,
                                        IDC_STATIC_MAX_PROCESSING_RATE};
  std::array<CRect, 5> bounds;
  for (std::size_t index = 0; index < controls.size(); ++index) {
    auto* control = dialog.GetDlgItem(controls[index]);
    Check(control != nullptr, "Separate current and peak rate controls exist");
    control->GetWindowRect(bounds[index]);
    if (index > 0)
      Check(bounds[index - 1].right <= bounds[index].left && bounds[index - 1].top == bounds[index].top,
            "Status and throughput controls share a row without overlapping");
  }
  CString text;
  dialog.GetDlgItemText(IDC_STATIC_MAX_PROCESSING_RATE_LABEL, text);
  Check(text == L"Max:", "Peak rate label identifies the maximum");
  dialog.GetDlgItemText(IDC_STATIC_MAX_PROCESSING_RATE, text);
  Check(text == L"0 items/s", "Peak rate starts at zero before the first sample");
  for (LPCWSTR value : {L"1 items/s", L"10 items/s", L"100 items/s", L"1000000 items/s"}) {
    dialog.SetDlgItemText(IDC_STATIC_RIGHT_STATUSBAR, value);
    dialog.SetDlgItemText(IDC_STATIC_MAX_PROCESSING_RATE, value);
    for (std::size_t index = 0; index < controls.size(); ++index) {
      CRect current;
      dialog.GetDlgItem(controls[index])->GetWindowRect(current);
      Check(current == bounds[index], "Changing rate digit count keeps labels and values in fixed controls");
    }
  }
  dialog.DestroyWindow();
}

void TestLogViewer(CWnd& parent) {
  // Make a hidden copy of the real template; modal viewers retain WS_VISIBLE.
  const HINSTANCE instance = AfxGetResourceHandle();
  const HRSRC resource = FindResource(instance, MAKEINTRESOURCE(IDD_DIALOG_LOG), RT_DIALOG);
  Check(resource != nullptr, "Find real log dialog template");
  const DWORD bytes = SizeofResource(instance, resource);
  const auto* source = static_cast<const char*>(LockResource(LoadResource(instance, resource)));
  Check(source && bytes >= 16 && reinterpret_cast<const WORD*>(source)[1] == 0xffff, "Extended dialog template");
  std::vector<ULONG_PTR> buffer((bytes + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
  std::memcpy(buffer.data(), source, bytes);
  DWORD style = 0;
  auto* data = reinterpret_cast<char*>(buffer.data());
  std::memcpy(&style, data + 12, sizeof(style));
  style &= ~WS_VISIBLE;
  std::memcpy(data + 12, &style, sizeof(style));
  LogViewerDialog dialog;
  Check(dialog.CreateIndirect(reinterpret_cast<const DLGTEMPLATE*>(buffer.data()), &parent), "Create real hidden log viewer");
  Check((dialog.GetStyle() & (WS_THICKFRAME | WS_MAXIMIZEBOX)) == (WS_THICKFRAME | WS_MAXIMIZEBOX), "Viewer can resize and maximize");
  auto* list = static_cast<CListBox*>(dialog.GetDlgItem(IDC_LIST_LOG));
  const auto refresh = [&] {
    MSG message;
    while (PeekMessage(&message, dialog.GetSafeHwnd(), AppLog::kLogUpdatedMessage, AppLog::kLogUpdatedMessage, PM_REMOVE))
      DispatchMessage(&message);
  };
  for (int index = 0; index < 100; ++index)
    AppLog::Get().Add(L"Log viewer regression message");
  refresh();
  Check(list->GetCount() >= 100 && list->GetTopIndex() > 0, "New entries follow the log tail");
  list->SetTopIndex(0);
  AppLog::Get().Add(L"New message while reading older entries");
  refresh();
  Check(list->GetTopIndex() == 0, "New entries preserve the position when browsing older logs");
  CRect window, before, after, button, client;
  dialog.GetWindowRect(window);
  list->GetWindowRect(before);
  dialog.GetDlgItem(IDOK)->GetWindowRect(button);
  Check(dialog.SetWindowPos(nullptr, 0, 0, window.Width() + 121, window.Height() + 81, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
        "Resize log viewer in both dimensions");
  list->GetWindowRect(after);
  Check(after.Width() == before.Width() + 121 && after.Height() == before.Height() + 81 && after.TopLeft() == before.TopLeft(),
        "Log list fills the additional dialog space");
  CRect resized_button;
  dialog.GetDlgItem(IDOK)->GetWindowRect(resized_button);
  Check(resized_button.top == button.top + 81 && resized_button.Size() == button.Size(), "OK button follows the bottom edge");
  dialog.ScreenToClient(resized_button);
  dialog.GetClientRect(client);
  Check(resized_button.left == (client.Width() - resized_button.Width()) / 2, "OK button stays centered after odd-width resizing");
  Check(dialog.SetWindowPos(nullptr, 0, 0, window.Width(), window.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
        "Restore viewer size");
  list->GetWindowRect(after);
  Check(after == before, "Repeated resizing restores the original log bounds");
  MINMAXINFO limits{};
  dialog.SendMessage(WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
  Check(limits.ptMinTrackSize.x == window.Width() && limits.ptMinTrackSize.y == window.Height(), "Viewer has a usable minimum size");
  dialog.DestroyWindow();
  AppLog::Get().Add(L"Log viewer closed; further logging remains safe");
}
}  // namespace

int main() {
  try {
    Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0), "MFC startup");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
    Check(InitCommonControlsEx(&controls), "Common controls startup");
    ApplicationCore core(ApplicationCore::StartupMode::kHeadless);
    g_application_core = &core;
    CWnd parent;
    Check(parent.CreateEx(0, AfxRegisterWndClass(0), L"UI regression host", WS_OVERLAPPED, CRect(0, 0, 500, 500), nullptr, 0),
          "Create hidden host");
    TestTree(parent);
    TestCombo(parent);
    TestDriveList(parent);
    TestIcons(parent);
    TestDialogs(parent);
    TestProcessingRateControls(parent);
    TestLogViewer(parent);
    Check(ui_statistics::DailyBytes(1, 2) == 43200, "Fractional bytes/second preserved");
    Check(ui_statistics::DailyBytes(123, 0) == 0, "Zero monitoring time handled");
    Check(ui_statistics::DailyBytes(123, 86400) == 123, "One-day values unchanged");
    Check(ui_statistics::DailyBytes((std::numeric_limits<std::uint64_t>::max)(), 1) == (std::numeric_limits<std::uint64_t>::max)(),
          "Daily estimate saturates on overflow");
    parent.DestroyWindow();
    std::cout << "UI ownership, selection, GDI, dialog-failure, resize and estimate checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (CException* error) {
    error->Delete();
    std::cerr << "Unexpected MFC exception\n";
    return 1;
  }
}
