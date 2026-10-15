#pragma once

class File;

class NamedObject {
 public:

  virtual ~NamedObject() = default;

  BOOL CompareAlias(LPCTSTR alias) const {
    return alias_.CompareNoCase(alias) == 0;
  }

  LPCTSTR GetAlias() const {
    return alias_;
  }

  void SetAlias(LPCTSTR alias) {
    alias_ = alias;
  }

  LPCTSTR GetDescription() const {
    return description_;
  }

  virtual BOOL Save(File* file) const;
  virtual BOOL Load(File* file);

 protected:

  CString alias_;
  CString description_;
};
