#pragma once

#include <cstddef>
#include <cstdint>

class File;

// Stored by value in a tree node. The disk index remains 32 bits on both platforms.
struct ProcessWriteStatistics {
  std::uint64_t written_bytes = 0;
  std::size_t process_index = 0;
  BOOL Save(File* file) const;
  BOOL Load(File* file);
};
