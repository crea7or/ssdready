#pragma once

class Settings {
 public:

  virtual ~Settings() = default;
  virtual BOOL SaveSettings() = 0;
  virtual BOOL LoadSettings() = 0;

 protected:

  explicit Settings(LPCTSTR section) : section_(section) {
  }

  CString section_;
};
