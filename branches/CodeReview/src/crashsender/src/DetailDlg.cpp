#include "stdafx.h"
#include "DetailDlg.h"

// Defines list control column attributes
LVCOLUMN _ListColumns[] =
{
   /*
   {
      mask,
      fmt,
      cx,
      pszText,
      cchTextMax,
      iSubItem,
      iImage,
      iOrder
   }
   */
   {  // Column 1: File name
      LVCF_FMT | LVCF_ORDER | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH,
      LVCFMT_LEFT, 
      114, 
      (LPTSTR)IDS_NAME, 
      0, 
      0, 
      0, 
      0
   },
   {  // Column 2: File description
      LVCF_FMT | LVCF_ORDER | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH, 
      LVCFMT_LEFT, 
      150, 
      (LPTSTR)IDS_DESC, 
      0, 
      1, 
      0, 
      1
   },
   {  // Column 3: File type
      LVCF_FMT | LVCF_ORDER | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH, LVCFMT_LEFT, 
      100, 
      (LPTSTR)IDS_TYPE, 
      0, 
      2, 
      0, 
      2
   },
   {  // Column 4: File size
      LVCF_FMT | LVCF_ORDER | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH, 
      LVCFMT_RIGHT, 
      100, 
      (LPTSTR)IDS_SIZE, 
      0, 
      3, 
      0, 
      3
   },
};

LRESULT CDetailDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    int i = 0;

   // Add "About..." menu item to system menu.

   // IDM_ABOUTBOX must be in the system command range.
    ATLASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX); 
    ATLASSERT(IDM_ABOUTBOX < 0xF000); 

    /*CMenu sysMenu;
    sysMenu.Attach(GetSystemMenu(FALSE));
    if (sysMenu.IsMenu())
    {
	   CString strAboutMenu;
	   strAboutMenu.LoadString(IDS_ABOUTBOX);
	   if (!strAboutMenu.IsEmpty())
	   {
          sysMenu.AppendMenu(MF_SEPARATOR);
		   sysMenu.AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
	   }
   }*/

    // center the dialog on the screen
	CenterWindow();

    CListViewCtrl list;
    list.Attach(GetDlgItem(IDC_FILE_LIST));

    // Turn on full row select
    ListView_SetExtendedListViewStyle(list.m_hWnd, LVS_EX_FULLROWSELECT);

    //
    // Attach the system image list to the list control.
    //
    /*SHFILEINFO sfi = {0};

    HIMAGELIST hil = (HIMAGELIST)SHGetFileInfo(
                                  NULL,
                                  0,
                                  &sfi,
                                  sizeof(sfi),
                                  SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

    if (NULL != hil)
    {
       m_iconList.Attach(hil);
       list.SetImageList(m_iconList, LVSIL_SMALL);
    }*/

    m_iconList.Create(16, 16, ILC_COLOR32|ILC_MASK, 3, 1);
    list.SetImageList(m_iconList, LVSIL_SMALL);

    //
    // Add column headings
    //
    for (i = 0; i < sizeof(_ListColumns) / sizeof(LVCOLUMN); i++)
    {
       list.InsertColumn(
          i, 
          CString(_ListColumns[i].pszText), 
          _ListColumns[i].fmt, 
          _ListColumns[i].cx, 
          _ListColumns[i].iSubItem);
    }

    //
    // Insert items
    //
    WIN32_FIND_DATA   findFileData   = {0};
    HANDLE            hFind          = NULL;
    CString           sSize;
    LVITEM            lvi            = {0};
    TStrStrMap::iterator p;
    for (i = 0, p = m_pUDFiles.begin(); p != m_pUDFiles.end(); p++, i++)
    {      
      SHFILEINFO sfi;
       SHGetFileInfo(
          CString(p->first),
          0,
          &sfi,
          sizeof(sfi),
          SHGFI_DISPLAYNAME | SHGFI_ICON | SHGFI_TYPENAME | SHGFI_SMALLICON);

       int iImage = -1;
       if(sfi.hIcon)
       {
         iImage = m_iconList.AddIcon(sfi.hIcon);
         DestroyIcon(sfi.hIcon);
       }

       // Name
       lvi.mask          = LVIF_IMAGE | LVIF_TEXT;
       lvi.iItem         = i;
       lvi.iSubItem      = 0;
       lvi.iImage        = iImage;
       lvi.pszText       = sfi.szDisplayName;
       list.InsertItem(&lvi);

       // Description
       list.SetItemText(i, 1, CString(p->second));

       // Type
       list.SetItemText(i, 2, CString(sfi.szTypeName));

       // Size
       hFind = FindFirstFile(CString(p->first), &findFileData);
       if (INVALID_HANDLE_VALUE != hFind)
       {
         FindClose(hFind);
          sSize.Format(TEXT("%d KB"), findFileData.nFileSizeLow);
          list.SetItemText(i, 3, sSize);
       }
    }

    // Select first file
    ListView_SetItemState(
       GetDlgItem(IDC_FILE_LIST), 
       0, 
       LVIS_SELECTED, 
       LVIS_SELECTED);

    list.Detach();

    return TRUE;
}

LRESULT CDetailDlg::OnCtlColor(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
  LRESULT res = 0;
  if ((HWND)lParam == GetDlgItem(IDC_FILE_EDIT))
     res = (LRESULT)GetSysColorBrush(COLOR_WINDOW);

  bHandled = TRUE;

  return res;
}

LRESULT CDetailDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
  LPNMLISTVIEW lpItem           = (LPNMLISTVIEW)pnmh; 
  int iItem                     = lpItem->iItem;

  if (lpItem->uChanged & LVIF_STATE
     && lpItem->uNewState & LVIS_SELECTED)
  {
     SelectItem(iItem);
  }

  return 0;
}

LRESULT CDetailDlg::OnItemDblClicked(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
  LPNMLISTVIEW lpItem           = (LPNMLISTVIEW)pnmh; 
  int iItem                     = lpItem->iItem;
  DWORD_PTR dwRet               = 0;

  if (iItem < 0 || (int)m_pUDFiles.size() < iItem)
     return 0;

  TStrStrMap::iterator p = m_pUDFiles.begin();
  for (int i = 0; i < iItem; i++, p++);

  dwRet = (DWORD_PTR)::ShellExecute(
                          0, 
                          _T("open"), 
                          CString(p->first),
                          0, 
                          0, 
                          SW_SHOWNORMAL
                          );
  ATLASSERT(dwRet > 32);

  return 0;
}

void CDetailDlg::SelectItem(int iItem)
{
  const int MAX_FILE_SIZE          = 32768; // 32k file preview max
  DWORD dwBytesRead                = 0;
  char buffer[MAX_FILE_SIZE + 1]  = "";

  // Sanity check
  if (iItem < 0 || (int)m_pUDFiles.size() < iItem)
      return;

  TStrStrMap::iterator p = m_pUDFiles.begin();
  for (int i = 0; i < iItem; i++, p++);

  // 
  // Update preview header info
  //
  ::SetWindowText(GetDlgItem(IDC_NAME), CString(p->first));
  ::SetWindowText(GetDlgItem(IDC_DESCRIPTION), CString(p->second));

  //
  // Display file contents in preview window
  //
  HANDLE hFile = CreateFile(
     CString(p->first),
     GENERIC_READ,
     FILE_SHARE_READ | FILE_SHARE_WRITE,
     NULL,
     OPEN_EXISTING,
     FILE_ATTRIBUTE_NORMAL,
     0);

  if (NULL != hFile)
  {
     // Read up to first 32 KB
     ReadFile(hFile, buffer, MAX_FILE_SIZE, &dwBytesRead, 0);
     buffer[dwBytesRead] = 0;
     CloseHandle(hFile);
  }

  // Update edit control with file contents
  ::SetWindowTextA(GetDlgItem(IDC_FILE_EDIT), buffer);
}

LRESULT CDetailDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
  EndDialog(0);
  return 0;
}