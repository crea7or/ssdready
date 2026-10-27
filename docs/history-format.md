# Monitoring history format

New saves use **version 2**. Version 1 remains readable through the original
serializers. A successful save migrates a loaded version 1 history; loading alone
does not modify it. Older builds that only understand version 1 cannot read
version 2 files. The manager writes `Monitored.Files.tmp`, closes it, and
replaces `Monitored.Files` only after success. Invalid loads retain the previous
in-memory model and disable saving until a successful load or explicit reset.
After encoding, the manager checks `FlushFileBuffers` through `File::Flush()`
before closing and replacing the destination. A flush failure retains the old
history. The cached-I/O codec timings below do not include this durability flush.

## Version 2 layout

All fixed-width integers are little endian. The 68-byte header contains:

- `uint32` version = 2.
- The existing seven `uint64` monitoring statistics, in their original order.
- `uint32` marker = `0x32524853` (`SHR2`).
- CRC32 of the 56 statistics bytes.

The payload is a sequence of blocks, each prefixed by three `uint32` values:
**decoded length, stored length, decoded CRC32**. Decoded lengths are at most
1 MiB. When stored length equals decoded length, the block is raw; otherwise it
uses Windows XPRESS buffer-mode compression (algorithm 3). Blocks that would
expand are written raw. Three zero integers terminate the stream; extra trailing
bytes are rejected. The compression API is loaded from the Windows system
`cabinet.dll`. If unavailable, saves use raw compact blocks; reading compressed
blocks requires Windows 8 or newer. No third-party library is needed. See
[Microsoft's compression API documentation](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-createcompressor).

Inside blocks, integers use canonical unsigned LEB128, with 64-bit counters.
Strings store `(UTF-16 code-unit count << 1) | wide`: `wide=0` uses one byte per
Latin-1 code unit; `wide=1` stores the original UTF-16 code units. This preserves
Unicode, surrogate pairs, unpaired surrogates and embedded NULs exactly, without
locale conversion. String lengths are limited to 32768 code units.

The process table stores its count, then each executable name and write-byte
counter. The table position is the process index, so no redundant index is
stored. Duplicate historical process slots and their ordering are retained.

The tree is stored in preorder. Each node contains:

- Flags: bit 0 means folder, bit 1 means its rounded counter is encoded in 4 KiB
  units (otherwise it is the exact byte count).
- Alias, direct write-byte count, rounded count.
- Direct process-link count, then each process index and write-byte count.
- Child count and child nodes.

Cached subtree totals are rebuilt during loading rather than stored. Object
version fields and four-byte child-type tags from version 1 are omitted.
Process references, tags, string lengths, varint overflow, subtree overflow,
file-node child counts, nesting above 256 levels, checksums, and truncation are
validated. Counts and process indices remain bounded by `UINT32_MAX`; internal
container sizes use `size_t`. Untrusted counts are not reserved in full before
decoding.

The codec uses bounded block buffers rather than holding another complete copy
of the file. Saving overlaps encoding with one compression/write task at a time,
using three 1 MiB buffers. Small blocks are written synchronously; failure to
start a background task falls back to synchronous writing. The final task is
joined and its error checked before writing the end marker or closing the file.
Loading uses two 1 MiB codec buffers plus the existing file read buffer.
These buffer counts exclude the compression API's internal workspace and model
storage. The codec is shared by the app and regression/measurement tool.

## Tests and measurements

`history_format_test` checks version 1 compatibility and version 2 round trips,
raw and compressed blocks, block boundaries, process ordering, Unicode,
64-bit counters, truncation, corruption, and repaired parent links. Manager
tests check version 1 migration and that failed loads/saves preserve existing data.

Build the usual benchmark project, then compare three runs on an existing file:

```powershell
build\google-benchmark\Release\history_format_test.exe "C:\path\Monitored.Files" "C:\Sources\ssdready\build\history-format-measurement"
```

The input is read only. The output directory receives `history-v1.files` and
`history-v2.files`. Measurements include codec and buffered/native file I/O,
but exclude model destruction, UI work, manager locks and final file replacement.
Repeated loads generally use the Windows file cache. Each loaded model is
checked against a fingerprint of the full normalized legacy serialization.

### Measured comparison, 2026-10-09

VS2022/v143, x64 Release, supplied version 1 history, 153 process slots.
The input remained unchanged (272,837,290 bytes, original modification time).
Three runs with Windows file caching and full model equality checks:

| Run | V1 save (s) | V2 save (s) | V1 load (s) | V2 load (s) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0.310355 | 0.194555 | 1.115060 | 0.860475 |
| 2 | 0.408226 | 0.287481 | 1.171520 | 0.884807 |
| 3 | 0.434298 | 0.284352 | 1.207690 | 0.905447 |
| Mean | 0.384293 | 0.255463 | 1.164760 | 0.883576 |

Version 2 output: **22,741,714 bytes**, versus **272,837,290 bytes** for
version 1: **91.7% smaller**. Mean save time improved **33.5%** and mean load time
improved **24.1%**. These measurements are specific to this file and machine and
exclude the final atomic replacement, UI work and model destruction.

Release/Debug application builds passed for x64 and Win32. Both architecture
regression suites passed all 17 runnable cases; the native drive-access case
was skipped. Format tests include synchronous and background write failures.
