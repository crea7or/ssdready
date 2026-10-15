// pch.h : include file for standard system include files
#pragma once

#ifndef _WIN32
#define _WIN32
#endif

// QueryFullProcessImageNameW requires Windows Vista or later.
#define WINVER _WIN32_WINNT_VISTA
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define NTDDI_VERSION NTDDI_VISTA


#define _AFX_NO_MFC_CONTROLS_IN_DIALOGS

#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS  // some CString constructors will be explicit
#define _CRT_SECURE_NO_WARNINGS
// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS

// clang-format off
// MFC/Windows definitions must precede the filter-manager SDK headers.
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcmn.h>
#include <afxpriv.h>
#include <Shlwapi.h>
#include <wininet.h>
#include <sal.h>
#include <Fltuser.h>
#include <DriverSpecs.h>
#include <winioctl.h>
// clang-format on

#include "app_constants.h"
#include "file.h"
#include "file_error.h"
#include "named_object.h"
#include "settings_registry.h"
#include "string_utils.h"
#include "styled_button.h"
#include "system_error.h"


#pragma message("Linking Shlwapi.lib")
#pragma comment(lib, "Shlwapi.lib")

#pragma message("Linking Wininet.lib")
#pragma comment(lib, "Wininet.lib")

#pragma message("Linking Winmm.lib")
#pragma comment(lib, "Winmm.lib")

#pragma message("Linking FltLib.lib")
#pragma comment(lib, "FltLib.lib")

#pragma message("Linking Psapi.lib")
#pragma comment(lib, "Psapi.lib")

#include "application_core.h"

extern ApplicationCore* g_application_core;
extern SettingsRegistry* g_settings_registry;

inline ApplicationCore* Core() {
  return g_application_core;
};

inline SettingsRegistry* Registry() {
  return g_settings_registry;
};

void NewCore();
void DeleteCore();
