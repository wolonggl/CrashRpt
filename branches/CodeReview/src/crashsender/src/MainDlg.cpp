// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include "MainDlg.h"
#include "Utility.h"
#include "tinyxml.h"
#include <atlstr.h>
#include "zip.h"
#include "unzip.h"

BOOL CMainDlg::PreTranslateMessage(MSG* pMsg)
{
	return CWindow::IsDialogMessage(pMsg);
}

BOOL CMainDlg::OnIdle()
{
	return FALSE;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// center the dialog on the screen
	CenterWindow();
	
  // Set window icon
  SetIcon(::LoadIcon(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME)), 0);

  // Load heading icon
  HMODULE hExeModule = LoadLibrary(m_sImageName);
  if(hExeModule)
  {
    // Use IDR_MAINFRAME icon which is the default one for the crashed application.
    m_HeadingIcon = ::LoadIcon(hExeModule, MAKEINTRESOURCE(IDR_MAINFRAME));
  }  

  // If there is no IDR_MAINFRAME icon in crashed EXE module, use IDI_APPLICATION system icon
  if(m_HeadingIcon == NULL)
  {
    m_HeadingIcon = ::LoadIcon(NULL, MAKEINTRESOURCE(IDI_APPLICATION));
  }  
 
  m_link.SubclassWindow(GetDlgItem(IDC_LINK));   
  m_link.SetHyperLinkExtendedStyle(HLINK_COMMANDBUTTON);

  m_linkMoreInfo.SubclassWindow(GetDlgItem(IDC_MOREINFO));
  m_linkMoreInfo.SetHyperLinkExtendedStyle(HLINK_COMMANDBUTTON);

  m_statEmail = GetDlgItem(IDC_STATMAIL);
  m_editEmail = GetDlgItem(IDC_EMAIL);
  m_statDesc = GetDlgItem(IDC_DESCRIBE);
  m_editDesc = GetDlgItem(IDC_DESCRIPTION);
  m_statCrashRpt = GetDlgItem(IDC_CRASHRPT);
  m_statHorzLine = GetDlgItem(IDC_HORZLINE);
  m_btnOk = GetDlgItem(IDOK);
  m_btnCancel = GetDlgItem(IDCANCEL);

  CRect rc1, rc2;
  m_linkMoreInfo.GetWindowRect(&rc1);
  m_statHorzLine.GetWindowRect(&rc2);
  m_nDeltaY = rc1.bottom+15-rc2.top;

  LOGFONT lf;
  memset(&lf, 0, sizeof(LOGFONT));
  lf.lfHeight = 25;
  lf.lfWeight = FW_NORMAL;
  lf.lfQuality = ANTIALIASED_QUALITY;
  _tcscpy_s(lf.lfFaceName, 32, _T("Tahoma"));
  m_HeadingFont.CreateFontIndirect(&lf);

  ShowMoreInfo(FALSE);

  m_dlgProgress.Create(m_hWnd);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	UIAddChildWindowContainer(m_hWnd);

	return TRUE;
}

void CMainDlg::ShowMoreInfo(BOOL bShow)
{
  CRect rc1, rc2;

  m_statEmail.ShowWindow(bShow?SW_SHOW:SW_HIDE);
  m_editEmail.ShowWindow(bShow?SW_SHOW:SW_HIDE);
  m_statDesc.ShowWindow(bShow?SW_SHOW:SW_HIDE);
  m_editDesc.ShowWindow(bShow?SW_SHOW:SW_HIDE);
  
  int k = bShow?-1:1;

  m_statHorzLine.GetWindowRect(&rc1);
  ScreenToClient(&rc1);
  rc1.OffsetRect(0, k*m_nDeltaY);
  m_statHorzLine.MoveWindow(&rc1);

  m_statCrashRpt.GetWindowRect(&rc1);
  ScreenToClient(&rc1);
  rc1.OffsetRect(0, k*m_nDeltaY);
  m_statCrashRpt.MoveWindow(&rc1);

  m_btnOk.GetWindowRect(&rc1);
  ScreenToClient(&rc1);
  rc1.OffsetRect(0, k*m_nDeltaY);
  m_btnOk.MoveWindow(&rc1);

  m_btnCancel.GetWindowRect(&rc1);
  ScreenToClient(&rc1);
  rc1.OffsetRect(0, k*m_nDeltaY);
  m_btnCancel.MoveWindow(&rc1);

  GetClientRect(&rc1);
  rc1.bottom += k*m_nDeltaY;
  ResizeClient(rc1.Width(), rc1.Height());

  if(bShow)
    m_editEmail.SetFocus();
  else
    m_btnOk.SetFocus();
}

LRESULT CMainDlg::OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
  CDCHandle dc((HDC)wParam);

  RECT rcClient;
  GetClientRect(&rcClient);

  RECT rc;
  CStatic statUpperHorzLine = GetDlgItem(IDC_UPPERHORZ);
  statUpperHorzLine.GetWindowRect(&rc);
  ScreenToClient(&rc);

  COLORREF cr = GetSysColor(COLOR_3DFACE);
  CBrush brush;
  brush.CreateSolidBrush(cr);  

  RECT rcHeading = {0, 0, rcClient.right, rc.bottom};
  dc.FillRect(&rcHeading, (HBRUSH)GetStockObject(WHITE_BRUSH));

  RECT rcBody = {0, rc.bottom, rcClient.right, rcClient.bottom};
  dc.FillRect(&rcBody, brush);

  rcHeading.left = 60;
  rcHeading.right -= 10;

  CString sHeading;
  sHeading.Format(_T("%s has stopped working"), m_sAppName);
  dc.SelectFont(m_HeadingFont);
  dc.DrawTextEx(sHeading.GetBuffer(), sHeading.GetLength(), &rcHeading, 
    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);  

  if(m_HeadingIcon)
  {
    ICONINFO ii;
    m_HeadingIcon.GetIconInfo(&ii);
    dc.DrawIcon(16, rcHeading.bottom/2 - ii.yHotspot, m_HeadingIcon);
  }

  return TRUE;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CloseDialog(wID);  
	return 0;
}

LRESULT CMainDlg::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
  CloseDialog(0);  
  return 0;
}

void CMainDlg::CloseDialog(int nVal)
{
	DestroyWindow();
	::PostQuitMessage(nVal);
}

LRESULT CMainDlg::OnLinkClick(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{  
  CDetailDlg dlg;
  dlg.m_pUDFiles = m_pUDFiles;
  dlg.DoModal();
  return 0;
}

LRESULT CMainDlg::OnMoreInfoClick(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
 m_linkMoreInfo.EnableWindow(0);
 ShowMoreInfo(TRUE);
 return 0;
}

LRESULT CMainDlg::OnSend(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{  
  HWND     hWndEmail = GetDlgItem(IDC_EMAIL);
  HWND     hWndDesc = GetDlgItem(IDC_DESCRIPTION);
  int      nEmailLen = ::GetWindowTextLength(hWndEmail);
  int      nDescLen = ::GetWindowTextLength(hWndDesc);

  LPTSTR lpStr = m_sEmailFrom.GetBufferSetLength(nEmailLen+1);
  ::GetWindowText(hWndEmail, lpStr, nEmailLen+1);
  m_sEmailFrom.ReleaseBuffer();

  lpStr = m_sDescription.GetBufferSetLength(nDescLen+1);
  ::GetWindowText(hWndDesc, lpStr, nDescLen+1);
  m_sDescription.ReleaseBuffer();

  //
  // If an email address was entered, verify that
  // it [1] contains a @ and [2] the last . comes
  // after the @.
  //
  if (m_sEmailFrom.GetLength() &&
      (m_sEmailFrom.Find(_T('@')) < 0 ||
       m_sEmailFrom.ReverseFind(_T('.')) < m_sEmailFrom.Find(_T('@'))))
  {
     // alert user
     TCHAR szBuf[256];
     ::LoadString(_Module.GetResourceInstance(), IDS_INVALID_EMAIL, szBuf, 255);
     MessageBox(szBuf, _T("Invalid E-mail address"), MB_OK);
     
     // select email
     ::SetFocus(hWndEmail);

     return 0;
  }

  // Write user email and problem description to XML
  AddUserInfoToCrashDescriptorXML(m_sEmailFrom, m_sDescription);
 
  
  m_ctx.m_hCancelEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  m_ctx.m_nStatus = 0;
  m_ctx.m_nProgressPct = 0;
  m_ctx.m_sZipName = m_sZipName;
  m_ctx.m_sEmailTo = m_sEmailTo;
  m_ctx.m_sEmailFrom = m_sEmailFrom;
  m_ctx.m_sEmailSubject = m_sEmailSubject;
  m_ctx.m_sEmailText = m_sDescription;
  m_ctx.m_sUrl = m_sUrl;
  memcpy(&m_ctx.m_uPriorities, &m_uPriorities, 3*sizeof(UINT));

  DWORD dwThreadId = 0;
  m_hSenderThread = CreateThread(NULL, 0, SenderThread, (LPVOID)&m_ctx, NULL, &dwThreadId);

  ShowWindow(SW_HIDE);
  m_dlgProgress.m_pctx = &m_ctx;
  m_dlgProgress.Start();  
  
  SetTimer(0, 1000, NULL);

  CreateTrayIcon(true, m_hWnd);

  return 0;
}

LRESULT CMainDlg::OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
  if(WaitForSingleObject(m_hSenderThread, 0)==WAIT_OBJECT_0)
  {
    CreateTrayIcon(false, m_hWnd);
    KillTimer(0);
    CloseDialog(0);
  }  
  return 0;
}

void CMainDlg::AddUserInfoToCrashDescriptorXML(CString sEmail, CString sDesc)
{ 
  HZIP hz = CreateZip(m_sZipName, NULL);
  
  TStrStrMap::iterator cur = m_pUDFiles.begin();
  unsigned int i;
  for (i = 0; i < m_pUDFiles.size(); i++, cur++)
  {
    CString sFileName = cur->first;
    sFileName = sFileName.Mid(sFileName.ReverseFind('\\')+1);
    if(sFileName.CompareNoCase(_T("crashrpt.xml"))==0)
    {
      TiXmlDocument doc;
  
      bool bLoad = doc.LoadFile(cur->first);
      if(!bLoad)
        return;

      TiXmlNode* root = doc.FirstChild("CrashRpt");
      if(!root)
        return;

      // Write user e-mail

      TiXmlElement* email = new TiXmlElement("UserEmail");
      root->LinkEndChild(email);

      TiXmlText* email_text = new TiXmlText(CStringA(sEmail));
      email->LinkEndChild(email_text);              

      // Write problem description

      TiXmlElement* desc = new TiXmlElement("ProblemDescription");
      root->LinkEndChild(desc);

      TiXmlText* desc_text = new TiXmlText(CStringA(sDesc));
      desc->LinkEndChild(desc_text);              

      doc.SaveFile();      
    }

    ZRESULT zr = ZipAdd(hz, sFileName, CString(cur->first));
    ATLASSERT(zr==ZR_OK);      
  }  

  CloseZip(hz);
}

LRESULT CMainDlg::OnCtlColorStatic(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
  if((HWND)lParam!=m_statIcon)
    return 0;

  HDC hDC = (HDC)wParam;
  //::SelectObject(hDC, GetStockObject(NULL_BRUSH));
  SetBkColor(hDC, RGB(0, 255, 255));
  SetTextColor(hDC, RGB(0, 255, 255));
  return (LRESULT)TRUE;
}

int CMainDlg::CreateTrayIcon(bool bCreate, HWND hWndParent)
{
  NOTIFYICONDATA nf;
	memset(&nf,0,sizeof(NOTIFYICONDATA));
	nf.cbSize = sizeof(NOTIFYICONDATA);
	nf.hWnd = hWndParent;
	nf.uID = 0;
	
	if(bCreate==true) // add icon to tray
	{
		nf.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		nf.uCallbackMessage = WM_TRAYICON;
		nf.uVersion = NOTIFYICON_VERSION;

		nf.hIcon = LoadIcon(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME));
	  _tcscpy_s(nf.szTip, 128, _T("Sending Error Report"));
		
		Shell_NotifyIcon(NIM_ADD, &nf);
	}
	else // delete icon
	{
		Shell_NotifyIcon(NIM_DELETE, &nf);
	}
  return 0;
}

               