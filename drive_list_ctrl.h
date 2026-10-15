#pragma once

#include "drive_info.h"
#include "sortable_list_ctrl.h"

class DriveListCtrl : public SortableListCtrl {
 public:

  DriveListCtrl(void);
  ~DriveListCtrl(void) override;

  BOOL AddDrive(const DriveInfo& drive, BOOL selected = FALSE);
  void SortAllItems() override;

  void AttachList();

 protected:

  CImageList drives_image_list_;
  afx_msg void OnGetdispinfo(NMHDR* header, LRESULT* result);
  afx_msg void OnClick(NMHDR* header, LRESULT* result);
  afx_msg void OnCustomdrawList(NMHDR* header, LRESULT* result);

  DECLARE_MESSAGE_MAP()
};
