// clang-format off
#include "pch.h"
// clang-format on

#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>

// Exercise the production enumerator with simulated Windows boundaries.
// This test never opens real volumes or changes filter attachments.
namespace fake {
enum class AttributeResult { kSuccess, kUnsupported, kTruncated };
enum class Access { kRead, kAttributesOnly, kDenied };

struct Volume {
  std::wstring name;
  DWORDLONG attributes = 0;
  AttributeResult attribute_result = AttributeResult::kSuccess;
  Access access = Access::kRead;
  std::wstring dos_name;
  unsigned opens = 0;
  unsigned attribute_queries = 0;
  unsigned length_queries = 0;
  unsigned instance_queries = 0;
  unsigned dos_queries = 0;
};

std::vector<Volume> volumes;
std::size_t index = 0;
int handles = 0;
int iterators = 0;
bool fail_next = false;
constexpr LONGLONG kLength = 8LL * 1024 * 1024 * 1024 * 1024;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

HRESULT WriteVolume(void* data, DWORD capacity, DWORD* required) {
  if (index >= volumes.size())
    return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
  const auto& name = volumes[index].name;
  *required = static_cast<DWORD>(offsetof(FILTER_VOLUME_BASIC_INFORMATION, FilterVolumeName) + name.size() * sizeof(wchar_t));
  if (capacity < *required)
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  auto* info = static_cast<FILTER_VOLUME_BASIC_INFORMATION*>(data);
  info->FilterVolumeNameLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
  std::memcpy(info->FilterVolumeName, name.data(), name.size() * sizeof(wchar_t));
  return S_OK;
}

HRESULT WINAPI FindFirst(FILTER_VOLUME_INFORMATION_CLASS info_class, void* data, DWORD capacity, DWORD* required, HANDLE* iterator) {
  Check(info_class == FilterVolumeBasicInformation, "Unexpected enumeration class");
  index = 0;
  const HRESULT result = WriteVolume(data, capacity, required);
  if (SUCCEEDED(result)) {
    *iterator = reinterpret_cast<HANDLE>(1);
    ++iterators;
  }
  return result;
}

HRESULT WINAPI FindNext(HANDLE, FILTER_VOLUME_INFORMATION_CLASS, void* data, DWORD capacity, DWORD* required) {
  if (fail_next)
    return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  ++index;
  // The fake names fit the initial buffer except in the single-volume resize test.
  return WriteVolume(data, capacity, required);
}

HRESULT WINAPI FindClose(HANDLE) {
  --iterators;
  return S_OK;
}

Volume& ByName(const wchar_t* name) {
  for (auto& volume : volumes) {
    if (volume.name == name)
      return volume;
  }
  throw std::runtime_error("Unknown volume name");
}

HANDLE WINAPI Open(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES, DWORD creation, DWORD, HANDLE) {
  constexpr std::wstring_view prefix = L"\\\\?\\GLOBALROOT";
  Check(std::wstring_view(path).substr(0, prefix.size()) == prefix, "Volume path must use GLOBALROOT");
  Check(sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE) && creation == OPEN_EXISTING, "Unexpected open parameters");
  auto& volume = ByName(path + prefix.size());
  ++volume.opens;
  if (volume.access == Access::kDenied || (volume.access == Access::kAttributesOnly && access != 0)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  ++handles;
  return &volume;
}

BOOL WINAPI Close(HANDLE) {
  --handles;
  return TRUE;
}

BOOL WINAPI IoControl(HANDLE handle, DWORD code, LPVOID, DWORD, LPVOID data, DWORD capacity, LPDWORD returned, LPOVERLAPPED) {
  auto& volume = *static_cast<Volume*>(handle);
  if (code == IOCTL_VOLUME_GET_GPT_ATTRIBUTES) {
    ++volume.attribute_queries;
    Check(capacity == sizeof(VOLUME_GET_GPT_ATTRIBUTES_INFORMATION), "Invalid attribute buffer size");
    if (volume.attribute_result == AttributeResult::kUnsupported) {
      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }
    static_cast<VOLUME_GET_GPT_ATTRIBUTES_INFORMATION*>(data)->GptAttributes = volume.attributes;
    *returned = volume.attribute_result == AttributeResult::kTruncated ? 0 : capacity;
    return TRUE;
  }
  Check(code == IOCTL_DISK_GET_LENGTH_INFO, "Unexpected volume IOCTL");
  ++volume.length_queries;
  if (volume.access != Access::kRead) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  Check(capacity == sizeof(GET_LENGTH_INFORMATION), "Invalid length buffer size");
  static_cast<GET_LENGTH_INFORMATION*>(data)->Length.QuadPart = kLength;
  *returned = capacity;
  return TRUE;
}

HRESULT WINAPI FindInstanceFirst(LPCWSTR name, INSTANCE_INFORMATION_CLASS, LPVOID, DWORD, LPDWORD, PHANDLE) {
  ++ByName(name).instance_queries;
  return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
}

HRESULT WINAPI DosName(LPCWSTR name, LPWSTR output, DWORD capacity) {
  auto& volume = ByName(name);
  ++volume.dos_queries;
  Check(capacity >= 3, "Insufficient DOS-name buffer");
  if (volume.dos_name.empty())
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  Check(volume.dos_name.size() + 1 <= capacity, "Insufficient DOS-name buffer");
  std::memcpy(output, volume.dos_name.c_str(), (volume.dos_name.size() + 1) * sizeof(wchar_t));
  return S_OK;
}
}  // namespace fake

#define FilterVolumeFindFirst fake::FindFirst
#define FilterVolumeFindNext fake::FindNext
#define FilterVolumeFindClose fake::FindClose
#define FilterVolumeInstanceFindFirst fake::FindInstanceFirst
#define FilterGetDosName fake::DosName
#define CreateFileW fake::Open
#define DeviceIoControl fake::IoControl
#define CloseHandle fake::Close
#include "../drive_info.cpp"
#undef FilterVolumeFindFirst
#undef FilterVolumeFindNext
#undef FilterVolumeFindClose
#undef FilterVolumeInstanceFindFirst
#undef FilterGetDosName
#undef CreateFileW
#undef DeviceIoControl
#undef CloseHandle

int main() {
  using namespace fake;
  try {
    // Detect hardware/provider snapshots with ordinary names, including when
    // only a metadata handle can be opened. Preserve unrelated volume flags.
    volumes = {
        {L"\\Device\\HarddiskVolume1", 0, AttributeResult::kSuccess, Access::kRead, L"C:"},
        {L"\\Device\\HarddiskVolume2", GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY, AttributeResult::kSuccess, Access::kRead, L"E:"},
        {L"\\Device\\HarddiskVolume3",
         GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY | GPT_BASIC_DATA_ATTRIBUTE_HIDDEN,
         AttributeResult::kSuccess,
         Access::kAttributesOnly,
         L"F:"},
        {L"\\Device\\HarddiskVolume4", GPT_BASIC_DATA_ATTRIBUTE_HIDDEN | GPT_BASIC_DATA_ATTRIBUTE_NO_DRIVE_LETTER},
        {L"\\Device\\HarddiskVolume5", GPT_BASIC_DATA_ATTRIBUTE_READ_ONLY, AttributeResult::kSuccess, Access::kRead, L"R:"},
        {L"\\Device\\HarddiskVolume6", 0, AttributeResult::kUnsupported, Access::kRead, L"u:"},
        {L"\\Device\\HarddiskVolume7", 0, AttributeResult::kSuccess, Access::kDenied, L"V:"},
        {L"\\Device\\HarddiskVolume8", GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY, AttributeResult::kTruncated, Access::kRead, L"W:"},
        {L"\\Device\\HarddiskVolumeShadowCopy10", GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY, AttributeResult::kSuccess, Access::kRead, L"X:"},
        {L"\\Device\\HarddiskVolumeShadowCopy11", 0, AttributeResult::kUnsupported, Access::kRead, L"Y:"},
        {L"\\device\\harddiskvolumeshadowcopy12", 0, AttributeResult::kSuccess, Access::kDenied, L"Z:"},
        {L"\\Device\\HarddiskVolumeShadowCopy13", 0, AttributeResult::kTruncated, Access::kRead, L"A:"},
        {L"\\Device\\NamedPipe"},
        {L"\\Device\\HarddiskVolume14",
         GPT_BASIC_DATA_ATTRIBUTE_HIDDEN | GPT_BASIC_DATA_ATTRIBUTE_NO_DRIVE_LETTER,
         AttributeResult::kSuccess,
         Access::kRead,
         L"H:"},
        {L"\\Device\\HarddiskVolume15"},
        {L"\\Device\\HarddiskVolume16", 0, AttributeResult::kSuccess, Access::kRead, L"1:"},
        {L"\\Device\\HarddiskVolume17", 0, AttributeResult::kSuccess, Access::kRead, L"D"},
        {L"\\Device\\HarddiskVolume18", 0, AttributeResult::kSuccess, Access::kRead, L"E;"},
        {L"\\Device\\HarddiskVolume19", 0, AttributeResult::kSuccess, Access::kRead, L"C:\\mount"},
    };
    for (int repetition = 0; repetition < 32; ++repetition) {
      auto result = GetVolumes();
      Check(result && result->size() == 6, "Filtering lost eligible volumes or retained snapshots/unresolved volumes");
      constexpr std::size_t kept[] = {0, 4, 5, 6, 7, 13};
      for (std::size_t row = 0; row < std::size(kept); ++row) {
        Check((*result)[row].volume_name == volumes[kept[row]].name, "Volume order/name changed");
        Check((*result)[row].volume_size == (kept[row] == 6 ? 0 : static_cast<std::uint64_t>(kLength)), "Incorrect volume size");
        Check((*result)[row].HasDriveLetter() && (*result)[row].drive_letter[0] == volumes[kept[row]].dos_name[0],
              "Drive-letter mapping changed");
      }
      Check(handles == 0 && iterators == 0, "Volume or iterator handle leak");
    }
    for (const std::size_t excluded : {1U, 2U, 3U, 8U, 9U, 10U, 11U, 12U, 14U, 15U, 16U, 17U, 18U}) {
      const auto& volume = volumes[excluded];
      Check(volume.length_queries == 0 && volume.instance_queries == 0, "Excluded volume triggered unnecessary metadata queries");
    }
    for (const std::size_t unresolved : {3U, 14U, 15U, 16U, 17U, 18U}) {
      Check(volumes[unresolved].opens == 0 && volumes[unresolved].attribute_queries == 0,
            "Unresolved volume was opened or queried for attributes");
    }
    Check(volumes[2].opens == 64 && volumes[2].attribute_queries == 32, "Attribute-only open fallback not exercised");
    Check(volumes[12].opens == 0, "Non-disk device was opened");

    fail_next = true;
    Check(!GetVolumes(), "Enumeration failure must return nullptr");
    Check(handles == 0 && iterators == 0, "Failure leaked enumeration handles");
    fail_next = false;
    volumes = {{L"\\Device\\HarddiskVolumeShadowCopy" + std::wstring(600, L'1'),
                GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY,
                AttributeResult::kSuccess,
                Access::kRead,
                L"S:"}};
    auto snapshots_only = GetVolumes();
    Check(snapshots_only && snapshots_only->empty(), "Snapshot-only enumeration must succeed with an empty list");
    Check(handles == 0 && iterators == 0, "Resized enumeration leaked handles");
    volumes = {{L"\\Device\\HarddiskVolume20"}};
    auto unresolved_only = GetVolumes();
    Check(unresolved_only && unresolved_only->empty(), "Unresolved-only enumeration must succeed with an empty list");
    volumes.clear();
    auto empty = GetVolumes();
    Check(empty && empty->empty(), "Empty enumeration failed");
    std::cout << "Volume shadow flags, drive-letter eligibility, query failures and handle cleanup passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
