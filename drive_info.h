#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A volume snapshot, independent of the manager and of UI row positions.
struct DriveInfo {
  std::wstring volume_name;
  std::uint64_t volume_size = 0;
  std::uint32_t instances = 0;
  std::array<wchar_t, 3> drive_letter{L'#', L':', L'\0'};

  bool HasDriveLetter() const {
    return drive_letter[0] != L'#';
  }
};

using DriveInfoList = std::vector<DriveInfo>;
using DriveInfoListPtr = std::unique_ptr<DriveInfoList>;

// Enumerate a fresh, caller-owned snapshot. nullptr means enumeration failed.
// Shadow copies and volumes without a resolved drive letter are excluded.
// An empty list means enumeration succeeded but found no eligible hard-disk volumes.
DriveInfoListPtr GetVolumes();
