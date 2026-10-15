// clang-format off
#include "pch.h"
// clang-format on

#include <memory>

ApplicationCore* g_application_core = nullptr;
SettingsRegistry* g_settings_registry = nullptr;

void NewCore() {
  // Publish the registry for settings initialization, retaining ownership until
  // core construction completes. An exception must not leak the registry.
  auto registry = std::make_unique<SettingsRegistry>();
  g_settings_registry = registry.get();
  try {
    g_application_core = new ApplicationCore;
  } catch (...) {
    g_settings_registry = nullptr;
    throw;
  }
  registry.release();
}

void DeleteCore() {
  delete g_application_core;
  g_application_core = nullptr;
  delete g_settings_registry;
  g_settings_registry = nullptr;
}
