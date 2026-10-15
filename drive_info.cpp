// clang-format off
#include "pch.h"
// clang-format on

#include "drive_info.h"

#include <cstddef>
#include <string_view>

namespace {
// Filter enumeration handles require their own close functions.
struct FilterIterator {
  HANDLE handle = INVALID_HANDLE_VALUE;
  HRESULT(WINAPI* close)(HANDLE);

  ~FilterIterator() {
    if (handle != INVALID_HANDLE_VALUE)
      close(handle);
  }
};

// Pointer-sized storage keeps the returned SDK structures properly aligned.
template <typename Query>
HRESULT QueryInformation(std::vector<ULONG_PTR>& buffer, Query query) {
  for (;;) {
    const auto capacity = static_cast<DWORD>(buffer.size() * sizeof(ULONG_PTR));
    DWORD required = 0;
    const HRESULT result = query(buffer.data(), capacity, &required);
    if (result != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) && result != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
      return result;
    if (required <= capacity)
      return result;
    buffer.resize((static_cast<std::size_t>(required) + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
  }
}

struct VolumeHandle {
  HANDLE handle;

  explicit VolumeHandle(const wchar_t* path)
      : handle(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)) {
    // Attributes need no read access; try that if opening for the size query failed.
    if (handle == INVALID_HANDLE_VALUE)
      handle = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  }

  ~VolumeHandle() {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }

  VolumeHandle(const VolumeHandle&) = delete;
  VolumeHandle& operator=(const VolumeHandle&) = delete;
};

bool IsShadowCopy(HANDLE volume, std::wstring_view name) {
  VOLUME_GET_GPT_ATTRIBUTES_INFORMATION attributes{};
  DWORD returned = 0;
  if (volume != INVALID_HANDLE_VALUE &&
      DeviceIoControl(volume, IOCTL_VOLUME_GET_GPT_ATTRIBUTES, nullptr, 0, &attributes, sizeof(attributes), &returned, nullptr) &&
      returned >= sizeof(attributes)) {
    return (attributes.GptAttributes & GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY) != 0;
  }

  // Some providers do not expose attributes. Recognize Windows software
  // snapshot devices in that case.
  constexpr std::wstring_view prefix = L"\\Device\\HarddiskVolumeShadowCopy";
  return name.size() >= prefix.size() && _wcsnicmp(name.data(), prefix.data(), prefix.size()) == 0;
}

std::uint64_t GetVolumeSize(HANDLE volume) {
  // Query the volume length directly; a fixed VOLUME_DISK_EXTENTS buffer cannot
  // represent volumes spanning multiple disks.
  if (volume == INVALID_HANDLE_VALUE)
    return 0;
  GET_LENGTH_INFORMATION length{};
  DWORD returned = 0;
  const BOOL result = DeviceIoControl(volume, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length, sizeof(length), &returned, nullptr);
  return result && returned >= sizeof(length) && length.Length.QuadPart > 0 ? static_cast<std::uint64_t>(length.Length.QuadPart) : 0;
}

std::uint32_t CountInstances(const wchar_t* volume_name) {
  std::vector<ULONG_PTR> buffer(128);
  FilterIterator iterator{INVALID_HANDLE_VALUE, FilterVolumeInstanceFindClose};
  HRESULT result = QueryInformation(buffer, [&](void* data, DWORD capacity, DWORD* returned) {
    return FilterVolumeInstanceFindFirst(volume_name, InstanceFullInformation, data, capacity, returned, &iterator.handle);
  });
  std::uint32_t count = 0;
  while (SUCCEEDED(result)) {
    const auto* info = reinterpret_cast<const INSTANCE_FULL_INFORMATION*>(buffer.data());
    const auto bytes = buffer.size() * sizeof(ULONG_PTR);
    if (info->FilterNameBufferOffset > bytes || info->FilterNameLength > bytes - info->FilterNameBufferOffset)
      break;
    const auto* name = reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(info) + info->FilterNameBufferOffset);
    const std::wstring_view filter_name(name, info->FilterNameLength / sizeof(wchar_t));
    constexpr std::wstring_view expected_name = kFilterName;
    if (filter_name.size() == expected_name.size() && _wcsnicmp(filter_name.data(), expected_name.data(), expected_name.size()) == 0)
      ++count;
    result = QueryInformation(buffer, [&](void* data, DWORD capacity, DWORD* returned) {
      return FilterVolumeInstanceFindNext(iterator.handle, InstanceFullInformation, data, capacity, returned);
    });
  }
  return count;
}
}  // namespace

DriveInfoListPtr GetVolumes() {
  std::vector<ULONG_PTR> buffer(128);
  FilterIterator iterator{INVALID_HANDLE_VALUE, FilterVolumeFindClose};
  HRESULT result = QueryInformation(buffer, [&](void* data, DWORD capacity, DWORD* returned) {
    return FilterVolumeFindFirst(FilterVolumeBasicInformation, data, capacity, returned, &iterator.handle);
  });
  auto volumes = std::make_unique<DriveInfoList>();
  while (SUCCEEDED(result)) {
    const auto* info = reinterpret_cast<const FILTER_VOLUME_BASIC_INFORMATION*>(buffer.data());
    const auto name_length = info->FilterVolumeNameLength / sizeof(wchar_t);
    if (info->FilterVolumeNameLength > buffer.size() * sizeof(ULONG_PTR) - offsetof(FILTER_VOLUME_BASIC_INFORMATION, FilterVolumeName))
      return nullptr;
    const std::wstring_view name(info->FilterVolumeName, name_length);
    constexpr std::wstring_view prefix = L"\\Device\\HarddiskVolume";
    if (name.size() >= prefix.size() && _wcsnicmp(name.data(), prefix.data(), prefix.size()) == 0) {
      std::wstring volume_name(name);
      wchar_t dos_name[MAX_PATH + 1]{};
      // Resolve eligibility before opening the volume or counting attachments.
      if (SUCCEEDED(FilterGetDosName(volume_name.c_str(), dos_name, MAX_PATH)) &&
          ((dos_name[0] >= L'A' && dos_name[0] <= L'Z') || (dos_name[0] >= L'a' && dos_name[0] <= L'z')) && dos_name[1] == L':' &&
          dos_name[2] == L'\0') {
        const std::wstring volume_path = L"\\\\?\\GLOBALROOT" + volume_name;
        const VolumeHandle volume(volume_path.c_str());
        if (!IsShadowCopy(volume.handle, name)) {
          auto& drive = volumes->emplace_back();
          drive.volume_name = std::move(volume_name);
          drive.drive_letter[0] = dos_name[0];
          drive.instances = CountInstances(drive.volume_name.c_str());
          drive.volume_size = GetVolumeSize(volume.handle);
        }
      }
    }
    result = QueryInformation(buffer, [&](void* data, DWORD capacity, DWORD* returned) {
      return FilterVolumeFindNext(iterator.handle, FilterVolumeBasicInformation, data, capacity, returned);
    });
  }
  if (result != HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))
    return nullptr;
  return volumes;
}
