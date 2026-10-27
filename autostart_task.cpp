// clang-format off
#include "pch.h"
// clang-format on
#include "autostart_task.h"

#include <atlbase.h>
#include <sddl.h>
#include <taskschd.h>

#include <string>
#include <vector>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {
struct ComScope {
  HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  ~ComScope() {
    if (SUCCEEDED(result))
      CoUninitialize();
  }
};

HRESULT Configure(bool enabled, LPCWSTR executable) {
  ComScope com;
  if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
    return com.result;
  const HRESULT security = CoInitializeSecurity(
      nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
  // COM security is process-wide; an earlier shell/MFC client may own it.
  if (FAILED(security) && security != RPC_E_TOO_LATE)
    return security;
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return HRESULT_FROM_WIN32(GetLastError());
  const std::unique_ptr<void, decltype(&CloseHandle)> token_owner(token, CloseHandle);
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if (!size)
    return HRESULT_FROM_WIN32(GetLastError());
  std::vector<unsigned char> user(size);
  if (!GetTokenInformation(token, TokenUser, user.data(), size, &size))
    return HRESULT_FROM_WIN32(GetLastError());
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
    return HRESULT_FROM_WIN32(GetLastError());
  const std::unique_ptr<wchar_t, decltype(&LocalFree)> sid_owner(sid, LocalFree);
  const CComBSTR task_name((std::wstring(L"SsdReady-") + sid).c_str());
  CComPtr<ITaskService> service;
  HRESULT result = service.CoCreateInstance(CLSID_TaskScheduler);
  if (FAILED(result))
    return result;
  if (FAILED(result = service->Connect(CComVariant(), CComVariant(), CComVariant(), CComVariant())))
    return result;
  CComPtr<ITaskFolder> folder;
  if (FAILED(result = service->GetFolder(CComBSTR(L"\\"), &folder)))
    return result;
  if (!enabled) {
    result = folder->DeleteTask(task_name, 0);
    return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_OK : result;
  }
  if (!executable || !*executable)
    return E_INVALIDARG;
  CComPtr<ITaskDefinition> task;
  if (FAILED(result = service->NewTask(0, &task)))
    return result;
  CComPtr<IPrincipal> principal;
  if (FAILED(result = task->get_Principal(&principal)) || FAILED(result = principal->put_UserId(CComBSTR(sid))) ||
      FAILED(result = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
      FAILED(result = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST)))
    return result;
  CComPtr<ITaskSettings> settings;
  if (FAILED(result = task->get_Settings(&settings)) || FAILED(result = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
      FAILED(result = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
      FAILED(result = settings->put_ExecutionTimeLimit(CComBSTR(L"PT0S"))) ||
      FAILED(result = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)))
    return result;
  CComPtr<ITriggerCollection> triggers;
  CComPtr<ITrigger> trigger;
  CComPtr<ILogonTrigger> logon;
  if (FAILED(result = task->get_Triggers(&triggers)) || FAILED(result = triggers->Create(TASK_TRIGGER_LOGON, &trigger)) ||
      FAILED(result = trigger.QueryInterface(&logon)) || FAILED(result = logon->put_UserId(CComBSTR(sid))))
    return result;
  CComPtr<IActionCollection> actions;
  CComPtr<IAction> action;
  CComPtr<IExecAction> exec;
  if (FAILED(result = task->get_Actions(&actions)) || FAILED(result = actions->Create(TASK_ACTION_EXEC, &action)) ||
      FAILED(result = action.QueryInterface(&exec)) || FAILED(result = exec->put_Path(CComBSTR(executable))))
    return result;
  CComPtr<IRegisteredTask> registered;
  return folder->RegisterTaskDefinition(
      task_name, task, TASK_CREATE_OR_UPDATE, CComVariant(sid), CComVariant(), TASK_LOGON_INTERACTIVE_TOKEN, CComVariant(), &registered);
}
}  // namespace

BOOL ConfigureAutostartTask(bool enabled, LPCWSTR executable) {
  const HRESULT result = Configure(enabled, executable);
  if (FAILED(result)) {
    AppLog::Get().Add(L"Could not update the elevated logon task (HRESULT {:08x}).", static_cast<ULONG>(result));
    SetLastError(HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : ERROR_GEN_FAILURE);
    return FALSE;
  }
  return TRUE;
}
