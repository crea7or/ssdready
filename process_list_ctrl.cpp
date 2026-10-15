// clang-format off
#include "pch.h"
// clang-format on

#include "process_list_ctrl.h"


BEGIN_MESSAGE_MAP(ProcessListCtrl, SortableListCtrl)
ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
END_MESSAGE_MAP()

void ProcessListCtrl::SetProcesses(std::vector<ProcessStatistics> processes) {
  SetRedraw(FALSE);
  DeleteAllItems();
  rows_.clear();
  rows_.reserve(processes.size());
  for (auto& process : processes) {
    Row row{std::move(process), {}};
    row.formatted_bytes = string_utils::FormatBytes(row.statistics.written_bytes);
    rows_.push_back(std::move(row));
  }
  for (auto& row : rows_) {
    LVITEM item{};
    item.mask = LVIF_PARAM | LVIF_TEXT;
    item.iItem = GetItemCount();
    item.pszText = LPSTR_TEXTCALLBACK;
    item.lParam = reinterpret_cast<LPARAM>(&row);
    InsertItem(&item);
  }
  SortAllItems();
  SetRedraw(TRUE);
  Invalidate();
}

void ProcessListCtrl::OnGetdispinfo(NMHDR* header, LRESULT* result) {
  auto& item = reinterpret_cast<NMLVDISPINFO*>(header)->item;
  const auto* row = reinterpret_cast<const Row*>(item.lParam);
  if (row && (item.mask & LVIF_TEXT)) {
    const CString& text = item.iSubItem == 0 ? row->statistics.name : row->formatted_bytes;
    // Let the list copy the text; the statistics strings remain read-only.
    if (item.pszText && item.cchTextMax > 0)
      wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax), text.GetString(), _TRUNCATE);
  }
  *result = 0;
}

void ProcessListCtrl::SortAllItems() {
  SortItems(
      [](LPARAM left, LPARAM right, LPARAM) -> int {
        const auto& first = reinterpret_cast<const Row*>(left)->statistics;
        const auto& second = reinterpret_cast<const Row*>(right)->statistics;
        if (first.written_bytes != second.written_bytes)
          return first.written_bytes > second.written_bytes ? -1 : 1;
        return first.name.CompareNoCase(second.name);
      },
      0);
}
