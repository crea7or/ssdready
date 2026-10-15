// clang-format off
#include "pch.h"
// clang-format on
#include "process_write_statistics.h"

BOOL ProcessWriteStatistics::Save(File* file) const {
  auto bytes = written_bytes;
  return file->Write(&bytes, sizeof(bytes)) && file->WriteSize32(process_index);
}

BOOL ProcessWriteStatistics::Load(File* file) {
  std::uint64_t bytes;
  std::size_t index;
  if (!file->Read(&bytes, sizeof(bytes)) || !file->ReadSize32(index))
    return FALSE;
  written_bytes = bytes;
  process_index = index;
  return TRUE;
}
