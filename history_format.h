#pragma once

#include <windows.h>

class File;
class FolderTreeItem;
class ProcessStatisticsStore;
struct MonitoringStatistics;

namespace history_format {

// Save version 2; Load accepts versions 1 and 2. Callers supply temporary load
// state and commit only after success, including closing the file.
BOOL Save(File& file, const MonitoringStatistics& statistics, const ProcessStatisticsStore& processes, const FolderTreeItem& root);
BOOL Load(File& file, MonitoringStatistics& statistics, ProcessStatisticsStore& processes, FolderTreeItem& root);

}  // namespace history_format
