# Application audit and first fixes (2026-10-26)

Findings were checked against the current source and tracked-file inventory.
The statuses below distinguish implemented fixes from work that remains. No
driver binary was modified or re-signed.

## Functional findings

| Finding | Verdict | Action |
| --- | --- | --- |
| Scientific-notation lifetime and mismatch with aligned-write tooltip | Confirmed | Fixed-point formatting; the estimate now uses rounded write bytes through the shared, tested estimate helper. |
| Drive preferences reconstructed from actual attachments at shutdown | Confirmed | Preferences are recorded when the selection is accepted, independently of driver success. Shutdown only detaches. Legacy empty means all; `-` means explicitly no drives. |
| HKCU Run entry for an elevated application | Confirmed | Replaced with a per-user interactive logon task at highest privileges; remove the legacy Run value after success. No password or SYSTEM account is used. |
| No durable flush before replacing history | Confirmed | Check `File::Flush()` after the codec finishes and before close/replace. A failed flush prevents replacement. This is a device-flush request, not a guarantee against faulty hardware caches. |
| First pending volume snapshot discards newer ones | Confirmed | Atomic exchange publishes the newest snapshot and reclaims the superseded one. The retrieval thread still owns its active snapshot. |
| Failed filesystem probe permanently turns a directory into a file | Confirmed | Removed the probe. Ancestors are structural folders; an uncertain leaf promotes in place when descendants arrive, keeping its counters, process links, UI pointer and cached address. A terminal directory with no descendants cannot be identified from this protocol alone. |

The Task Scheduler path is tested through an injected boundary so regression
tests never register, run or delete a real task. Actual elevated registration
and launch at interactive sign-in still require an opt-in Windows integration
check. The application manifest continues to require administrator privileges.

## Performance findings

| Finding | Verdict | Action |
| --- | --- | --- |
| Refresh walks unchanged visible subtrees under the model lock | Confirmed | Dirty flags now prune unchanged subtrees. Existing-write changes dirty their ancestors too; option changes request an explicit full resort. |
| GetFileAttributesW under the processing lock | Confirmed | Removed. Path processing performs no filesystem metadata query. |
| Allocated path key on every cache lookup | Confirmed | Transparent `wstring_view` lookup; allocate an owned key only on a miss. Cache at most 65,536 paths; clearing this accelerator does not remove historical nodes. |
| Allocated/lowercased process key on each write | Confirmed | Transparent case-insensitive lookup; allocate an owned key only for a new process. |
| Heap allocation for each pending event object | Confirmed, with qualification | Batches now store events by value. `CString` may share cached process-name storage, so three independent allocations per event were not guaranteed. |
| Unlimited log and repeated driver memory warnings | Confirmed | Retain 4,096 messages, cap message body length, maintain stable incremental cursors, and update/rotate the viewer accordingly. Driver memory warnings are aggregated and throttled to once per five seconds. |
| Unlimited historical tree/file-name retention | Confirmed; deferred | The tree intentionally retains historical information. A retention or truncation policy needs separate design; these fixes do not silently delete historical details. Use “Do not log file information” for totals-only monitoring meanwhile. |
| Full process snapshot every second for System | Confirmed | PID 4 resolves directly to System without repeated OS queries. Other unpinned fallback names keep their short expiry to avoid extending stale PID attribution. Broader fallback tuning remains open. |
| History loads synchronously before the initial window appears | Confirmed; deferred | Startup still loads synchronously. Moving parsing to a worker requires an explicit UI-safe commit/cancellation lifecycle and remains separate work. |

The model mutex is now recursive because native tree insertion/sort callbacks
can synchronously re-enter label retrieval on the UI thread. Those callbacks
now lock the model. The queue and drive mutexes remain nonrecursive.

## Correctness and UI findings

| Finding | Verdict | Action |
| --- | --- | --- |
| Unlocked label/child/counter access | Confirmed | Lock display and deletion callbacks, including reentrant native callbacks. x64 does not make unsynchronized container access safe. |
| Total files includes folders and roots | Confirmed | Count leaves only, subtract an inferred file when it becomes a folder, and rebuild the count when loading old history. |
| Existing-write and sort-option changes leave order stale | Confirmed | Mark existing-write ancestors dirty; refresh sorts from timer notifications and explicitly refresh after options change. |
| Explorer restart loses tray icon | Confirmed | Handle registered TaskbarCreated, allow that message through the elevated process's filter, and restore the icon. |
| Escape closes monitoring | Confirmed | Escape is ignored; explicit window close still shuts down normally. |
| Worker retry uses stale startup drive choices | Confirmed | Drive-dialog acceptance updates the same preference used for retry, even when attachment fails. |
| First-enumerated volume determines default SSD size | Confirmed | Select the Windows system drive rather than the first enumerated volume. |
| HTTP help/measure links | Confirmed | Switch application links to HTTPS. |
| Committed build outputs | Confirmed | Stop tracking x64 application/installer executables and driver copies; ignore output directories. Keep the canonical signed driver assets under `drivers/`. |

## Driver-dependent questions

- **Read totals:** the application accepts `ReadDataLength` only on accepted
  write records and ignores separate read records. This is confirmed code
  behavior, but the binary driver's meaning for that field is unknown. Adding
  both could double-count if it is an accumulated delta. Capture/decode live
  driver output or obtain its source before changing this contract.
- **Background paging writes:** the existing synchronous-paging filter does not
  exclude every paging write. Duplicate accounting is plausible but unproven;
  blanket filtering could also discard real writes. Driver instrumentation is
  needed before changing the primary byte counters.

Relevant Microsoft documentation:

- [Task security contexts and highest privileges](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks)
- [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
- [System-buffered I/O and close behavior](https://learn.microsoft.com/en-us/windows/win32/fileio/flushing-system-buffered-i-o-data-to-disk)

## Validation

Regression coverage adds newest-snapshot publication, deleted/long structural
paths, stable folder promotion, file counts, readable lifetime estimates,
scheduled-task boundary errors, explicit no-drive selection, bounded logging
and viewer rotation, and System PID resolution. Existing history round-trip,
malformed-file, worker shutdown, concurrent-state and native UI tests are also
run. See the implementation report for the actual build/test results; live
driver and real sign-in behavior are separate integration checks.

Validation on 2026-10-26: VS2022/v143 Debug x64 and Release x64 application and
installer builds succeeded with zero warnings/errors. Release x64 CTest passed
17 cases; the privileged native-drive enumeration case was skipped. Native tree
regressions cover existing-write and option-driven sorting, and the log-viewer
test covers rotation at its retention limit. No actual sign-in, Explorer restart,
power-loss simulation, or live driver session was performed. Existing history
format tests cover version 1 compatibility and version 2 round trips.
