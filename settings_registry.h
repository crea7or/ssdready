#pragma once

// Owns an open registry key. Read operations never create keys; a section can be
// reused for all values in one load/save. It must not be shared between threads.
class RegistrySection {
 public:

  RegistrySection() = default;
  ~RegistrySection();
  RegistrySection(const RegistrySection&) = delete;
  RegistrySection& operator=(const RegistrySection&) = delete;
  RegistrySection(RegistrySection&& other) noexcept;
  RegistrySection& operator=(RegistrySection&& other) noexcept;

  static RegistrySection Open(HKEY root, LPCWSTR path, bool write = false);

  explicit operator bool() const {
    return key_ != nullptr;
  }

  DWORD GetInt(LPCWSTR name, DWORD default_value) const;
  CString GetString(LPCWSTR name, LPCWSTR default_value = L"") const;
  BOOL WriteInt(LPCWSTR name, DWORD value) const;
  BOOL WriteString(LPCWSTR name, LPCWSTR value) const;
  BOOL DeleteValue(LPCWSTR name) const;

 private:

  HKEY key_ = nullptr;
};

class SettingsRegistry {
 public:

  SettingsRegistry();

  // root is a borrowed predefined hive or a key that outlives this registry.
  SettingsRegistry(HKEY root, LPCWSTR path) : root_(root), path_(path) {
  }

  RegistrySection OpenSection(LPCWSTR section, bool write = false) const;

 private:

  HKEY root_ = HKEY_CURRENT_USER;
  CString path_ = L"Software\\CEZEO software\\SsdReady";
};
