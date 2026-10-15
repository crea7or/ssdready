// dialog_base.cpp : implementation file
// clang-format off
#include "pch.h"
// clang-format on
#include "dialog_base.h"

#include "about_dialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

DialogBase::DialogBase(UINT dlg_id, CWnd* parent) : CDialog(dlg_id, parent) {
  main_window_ = FALSE;
  hidden_window_ = FALSE;
  system_tray_ = FALSE;
  allow_resize_ = FALSE;

  background_back_.CreateSolidBrush(Core()->GetWindowBk());
}

BEGIN_MESSAGE_MAP(DialogBase, CDialog)
ON_WM_GETMINMAXINFO()
ON_WM_SYSCOMMAND()
ON_WM_QUERYDRAGICON()
ON_MESSAGE(kTrayNotificationMessage, TrayIconNotify)  // message mapped to function
ON_WM_INITMENUPOPUP()
ON_WM_ERASEBKGND()
ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL DialogBase::OnEraseBkgnd(CDC* dc) {
  // get clipping rectangle
  CRect clip;
  dc->GetClipBox(clip);
  // fill rectangle using a given brush
  dc->FillRect(clip, &background_back_);
  return TRUE;  // returns non-zero to prevent further erasing
}

HBRUSH DialogBase::OnCtlColor(CDC* dc, CWnd* window, UINT color_type) {
  HBRUSH brush = nullptr;
  switch (color_type) {
    case CTLCOLOR_DLG:
      // just return a not nullptr brush handle
      brush = (HBRUSH)background_back_;
      break;
    case CTLCOLOR_STATIC: {
      // set text color, transparent back node then
      dc->SetTextColor(Core()->GetTextColor());
      dc->SetBkMode(TRANSPARENT);
      // return a not nullptr brush handle
      brush = (HBRUSH)background_back_;
    } break;
    default:
      // do the default processing
      brush = CDialog::OnCtlColor(dc, window, color_type);
      break;
  }
  return brush;
}

void DialogBase::OnInitMenuPopup(CMenu* menu, UINT, BOOL) {
  if (menu == nullptr) {
    return;
  }
  // Check the enabled state of various menu items.

  CCmdUI state;
  state.m_pMenu = menu;
  ASSERT(state.m_pOther == nullptr);
  ASSERT(state.m_pParentMenu == nullptr);

  // Determine if menu is popup in top-level menu and set m_pOther to

  HMENU parent_menu;
  if (AfxGetThreadState()->m_hTrackingMenu == menu->m_hMenu)
    state.m_pParentMenu = menu;  // Parent == child for tracking popup.
  else if ((parent_menu = ::GetMenu(m_hWnd)) != nullptr) {
    CWnd* parent = this;
    // Child windows don't have menus--need to go to the top!
    if (parent != nullptr && (parent_menu = ::GetMenu(parent->m_hWnd)) != nullptr) {
      int index_count = ::GetMenuItemCount(parent_menu);
      for (int index = 0; index < index_count; index++) {
        if (::GetSubMenu(parent_menu, index) == menu->m_hMenu) {
          // When popup is found, m_pParentMenu is containing menu.
          state.m_pParentMenu = CMenu::FromHandle(parent_menu);
          break;
        }
      }
    }
  }

  state.m_nIndexMax = menu->GetMenuItemCount();
  for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax; state.m_nIndex++) {
    state.m_nID = menu->GetMenuItemID(state.m_nIndex);
    if (state.m_nID == 0)
      continue;  // Menu separator or invalid cmd - ignore it.

    ASSERT(state.m_pOther == nullptr);
    ASSERT(state.m_pMenu != nullptr);
    if (state.m_nID == (UINT)-1) {
      // Possibly a popup menu, route to first item of that popup.
      state.m_pSubMenu = menu->GetSubMenu(state.m_nIndex);
      if (state.m_pSubMenu == nullptr || (state.m_nID = state.m_pSubMenu->GetMenuItemID(0)) == 0 || state.m_nID == (UINT)-1) {
        continue;  // First item of popup can't be routed to.
      }
      state.DoUpdate(this, TRUE);  // Popups are never auto disabled.
    } else {
      // Normal menu item.
      // Auto enable/disable if frame window has m_bAutoMenuEnable
      // set and command is _not_ a system command.
      state.m_pSubMenu = nullptr;
      state.DoUpdate(this, FALSE);
    }

    // Adjust for menu deletions and additions.
    UINT count = menu->GetMenuItemCount();
    if (count < state.m_nIndexMax) {
      state.m_nIndex -= (state.m_nIndexMax - count);
      while (state.m_nIndex < count && menu->GetMenuItemID(state.m_nIndex) == state.m_nID) {
        state.m_nIndex++;
      }
    }
    state.m_nIndexMax = count;
  }
}

INT_PTR DialogBase::DoModal() {
  LPCDLGTEMPLATE dialog_template = m_lpDialogTemplate;
  HGLOBAL template_handle = m_hDialogTemplate;
  HINSTANCE instance = AfxGetResourceHandle();
  if (m_lpszTemplateName) {
    instance = AfxFindResourceHandle(m_lpszTemplateName, RT_DIALOG);
    const HRSRC resource = ::FindResource(instance, m_lpszTemplateName, RT_DIALOG);
    template_handle = resource ? LoadResource(instance, resource) : nullptr;
  }
  if (template_handle)
    dialog_template = static_cast<LPCDLGTEMPLATE>(LockResource(template_handle));
  if (!dialog_template)
    return -1;
  const HWND parent = PreModal();
  AfxUnhookWindowCreate();
  const bool enable_parent = parent && ::IsWindowEnabled(parent);
  if (enable_parent)
    ::EnableWindow(parent, FALSE);
  const auto cleanup = [this, parent, enable_parent](void*) {
    if (enable_parent)
      ::EnableWindow(parent, TRUE);
    if (parent && ::GetActiveWindow() == GetSafeHwnd())
      ::SetActiveWindow(parent);
    if (GetSafeHwnd())
      DestroyWindow();
    PostModal();
  };
  const std::unique_ptr<void, decltype(cleanup)> modal_scope(this, cleanup);
  m_nModalResult = -1;
  TRY {
    AfxHookWindowCreate(this);
    if (CreateDlgIndirect(dialog_template, CWnd::FromHandle(parent), instance)) {
      if (m_nFlags & WF_CONTINUEMODAL) {
        // MainDialog decides visibility so start-minimized remains respected.
        const DWORD flags = (GetStyle() & DS_NOIDLEMSG) ? MLF_NOIDLEMSG : 0;
        RunModalLoop(flags);
      }
      if (GetSafeHwnd())
        SetWindowPos(nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
  }
  CATCH_ALL(error) {
    // AFX_EXCEPTION_LINK deletes the caught MFC exception.
    m_nModalResult = -1;
  }
  END_CATCH_ALL
  return m_nModalResult;
}

BOOL DialogBase::OnInitDialog() {
  CDialog::OnInitDialog();

  // Set the icon for this dialog.  The framework does this automatically
  //  when the application's main window is not a dialog
  SetIcon(Core()->GetMainIcon(), TRUE);   // Set big icon
  SetIcon(Core()->GetMainIcon(), FALSE);  // Set small icon

  CWnd::EnableToolTips();

  //	if ( ! main_window_ )

  CenterWindow(GetOwner());

  SendMessage(DM_REPOSITION);

  return TRUE;
}

void DialogBase::OnSysCommand(UINT id, LPARAM parameter) {
  if ((id & 0xFFF0) == IDM_ABOUTBOX) {
    ShowAboutBox();
  } else {
    CDialog::OnSysCommand(id, parameter);
    if (id == SC_MINIMIZE) {
      MinimizeWindow();
    }
  }
}

void DialogBase::ShowAboutBox() {
  AboutDialog dlgAbout;
  dlgAbout.DoModal();
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR DialogBase::OnQueryDragIcon() {
  return (HCURSOR)Core()->GetMainIcon();
}

void DialogBase::OnGetMinMaxInfo(MINMAXINFO* info) {
  if (!allow_resize_) {
    CDialog::OnGetMinMaxInfo(info);
  } else {
    info->ptMinTrackSize.y = min_height_;
    info->ptMinTrackSize.x = min_width_;
  }
}

void DialogBase::SetAlwaysOnTop(const BOOL action, DWORD flags) {
  if (action) {
    SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | flags);
  } else {
    SetWindowPos(&wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | flags);
  }
}

void DialogBase::Tray(DWORD message, HICON icon, LPCTSTR tooltip) {
  if ((message == NIM_DELETE && !system_tray_) || (message == NIM_ADD && system_tray_))
    return;
  NOTIFYICONDATA data{};
  data.cbSize = sizeof(data);
  data.hWnd = GetSafeHwnd();
  data.uID = kTrayIconId;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data.uCallbackMessage = kTrayNotificationMessage;
  data.hIcon = icon;
  if (tooltip)
    wcsncpy_s(data.szTip, tooltip, _TRUNCATE);
  if (Shell_NotifyIcon(message, &data)) {
    if (message == NIM_ADD)
      system_tray_ = TRUE;
    if (message == NIM_DELETE)
      system_tray_ = FALSE;
  }
}

LRESULT DialogBase::TrayIconNotify(WPARAM, LPARAM parameter) {
  switch (parameter) {
    case WM_LBUTTONDBLCLK:
      ToggleWindowVisibility();
      break;
    case WM_RBUTTONDOWN:
      OnTrayRightClick();
      break;

    default:
      break;
  }
  return 0;
}

void DialogBase::MinimizeWindow() {
  ShowWindow(SW_MINIMIZE);
  if (Core()->GetInterfaceSettings()->tray_icon_) {
    if (main_window_) {
      ShowWindow(SW_HIDE);
    }
  }
  hidden_window_ = TRUE;
}

void DialogBase::RestoreWindow() {
  if (hidden_window_) {
    ShowWindow(SW_SHOW);
    ShowWindow(SW_RESTORE);
    SendMessage(DM_REPOSITION);
  } else {
    SetAlwaysOnTop(TRUE);
    SetAlwaysOnTop(FALSE);
  }
  hidden_window_ = FALSE;
}

void DialogBase::ToggleWindowVisibility() {
  if (!IsVisibleOnScreen()) {
    RestoreWindow();
  } else {
    MinimizeWindow();
  }
}

void DialogBase::OnTrayRightClick() {
}

BOOL DialogBase::IsVisibleOnScreen() {
  return !hidden_window_ && IsWindowVisible() && !IsIconic();
}
