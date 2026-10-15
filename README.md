# SsdReady

**Free, open-source disk-write monitoring for Windows.**

SsdReady measures disk activity under your everyday workload to help you
understand SSD endurance and find applications that write frequently. It shows
where writes go, which processes generate them, and how much data your workload
writes per day. The project is now available free of charge under the
[MIT license](LICENSE).

The [SsdReady website](https://www.ssdready.com/) describes the application's
background and original workflow. Its older free/paid edition descriptions do
not describe the licensing of this open-source repository.

## Features

- Monitor selected drives with the SsdSpy filesystem minifilter.
- View written and read bytes, write operations, file counts, and monitoring time.
- Compare actual write sizes with a per-operation 4 KiB rounded estimate.
- Browse write activity by file and folder and inspect the writing processes.
- Estimate daily writes and approximate SSD lifetime from the observed workload.
- Display current and peak event-processing throughput for the current run.
- Save monitoring history between runs using a compact, checksummed version 2
  format; existing version 1 histories remain readable.
- Use the system tray and application log to follow monitoring and driver status.

The lifetime figure is an estimate based on workload and assumed endurance.
Filesystem write totals and the 4 KiB approximation do not measure physical NAND
wear or controller write amplification.

## Requirements

### Build

- Visual Studio 2022 with **Desktop development with C++**.
- MSVC v143 x64 build tools and the matching **C++ MFC** component.
- Windows SDK **10.0.26100.0**, as selected by `SsdReady.vcxproj`, or retarget the
  project to your installed SDK.
- C++20 support. CMake 3.24 or later and Git are additionally needed for the optional tests
  and benchmarks.

The solution and both projects provide **Debug x64** and **Release x64** only.
The solution builds the GUI and driver installer; it does not build or sign the
kernel driver.

### Monitoring

- 64-bit Windows with a compatible, loadable SsdSpy driver.
- Administrator privileges to install, start, and attach the filesystem filter.

The x64 prebuilt driver is in `drivers/x64/ssdspy.sys`. Whether Windows accepts
that binary depends on its driver-signing and security policies; a successful
application build does not validate driver compatibility on a particular system.

## Build the application

Open `SsdReady.sln` in Visual Studio 2022, select **x64**, and build **Debug** or
**Release**. Alternatively, run these commands from the repository root in a
Visual Studio Developer Command Prompt:

```bat
msbuild SsdReady.sln /m /p:Configuration=Debug /p:Platform=x64
msbuild SsdReady.sln /m /p:Configuration=Release /p:Platform=x64
```

Each configuration produces:

| Output | Purpose |
| --- | --- |
| `x64/<Configuration>/SsdReady.exe` | Monitoring application |
| `x64/<Configuration>/driver_installer.exe` | Driver installation/removal tool |

Release uses static MFC and the static C++ runtime. Debug uses the matching
Visual Studio debug runtime and dynamic MFC libraries.

## Run and monitor

After building Release, use an **elevated PowerShell** at the repository root:

```powershell
Copy-Item .\drivers\x64\ssdspy.sys .\x64\Release\ssdspy.sys
& .\x64\Release\driver_installer.exe install
& .\x64\Release\SsdReady.exe
```

The installer expects `ssdspy.sys` beside its executable, copies it to the
Windows drivers directory, and configures and starts the `SsdSpy` service.

1. Use **Menu > Select Drives To Monitor** to select drives.
2. Enable **Collect names of writing processes** in Options if you want process
   attribution. Leave **Do not log file information** unchecked for file details.
3. Click **Start** and run your normal workload. Longer, representative sessions
   give more useful daily-write estimates.
4. Browse the file tree, use **Processes**, or double-click a file/folder label
   to inspect its writers. Use **Menu > Show App Log** to investigate monitoring errors.

Shadow copies and volumes without a resolved drive letter are excluded from
drive selection. Monitoring data is stored locally in
`%APPDATA%\CEZEO software\SsdReady\Monitored.Files`; settings use
`HKCU\Software\CEZEO software\SsdReady`.

New saves use version 2. Loading version 1 does not change the original file;
the next successful save migrates it. Older version 1-only applications cannot
read version 2 files. See [history format details](docs/history-format.md).

To remove the driver, close SsdReady and run from an elevated PowerShell:

```powershell
& .\x64\Release\driver_installer.exe uninstall
```

## Tests and performance tuning

The Google Benchmark executables exercise the same retrieval and processing
functions as the application, using real local process names and synthetic
driver-format records. Build and run the regression suite from the repository
root with CMake and Git available:

```powershell
cmake -S benchmarks -B build/google-benchmark -G "Visual Studio 17 2022" -A x64
cmake --build build/google-benchmark --config Release --parallel 4
ctest --test-dir build/google-benchmark -C Release --output-on-failure
```

Initial configuration downloads the pinned Google Benchmark dependency. See
[benchmark instructions](benchmarks/README.md) for workload sizes, repetitions,
cache modes, history-format measurements, and test coverage. Simulated worker
tests do not install the driver or validate live kernel-driver operation.

## Repository layout

| Location | Contents |
| --- | --- |
| Root C++ sources | MFC interface, monitoring workers, statistics, settings, and history codec |
| `DriverInstaller/` | Driver service installation/removal utility |
| `drivers/` | Prebuilt driver binaries; use `drivers/x64/` with current builds |
| `benchmarks/` | Shared-production-code benchmarks and regression tests |
| `docs/` | History format and measured performance details |
| `res/` | Application icons and other resources |

## License and acknowledgements

Original SsdReady code is released under the [MIT license](LICENSE), copyright
CEZEO software Ltd. Existing third-party notices remain applicable and are
retained in the source:

- `minispy.h` contains Microsoft Corporation's MiniSpy copyright notice.
- `buffered_device_context.h` retains Keith Rule's copyright and permission notice.
- Google Benchmark is a separate test dependency with its own license.
