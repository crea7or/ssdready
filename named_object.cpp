// clang-format off
#include "pch.h"
// clang-format on
#include "named_object.h"

BOOL NamedObject::Save(File* file) const {
  DWORD version = 0;
  return file->Write(&version, sizeof(version)) && file->WriteString(alias_);
}

BOOL NamedObject::Load(File* file) {
  DWORD version = 0;
  CString alias;
  if (!file->Read(&version, sizeof(version)))
    return FALSE;
  if (version != 0) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  if (!file->ReadString(alias))
    return FALSE;
  alias_ = std::move(alias);
  description_.Empty();
  return TRUE;
}
