#pragma once

#include <windows.h>

// Per-user interactive logon task. Never stores a password or uses SYSTEM.
BOOL ConfigureAutostartTask(bool enabled, LPCWSTR executable);
