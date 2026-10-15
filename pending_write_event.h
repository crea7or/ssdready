#pragma once

// The PID is resolved by the producer; the consumer needs only names and size.
struct PendingWriteEvent {
  CString file_name;
  CString process_name;
  DWORD64 written_bytes = 0;
};
