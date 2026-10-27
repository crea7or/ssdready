#include <iostream>
#include <thread>

// clang-format off
#include "pch.h"
// clang-format on

namespace {
CWinApp drive_test_app;

DriveInfoListPtr Snapshot(wchar_t letter, const wchar_t* volume = L"\\Device\\HarddiskVolume42") {
  auto result = std::make_unique<DriveInfoList>();
  auto& drive = result->emplace_back();
  drive.volume_name = volume;
  drive.drive_letter[0] = letter;
  drive.volume_size = 8ULL * 1024 * 1024 * 1024 * 1024;
  return result;
}

bool Resolve(MiniSpyManager& manager, const wchar_t* input, const wchar_t* expected) {
  CString path = L"old result";
  manager.VolumeToDriveName(input, path);
  if (path == expected)
    return true;
  std::wcerr << L"Path mismatch: " << path.GetString() << L", expected " << expected << L'\n';
  return false;
}

int TestNativeEnumeration() {
  // Distinguish a missing administrator token from an implementation failure.
  alignas(FILTER_VOLUME_BASIC_INFORMATION) char buffer[1024]{};
  DWORD returned = 0;
  HANDLE iterator = INVALID_HANDLE_VALUE;
  const HRESULT result = FilterVolumeFindFirst(FilterVolumeBasicInformation, buffer, sizeof(buffer), &returned, &iterator);
  if (SUCCEEDED(result))
    FilterVolumeFindClose(iterator);
  if (result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
    std::cout << "Native filter-volume enumeration requires an administrator token; skipped.\n";
    return 77;
  }
  // Native enumeration must release its iterators and volume handles on repeat.
  DWORD handles_before = 0, handles_after = 0;
  if (!GetProcessHandleCount(GetCurrentProcess(), &handles_before))
    return 1;
  std::size_t volumes_seen = 0;
  for (int iteration = 0; iteration < 64; ++iteration) {
    auto volumes = GetVolumes();
    if (!volumes) {
      std::cerr << "Native volume enumeration failed.\n";
      return 1;
    }
    volumes_seen = volumes->size();
    for (const auto& drive : *volumes) {
      if (drive.volume_name.empty() || !drive.HasDriveLetter() || drive.drive_letter[1] != L':' || drive.drive_letter[2] != L'\0')
        return 1;
    }
  }
  if (!GetProcessHandleCount(GetCurrentProcess(), &handles_after) || handles_after > handles_before + 1)
    return 1;
  std::cout << "Repeated native enumeration passed.\n"
            << "DriveInfo: " << sizeof(DriveInfo) << " bytes; native volumes: " << volumes_seen << "; process handles: " << handles_before
            << " -> " << handles_after << ".\n";
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--native")
    return TestNativeEnumeration();
  if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
    return 1;
  MiniSpyManager manager;
  if (manager.UpdateVolumes(nullptr) || !manager.UpdateVolumes(Snapshot(L'C')) || !manager.UpdateVolumes(Snapshot(L'D')))
    return 1;
  if (!Resolve(manager, nullptr, L"#:\\unknown.file") ||
      !Resolve(manager, L"\\Device\\HarddiskVolume42\\folder\\file.dat", L"D:\\folder\\file.dat") ||
      !Resolve(manager, L"\\Device\\HarddiskVolume42", L"D:") ||
      !Resolve(manager, L"\\Device\\HarddiskVolume420\\file.dat", L"#:\\Device\\HarddiskVolume420\\file.dat"))
    return 1;

  auto ui_snapshot = Snapshot(L'D');
  auto worker_snapshot = std::make_unique<DriveInfoList>(*ui_snapshot);
  if (!manager.UpdateVolumes(std::move(worker_snapshot)))
    return 1;
  ui_snapshot->front().drive_letter[0] = L'E';
  ui_snapshot->front().volume_name = L"changed UI volume";
  ui_snapshot.reset();
  if (!Resolve(manager, L"\\Device\\HarddiskVolume42\\file.dat", L"D:\\file.dat"))
    return 1;
  if (!manager.UpdateVolumes(Snapshot(L'#')) ||
      !Resolve(manager, L"\\Device\\HarddiskVolume42\\file.dat", L"#:\\Device\\HarddiskVolume42\\file.dat"))
    return 1;

  // Exercise the real producer/consumer ownership handoff, without a driver.
  constexpr int updates = 20000;
  std::atomic<int> consumed{0};
  std::atomic<bool> failed{false};
  std::thread publisher([&] {
    for (int index = 1; index <= updates; ++index) {
      while (consumed.load(std::memory_order_acquire) != index - 1)
        std::this_thread::yield();
      auto volumes = Snapshot(index & 1 ? L'C' : L'D');
      if (!manager.UpdateVolumes(std::move(volumes)))
        failed = true;
      if (manager.UpdateVolumes(nullptr))
        failed = true;
    }
  });
  for (int index = 1; index <= updates; ++index) {
    const wchar_t* expected = index & 1 ? L"C:\\file.dat" : L"D:\\file.dat";
    CString path;
    do {
      manager.VolumeToDriveName(L"\\Device\\HarddiskVolume42\\file.dat", path);
      if (failed)
        break;
      std::this_thread::yield();
    } while (path != expected);
    consumed.store(index, std::memory_order_release);
  }
  publisher.join();
  if (failed)
    return 1;

  std::cout << "Drive snapshots, path boundaries and " << updates << " atomic updates passed.\n"
            << "DriveInfo: " << sizeof(DriveInfo) << " bytes.\n";
  return 0;
}
