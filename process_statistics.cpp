// clang-format off
#include "pch.h"
// clang-format on

#include "process_statistics.h"

std::wstring ProcessStatisticsStore::NameKey(const CString& name) {
  std::wstring key(name.GetString(), name.GetLength());
  // Use the same CRT case folding as CString::CompareNoCase.
  _wcslwr_s(key.data(), key.size() + 1);
  return key;
}

std::size_t ProcessStatisticsStore::AddWrite(const CString& name, std::uint64_t written_bytes) {
  auto key = NameKey(name);
  auto found = indexes_by_name_.find(key);
  if (found != indexes_by_name_.end()) {
    records_[found->second].written_bytes += written_bytes;
    return found->second;
  }

  const auto index = records_.size();
  records_.push_back({name, written_bytes, index});
  try {
    indexes_by_name_.emplace(std::move(key), index);
  } catch (...) {
    records_.pop_back();
    throw;
  }
  return index;
}

const ProcessStatistics* ProcessStatisticsStore::Get(std::size_t index) const {
  return index < records_.size() ? &records_[index] : nullptr;
}

void ProcessStatisticsStore::Clear() {
  indexes_by_name_.clear();
  records_.clear();
}

BOOL ProcessStatisticsStore::Save(File& file) const {
  if (!file.WriteSize32(records_.size()))
    return FALSE;
  for (const auto& record : records_) {
    if (!file.Write(&record.written_bytes, sizeof(record.written_bytes)) || !file.WriteSize32(record.process_index) ||
        !file.WriteString(record.name, _MAX_PATH))
      return FALSE;
  }
  return TRUE;
}

BOOL ProcessStatisticsStore::Load(File& file) {
  std::size_t count;
  if (!file.ReadSize32(count))
    return FALSE;
  ProcessStatisticsStore loaded;
  loaded.records_.reserve(count);
  loaded.indexes_by_name_.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    ProcessStatistics record;
    if (!file.Read(&record.written_bytes, sizeof(record.written_bytes)) || !file.ReadSize32(record.process_index) ||
        !file.ReadString(record.name, _MAX_PATH))
      return FALSE;
    if (record.process_index != index) {
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
    }
    // Keep all saved slots, including duplicate names; old lookup chose the first.
    loaded.indexes_by_name_.emplace(NameKey(record.name), index);
    loaded.records_.push_back(std::move(record));
  }
  records_.swap(loaded.records_);
  indexes_by_name_.swap(loaded.indexes_by_name_);
  return TRUE;
}
