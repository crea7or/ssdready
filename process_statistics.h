#pragma once

#include <afxstr.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class File;

struct ProcessStatistics {
  CString name;
  std::uint64_t written_bytes = 0;
  std::size_t process_index = 0;
};

struct ProcessStatisticsSnapshot {
  CString item_name;
  std::uint64_t written_bytes = 0;
  std::vector<ProcessStatistics> processes;
};

// Historical executable-name totals, independent of the transient PID cache.
// The manager's processed-data mutex protects every access to this store.
class ProcessStatisticsStore {
 public:

  std::size_t AddWrite(const CString& name, std::uint64_t written_bytes);
  const ProcessStatistics* Get(std::size_t index) const;

  const std::vector<ProcessStatistics>& records() const {
    return records_;
  }

  void Clear();

  // Preserve version 1: count32, then bytes64, index32, length-prefixed UTF-16 name.
  BOOL Save(File& file) const;
  BOOL Load(File& file);

 private:

  friend class HistoryCodec;

  static std::wstring NameKey(const CString& name);
  std::vector<ProcessStatistics> records_;

  struct NameHash {
    using is_transparent = void;
    std::size_t operator()(std::wstring_view name) const;
  };

  struct NameEqual {
    using is_transparent = void;
    bool operator()(std::wstring_view left, std::wstring_view right) const;
  };

  std::unordered_map<std::wstring, std::size_t, NameHash, NameEqual> indexes_by_name_;
};
