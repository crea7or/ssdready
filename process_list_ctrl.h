#pragma once

#include "process_statistics.h"
#include "sortable_list_ctrl.h"

// Owns UI rows so manager updates and cache eviction cannot invalidate lParam.
class ProcessListCtrl : public SortableListCtrl {
 public:

  void SetProcesses(std::vector<ProcessStatistics> processes);
  void SortAllItems() override;

 private:

  struct Row {
    ProcessStatistics statistics;
    CString formatted_bytes;
  };

  std::vector<Row> rows_;

 protected:

  afx_msg void OnGetdispinfo(NMHDR* header, LRESULT* result);
  DECLARE_MESSAGE_MAP()
};
