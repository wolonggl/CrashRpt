/************************************************************************************* 
  This file is a part of CrashRpt library.

  Copyright (c) 2003, Michael Carruth
  All rights reserved.
 
  Redistribution and use in source and binary forms, with or without modification, 
  are permitted provided that the following conditions are met:
 
   * Redistributions of source code must retain the above copyright notice, this 
     list of conditions and the following disclaimer.
 
   * Redistributions in binary form must reproduce the above copyright notice, 
     this list of conditions and the following disclaimer in the documentation 
     and/or other materials provided with the distribution.
 
   * Neither the name of the author nor the names of its contributors 
     may be used to endorse or promote products derived from this software without 
     specific prior written permission.
 

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
  SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT 
  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************/

#include "stdafx.h"
#include "ErrorReportSender.h"
#include "MailMsg.h"
#include "smtpclient.h"
#include "HttpRequestSender.h"
#include "CrashRpt.h"
#include "md5.h"
#include "Utility.h"
#include "zip.h"
#include "CrashInfoReader.h"
#include "strconv.h"
#include "ScreenCap.h"
#include "base64.h"
#include <sys/stat.h>
#include "dbghelp.h"

// Globally accessible object
CErrorReportSender g_ErrorReportSender;

CErrorReportSender::CErrorReportSender()
{
  m_hThread = NULL;
  m_SendAttempt = 0;
  m_Action=COLLECT_CRASH_INFO;
  m_bExport = FALSE;
  m_nCurReport = 0;
}

CErrorReportSender::~CErrorReportSender()
{
}

int CErrorReportSender::GetCurReport()
{
  return m_nCurReport;
}

BOOL CErrorReportSender::SetCurReport(int nCurReport)
{
  if(nCurReport<0 || nCurReport>=g_CrashInfo.GetReportCount())
  {
    ATLASSERT(0);
    return FALSE;
  }
  m_nCurReport = nCurReport;
  return TRUE;
}

// This method does crash files collection and
// error report sending work
BOOL CErrorReportSender::DoWork(int action)
{
  m_Action = action;

  // Create worker thread which will do all work assynchroniously
  m_hThread = CreateThread(NULL, 0, WorkerThread, (LPVOID)this, 0, NULL);
  
  // Check if the thread was created ok
  if(m_hThread==NULL)
    return FALSE;

  return TRUE;
}

// This method is the worker thread procedure that delegates further work 
// back to the CErrorReportSender class
DWORD WINAPI CErrorReportSender::WorkerThread(LPVOID lpParam)
{
  // Delegate the action to the CErrorReportSender::DoWorkAssync() method
  CErrorReportSender* pSender = (CErrorReportSender*)lpParam;
  pSender->DoWorkAssync();

  return 0;
}

void CErrorReportSender::UnblockParentProcess()
{
  // Notify the parent process that we have finished with minidump,
  // so the parent process is able to unblock and terminate itself.
  CString sEventName;
  
  sEventName.Format(_T("Local\\CrashRptEvent_%s"), g_CrashInfo.GetReport(0).m_sCrashGUID);
  HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, sEventName);
  if(hEvent!=NULL)
    SetEvent(hEvent);
}

// This method collects required crash files (minidump, screenshot etc.)
// and then sends the error report over the Internet.
void CErrorReportSender::DoWorkAssync()
{
  m_Assync.Reset();
  
  if(g_CrashInfo.m_bSendRecentReports)
  {
    CString sMsg;
    sMsg.Format(_T(">>> Performing actions with error report: '%s'"), 
                g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName);
    m_Assync.SetProgress(sMsg, 0, false);
  }

  if(m_Action&COLLECT_CRASH_INFO)
  {
    m_Assync.SetProgress(_T("Start collecting information about the crash..."), 0, false);
    
    // First take a screenshot of user's desktop (if needed).
    TakeDesktopScreenshot();

    if(m_Assync.IsCancelled())
    {      
      UnblockParentProcess();
      // Delete report files
      Utility::RecycleFile(g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, true);  
      m_Assync.SetProgress(_T("[exit_silently]"), 0, false);
      return;
    }

    // Create crash dump.
    CreateMiniDump();

    if(m_Assync.IsCancelled())
    {      
      UnblockParentProcess();
      // Delete report files
      Utility::RecycleFile(g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, true);  
      m_Assync.SetProgress(_T("[exit_silently]"), 0, false);
      return;
    }

    // Notify the parent process that we have finished with minidump,
    // so the parent process is able to unblock and terminate itself.
    UnblockParentProcess();
    
    // Copy user-provided files.
    CollectCrashFiles();

    if(m_Assync.IsCancelled())
    {      
      // Delete report files
      Utility::RecycleFile(g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, true);  
      m_Assync.SetProgress(_T("[exit_silently]"), 0, false);
      return;
    }

    m_Assync.SetProgress(_T("[confirm_send_report]"), 100, false);
  }
  
  if(m_Action&COMPRESS_REPORT)
  { 
    // Compress error report files
    BOOL bCompress = CompressReportFiles(g_CrashInfo.GetReport(m_nCurReport));
    if(!bCompress)
    {
      m_Assync.SetProgress(_T("[status_failed]"), 100, false);
      return; // Error compressing files
    }
  }

  if(m_Action&RESTART_APP)
  { 
    // Restart the application
    RestartApp();
  }
  
  if(m_Action&SEND_REPORT)
  {
    // Send the error report.
    SendReport();
  }

  // Done
  return;
}

void CErrorReportSender::SetExportFlag(BOOL bExport, CString sExportFile)
{
  m_bExport = bExport;
  m_sExportFileName = sExportFile;
}

// This method blocks until worker thread is exited
void CErrorReportSender::WaitForCompletion()
{
  WaitForSingleObject(m_hThread, INFINITE);
}

void CErrorReportSender::GetStatus(int& nProgressPct, std::vector<CString>& msg_log)
{
  m_Assync.GetProgress(nProgressPct, msg_log); 
}

void CErrorReportSender::Cancel()
{
  m_Assync.Cancel();
}

void CErrorReportSender::FeedbackReady(int code)
{
  m_Assync.FeedbackReady(code);
}


// This takes the desktop screenshot (screenshot of entire virtual screen
// or screenshot of the main window). 
BOOL CErrorReportSender::TakeDesktopScreenshot()
{
  CScreenCapture sc;
  std::vector<CString> screenshot_names;
   
  m_Assync.SetProgress(_T("[taking_screenshot]"), 0);    

  if(!g_CrashInfo.m_bAddScreenshot)
  {
    m_Assync.SetProgress(_T("Desktop screenshot generation disabled; skipping."), 0);    
    return TRUE;
  }

  m_Assync.SetProgress(_T("Taking desktop screenshot"), 0);    
  
  DWORD dwFlags = g_CrashInfo.m_dwScreenshotFlags;

  if(dwFlags==CR_AS_VIRTUAL_SCREEN)
  {
    // Take screenshot of entire desktop
    CRect rcScreen;
    sc.GetScreenRect(&rcScreen);
    
    BOOL bTakeScreenshot = sc.CaptureScreenRect(rcScreen, g_CrashInfo.m_ptCursorPos,
      g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, 0, screenshot_names);
    if(bTakeScreenshot==FALSE)
    {
      return FALSE;
    }
  }
  else if(dwFlags==CR_AS_MAIN_WINDOW)
  {     
    // Take screenshot of the main window
    CRect rcWindow = g_CrashInfo.m_rcAppWnd; 
    if(rcWindow.IsRectEmpty())
    {
      return FALSE;
    }

    BOOL bTakeScreenshot = sc.CaptureScreenRect(rcWindow, g_CrashInfo.m_ptCursorPos,
      g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, 0, screenshot_names);
    if(bTakeScreenshot==FALSE)
    {      
      return FALSE;
    }
  }
  else
  {    
    // Invalid flags
    ATLASSERT(0);
    return FALSE;
  }

  // Prepare the list of screenshot files we will add to the error report
  std::vector<ERIFileItem> FilesToAdd;
  size_t i;
  for(i=0; i<screenshot_names.size(); i++)
  {
    CString sDestFile;
    sDestFile.Format(_T("screenshot%d.png"), i); 
    ERIFileItem fi;
    fi.m_sSrcFile = screenshot_names[i];
    fi.m_sDestFile = sDestFile;
    fi.m_sDesc = _T("Desktop Screenshot");    
    FilesToAdd.push_back(fi);
  }

  // Add the list of PNG files to the error report
  int nAdd = g_CrashInfo.AddFilesToCrashDescriptionXML(FilesToAdd);
  if(nAdd!=0)
  {
    return FALSE;
  }

  // Done
  return TRUE;
}

// This callbask function is called by MinidumpWriteDump
BOOL CALLBACK CErrorReportSender::MiniDumpCallback(
  PVOID CallbackParam,
  PMINIDUMP_CALLBACK_INPUT CallbackInput,
  PMINIDUMP_CALLBACK_OUTPUT CallbackOutput )
{
  // Delegate back to the CErrorReportSender
  CErrorReportSender* pErrorReportSender = (CErrorReportSender*)CallbackParam;  
  return pErrorReportSender->OnMinidumpProgress(CallbackInput, CallbackOutput);  
}

// This method is called when MinidumpWriteDump notifies us about
// currently performed action
BOOL CErrorReportSender::OnMinidumpProgress(const PMINIDUMP_CALLBACK_INPUT CallbackInput,
                PMINIDUMP_CALLBACK_OUTPUT CallbackOutput)
{
  switch(CallbackInput->CallbackType)
  {
  case CancelCallback: 
    {
      // This callback allows to cancel minidump generation
      if(m_Assync.IsCancelled())
      {
        CallbackOutput->Cancel = TRUE;      
        m_Assync.SetProgress(_T("Dump generation cancelled by user"), 0, true);
      }
    }
    break;
  
  case ModuleCallback:
    {
      // We are currently dumping some module
      strconv_t strconv;
      CString sMsg;
      sMsg.Format(_T("Dumping info for module %s"), 
        strconv.w2t(CallbackInput->Module.FullPath));
      m_Assync.SetProgress(sMsg, 0, true);
    }
    break;
  case ThreadCallback:
    {      
      // We are currently dumping some thread 
      CString sMsg;
      sMsg.Format(_T("Dumping info for thread 0x%X"), 
        CallbackInput->Thread.ThreadId);
      m_Assync.SetProgress(sMsg, 0, true);
    }
    break;
  
  }

  return TRUE;
}
  
// This method creates minidump of the process
BOOL CErrorReportSender::CreateMiniDump()
{   
  BOOL bStatus = FALSE;
  HMODULE hDbgHelp = NULL;
  HANDLE hFile = NULL;
  MINIDUMP_EXCEPTION_INFORMATION mei;
  MINIDUMP_CALLBACK_INFORMATION mci;
  CString sMinidumpFile = g_CrashInfo.GetReport(m_nCurReport).
    m_sErrorReportDirName + _T("\\crashdump.dmp");
  std::vector<ERIFileItem> files_to_add;
  ERIFileItem fi;
  BOOL bAdd = FALSE;

  if(g_CrashInfo.m_bGenerateMinidump==FALSE)
  {
    m_Assync.SetProgress(_T("Crash dump generation disabled; skipping."), 0, false);
    return TRUE;
  }

  m_Assync.SetProgress(_T("Creating crash dump file..."), 0, false);
  m_Assync.SetProgress(_T("[creating_dump]"), 0, false);

  // Load dbghelp.dll
  hDbgHelp = LoadLibrary(g_CrashInfo.m_sDbgHelpPath);
  if(hDbgHelp==NULL)
  {
    //try again ... fallback to dbghelp.dll in path
    const CString sDebugHelpDLL_name = "dbghelp.dll";
    hDbgHelp = LoadLibrary(sDebugHelpDLL_name);    
  }

  if(hDbgHelp==NULL)
  {
    m_Assync.SetProgress(_T("dbghelp.dll couldn't be loaded."), 0, false);
    goto cleanup;
  }

  // Create the minidump file
  hFile = CreateFile(
    sMinidumpFile,
    GENERIC_WRITE,
    0,
    NULL,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    NULL);

  if(hFile==INVALID_HANDLE_VALUE)
  {
    DWORD dwError = GetLastError();
    CString sMsg;    
    sMsg.Format(_T("Couldn't create minidump file: %s"), 
      Utility::FormatErrorMsg(dwError));
    m_Assync.SetProgress(sMsg, 0, false);
    return FALSE;
  }

  // Set valid dbghelp API version  
  typedef LPAPI_VERSION (WINAPI* LPIMAGEHLPAPIVERSIONEX)(LPAPI_VERSION AppVersion);  
  LPIMAGEHLPAPIVERSIONEX lpImagehlpApiVersionEx = 
    (LPIMAGEHLPAPIVERSIONEX)GetProcAddress(hDbgHelp, "ImagehlpApiVersionEx");
  ATLASSERT(lpImagehlpApiVersionEx!=NULL);
  if(lpImagehlpApiVersionEx!=NULL)
  {    
    API_VERSION CompiledApiVer;
    CompiledApiVer.MajorVersion = 6;
    CompiledApiVer.MinorVersion = 1;
    CompiledApiVer.Revision = 11;    
    CompiledApiVer.Reserved = 0;
    LPAPI_VERSION pActualApiVer = lpImagehlpApiVersionEx(&CompiledApiVer);    
    pActualApiVer;
    ATLASSERT(CompiledApiVer.MajorVersion==pActualApiVer->MajorVersion);
    ATLASSERT(CompiledApiVer.MinorVersion==pActualApiVer->MinorVersion);
    ATLASSERT(CompiledApiVer.Revision==pActualApiVer->Revision);    
  }
  
  // Write minidump to the file
  mei.ThreadId = g_CrashInfo.m_dwThreadId;
  mei.ExceptionPointers = g_CrashInfo.m_pExInfo;
  mei.ClientPointers = TRUE;
  
  mci.CallbackRoutine = MiniDumpCallback;
  mci.CallbackParam = this;

  typedef BOOL (WINAPI *LPMINIDUMPWRITEDUMP)(
    HANDLE hProcess, 
    DWORD ProcessId, 
    HANDLE hFile, 
    MINIDUMP_TYPE DumpType, 
    CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, 
    CONST PMINIDUMP_USER_STREAM_INFORMATION UserEncoderParam, 
    CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

  LPMINIDUMPWRITEDUMP pfnMiniDumpWriteDump = 
    (LPMINIDUMPWRITEDUMP)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
  if(!pfnMiniDumpWriteDump)
  {    
    m_Assync.SetProgress(_T("Bad MiniDumpWriteDump function."), 0, false);
    return FALSE;
  }

  HANDLE hProcess = OpenProcess(
    PROCESS_ALL_ACCESS, 
    FALSE, 
    g_CrashInfo.m_dwProcessId);

  BOOL bWriteDump = pfnMiniDumpWriteDump(
    hProcess,
    g_CrashInfo.m_dwProcessId,
    hFile,
    g_CrashInfo.m_MinidumpType,
    &mei,
    NULL,
    &mci);
 
  if(!bWriteDump)
  {    
    m_Assync.SetProgress(_T("Error writing dump."), 0, false);
    m_Assync.SetProgress(Utility::FormatErrorMsg(GetLastError()), 0, false);
    goto cleanup;
  }

  fi.m_bMakeCopy = false;
  fi.m_sDesc = _T("Crash Dump");
  fi.m_sDestFile = _T("crashdump.dmp");
  fi.m_sSrcFile = sMinidumpFile;
  files_to_add.push_back(fi);
  bAdd = g_CrashInfo.AddFilesToCrashDescriptionXML(files_to_add);
  if(!bAdd)
  {
    m_Assync.SetProgress(_T("Error adding minidump file to the reprot."), 0, false);
    goto cleanup;
  }

  bStatus = TRUE;
  m_Assync.SetProgress(_T("Finished creating dump."), 100, false);

cleanup:

  // Close file
  if(hFile)
    CloseHandle(hFile);

  // Unload dbghelp.dll
  if(hDbgHelp)
    FreeLibrary(hDbgHelp);

  return bStatus;
}

// This method collects user-specified files
BOOL CErrorReportSender::CollectCrashFiles()
{ 
  BOOL bStatus = FALSE;
  CString str;
  CString sErrorReportDir = g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName;
  CString sSrcFile;
  CString sDestFile;
  HANDLE hSrcFile = INVALID_HANDLE_VALUE;
  HANDLE hDestFile = INVALID_HANDLE_VALUE;
  LARGE_INTEGER lFileSize;
  BOOL bGetSize = FALSE;
  LPBYTE buffer[4096];
  LARGE_INTEGER lTotalWritten;
  DWORD dwBytesRead=0;
  DWORD dwBytesWritten=0;
  BOOL bRead = FALSE;
  BOOL bWrite = FALSE;
  std::map<CString, CString>::iterator rit;

  // Copy application-defined files that should be copied on crash
  m_Assync.SetProgress(_T("[copying_files]"), 0, false);

  std::map<CString, ERIFileItem>::iterator it;
  for(it=g_CrashInfo.GetReport(m_nCurReport).m_FileItems.begin(); it!=g_CrashInfo.GetReport(m_nCurReport).m_FileItems.end(); it++)
  {
    if(m_Assync.IsCancelled())
      goto cleanup;

    if(it->second.m_bMakeCopy)
    {
      str.Format(_T("Copying file %s."), it->second.m_sSrcFile);
      m_Assync.SetProgress(str, 0, false);
      
      hSrcFile = CreateFile(it->second.m_sSrcFile, GENERIC_READ, 
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
      if(hSrcFile==INVALID_HANDLE_VALUE)
      {
        str.Format(_T("Error opening file %s."), it->second.m_sSrcFile);
        m_Assync.SetProgress(str, 0, false);
      }
      
      bGetSize = GetFileSizeEx(hSrcFile, &lFileSize);
      if(!bGetSize)
      {
        str.Format(_T("Couldn't get file size of %s"), it->second.m_sSrcFile);
        m_Assync.SetProgress(str, 0, false);
        CloseHandle(hSrcFile);
        hSrcFile = INVALID_HANDLE_VALUE;
        continue;
      }

      sDestFile = sErrorReportDir + _T("\\") + it->second.m_sDestFile;
      
      hDestFile = CreateFile(sDestFile, GENERIC_WRITE, 
        FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
      if(hDestFile==INVALID_HANDLE_VALUE)
      {
        str.Format(_T("Error creating file %s."), sDestFile);
        m_Assync.SetProgress(str, 0, false);
        CloseHandle(hSrcFile);
        hSrcFile = INVALID_HANDLE_VALUE;
        continue;
      }

      lTotalWritten.QuadPart = 0;

      for(;;)
      {        
        if(m_Assync.IsCancelled())
          goto cleanup;
    
        bRead = ReadFile(hSrcFile, buffer, 4096, &dwBytesRead, NULL);
        if(!bRead || dwBytesRead==0)
          break;

        bWrite = WriteFile(hDestFile, buffer, dwBytesRead, &dwBytesWritten, NULL);
        if(!bWrite || dwBytesRead!=dwBytesWritten)
          break;

        lTotalWritten.QuadPart += dwBytesWritten;

        int nProgress = (int)(100.0f*lTotalWritten.QuadPart/lFileSize.QuadPart);

        m_Assync.SetProgress(nProgress, false);
      }

      //if(lTotalWritten.QuadPart!=lFileSize.QuadPart)
      //  goto cleanup; // Error copying file

      CloseHandle(hSrcFile);
      hSrcFile = INVALID_HANDLE_VALUE;
      CloseHandle(hDestFile);
      hDestFile = INVALID_HANDLE_VALUE;
    }
  }

  // Create dump of registry keys

  ErrorReportInfo& eri = g_CrashInfo.GetReport(m_nCurReport);  
  if(eri.m_RegKeys.size()!=0)
  {
    m_Assync.SetProgress(_T("Dumping registry keys..."), 0, false);    
  }
  
  for(rit=eri.m_RegKeys.begin(); rit!=eri.m_RegKeys.end(); rit++)
  {
    if(m_Assync.IsCancelled())
      goto cleanup;

    CString sFileName = eri.m_sErrorReportDirName + _T("\\") + rit->second;

    str.Format(_T("Dumping registry key '%s' to file '%s' "), rit->first, sFileName);
    m_Assync.SetProgress(str, 0, false);    

    DumpRegKey(rit->first, sFileName);
    ERIFileItem fi;
    fi.m_sSrcFile = sFileName;
    fi.m_sDestFile = rit->second;
    fi.m_sDesc = _T("Registry Key Dump");
    fi.m_bMakeCopy = FALSE;
    std::vector<ERIFileItem> file_list;
    file_list.push_back(fi);
    g_CrashInfo.AddFilesToCrashDescriptionXML(file_list);
  }

  // Success
  bStatus = TRUE;

cleanup:
  
  if(hSrcFile!=INVALID_HANDLE_VALUE)
    CloseHandle(hSrcFile);

  if(hDestFile!=INVALID_HANDLE_VALUE)
    CloseHandle(hDestFile);

  m_Assync.SetProgress(_T("Finished copying files."), 100, false);
  
  return 0;
}

int CErrorReportSender::DumpRegKey(CString sRegKey, CString sDestFile)
{
  strconv_t strconv;
  TiXmlDocument document; 

  // Load document if file already exists
  // otherwise create new document.
  
  FILE* f = NULL;
#if _MSC_VER<1400
  f = _tfopen(sDestFile, _T("rb"));
#else
  _tfopen_s(&f, sDestFile, _T("rb"));
#endif
  if(f!=NULL)
  { 
    document.LoadFile(f);
  }

  TiXmlHandle hdoc(&document);
  
  TiXmlElement* registry = document.RootElement();

  if(registry==NULL)
  {
    registry = new TiXmlElement("registry");
    document.LinkEndChild(registry);
  }

  TiXmlNode* declaration = hdoc.Child(0).ToNode();
  if(declaration==NULL || declaration->Type()!=TiXmlNode::TINYXML_DECLARATION)
  {
    TiXmlDeclaration * decl = new TiXmlDeclaration( "1.0", "UTF-8", "" );
    document.InsertBeforeChild(registry, *decl);
  }   

  DumpRegKey(NULL, sRegKey, registry);

  if(f==NULL)
  {
#if _MSC_VER<1400
  f = _tfopen(sDestFile, _T("wb"));
#else
  _tfopen_s(&f, sDestFile, _T("wb"));
#endif
  }

  bool bSave = document.SaveFile(f);

  fclose(f);

  return (bSave==true)?0:1;
}

int CErrorReportSender::DumpRegKey(HKEY hParentKey, CString sSubKey, TiXmlElement* elem)
{
  strconv_t strconv;
  HKEY hKey = NULL;  

  if(hParentKey==NULL)
  {    
    int nSkip = 0;
    if(sSubKey.Left(19).Compare(_T("HKEY_LOCAL_MACHINE\\"))==0)
    {
      hKey = HKEY_LOCAL_MACHINE;
      nSkip = 18;
    }
    else if(sSubKey.Left(18).Compare(_T("HKEY_CURRENT_USER\\"))==0)
    {
      hKey = HKEY_CURRENT_USER;
      nSkip = 17;
    }    
    else 
    {
      return 1; // Invalid key.
    }

    CString sKey = sSubKey.Mid(0, nSkip);
    LPCSTR szKey = strconv.t2utf8(sKey);
    sSubKey = sSubKey.Mid(nSkip+1);

    TiXmlHandle key_node = elem->FirstChild(szKey);
    if(key_node.ToElement()==NULL)
    {
      key_node = new TiXmlElement("k");
      elem->LinkEndChild(key_node.ToNode());
      key_node.ToElement()->SetAttribute("name", szKey);
    }

    DumpRegKey(hKey, sSubKey, key_node.ToElement());
  }
  else
  {
    int pos = sSubKey.Find('\\');
    CString sKey = sSubKey;
    if(pos>0)
      sKey = sSubKey.Mid(0, pos);
    LPCSTR szKey = strconv.t2utf8(sKey);
    
    TiXmlHandle key_node = elem->FirstChild(szKey);
    if(key_node.ToElement()==NULL)
    {
      key_node = new TiXmlElement("k");
      elem->LinkEndChild(key_node.ToNode());
      key_node.ToElement()->SetAttribute("name", szKey);
    }

    if(ERROR_SUCCESS==RegOpenKeyEx(hParentKey, sKey, 0, GENERIC_READ, &hKey))
    {
      if(pos>0)
      {
        sSubKey = sSubKey.Mid(pos+1);
        DumpRegKey(hKey, sSubKey, key_node.ToElement());
      }
      else
      {
        DWORD dwSubKeys = 0;
        DWORD dwMaxSubKey = 0;
        DWORD dwValues = 0;
        DWORD dwMaxValueNameLen = 0;
        DWORD dwMaxValueLen = 0;
        LONG lResult = RegQueryInfoKey(hKey, NULL, 0, 0, &dwSubKeys, &dwMaxSubKey, 
          0, &dwValues, &dwMaxValueNameLen, &dwMaxValueLen, NULL, NULL); 
        if(lResult==ERROR_SUCCESS)
        {
          // Enumerate and dump subkeys          
          int i;
          for(i=0; i<(int)dwSubKeys; i++)
          { 
            LPWSTR szName = new WCHAR[dwMaxSubKey];
            DWORD dwLen = dwMaxSubKey;
            lResult = RegEnumKeyEx(hKey, i, szName, &dwLen, 0, NULL, 0, NULL);
            if(lResult==ERROR_SUCCESS)
            {
              DumpRegKey(hKey, CString(szName), key_node.ToElement());
            }

            delete [] szName;
          }         

          // Dump key values 
          for(i=0; i<(int)dwValues; i++)
          { 
            LPWSTR szName = new WCHAR[dwMaxValueNameLen];
            LPBYTE pData = new BYTE[dwMaxValueLen];
            DWORD dwNameLen = dwMaxValueNameLen;
            DWORD dwValueLen = dwMaxValueLen;
            DWORD dwType = 0;

            lResult = RegEnumValue(hKey, i, szName, &dwNameLen, 0, &dwType, pData, &dwValueLen);
            if(lResult==ERROR_SUCCESS)
            {
              TiXmlHandle val_node = key_node.ToElement()->FirstChild(strconv.w2utf8(szName));
              if(val_node.ToElement()==NULL)
              {
                val_node = new TiXmlElement("v");
                key_node.ToElement()->LinkEndChild(val_node.ToNode());
              }
                
              val_node.ToElement()->SetAttribute("name", strconv.w2utf8(szName));

              char str[128];
              LPSTR szType = NULL;
              if(dwType==REG_BINARY)
                szType = "REG_BINARY";
              else if(dwType==REG_DWORD)
                szType = "REG_DWORD";
              else if(dwType==REG_EXPAND_SZ)
                szType = "REG_EXPAND_SZ";
              else if(dwType==REG_MULTI_SZ)
                szType = "REG_MULTI_SZ";
              else if(dwType==REG_QWORD)
                szType = "REG_QWORD";
              else if(dwType==REG_SZ)
                szType = "REG_SZ";
              else 
              {
#if _MSC_VER<1400
                sprintf(str, "Unknown type (0x%08x)", dwType);
#else
                sprintf_s(str, 128, "Unknown type (0x%08x)", dwType);
#endif
                szType = str;                
              }
                
              val_node.ToElement()->SetAttribute("type", szType);              

              if(dwType==REG_BINARY)
              {
                std::string str;
                int i;
                for(i=0; i<(int)dwValueLen; i++)
                {
                  char num[10];
#if _MSC_VER<1400
                  sprintf(num, "%02X", pData[i]);
#else
                  sprintf_s(num, 10, "%02X", pData[i]);
#endif
                  str += num;
                  if(i<(int)dwValueLen)
                    str += " ";
                }
                
                val_node.ToElement()->SetAttribute("value", str.c_str());
              }
              else if(dwType==REG_DWORD)
              {
                LPDWORD pdwValue = (LPDWORD)pData;
                char str[64];
#if _MSC_VER<1400
                sprintf(str, "0x%08x (%lu)", *pdwValue, *pdwValue);                
#else
                sprintf_s(str, 64, "0x%08x (%lu)", *pdwValue, *pdwValue);                
#endif
                val_node.ToElement()->SetAttribute("value", str);
              }
              else if(dwType==REG_SZ || dwType==REG_EXPAND_SZ)
              {
                LPCSTR szValue = strconv.t2utf8((LPCTSTR)pData);
                val_node.ToElement()->SetAttribute("value", szValue);

                /*if(dwType==REG_EXPAND_SZ)
                {
                  DWORD dwDstBuffSize = ExpandEnvironmentStrings((LPCTSTR)pData, NULL, 0);
                  LPTSTR szExpanded = new TCHAR[dwDstBuffSize];
                  ExpandEnvironmentStrings((LPCTSTR)pData, szExpanded, dwDstBuffSize);                  
                  val_node.ToElement()->SetAttribute("expanded", strconv.t2utf8(szExpanded));
                  delete [] szExpanded;
                }*/
              }
              else if(dwType==REG_MULTI_SZ)
              {                
                LPCTSTR szValues = (LPCTSTR)pData;
                int prev = 0;
                int pos = 0;
                for(;;)
                {
                  if(szValues[pos]==0)
                  {
                    CString sValue = CString(szValues+prev, pos-prev);
                    LPCSTR szValue = strconv.t2utf8(sValue);

                    TiXmlHandle str_node = new TiXmlElement("str");
                    val_node.ToElement()->LinkEndChild(str_node.ToNode());                    
                    str_node.ToElement()->SetAttribute("value", szValue);              

                    prev = pos+1;
                  }

                  if(szValues[pos]==0 && szValues[pos+1]==0)
                    break; // Double-null

                  pos++;
                }                     
              }
            }

            delete [] szName;
            delete [] pData;
          }         
        }
      }

      RegCloseKey(hKey);
    }
    else
    {      
      CString sErrMsg = Utility::FormatErrorMsg(GetLastError());
      LPCSTR szErrMsg = strconv.t2utf8(sErrMsg);
      key_node.ToElement()->SetAttribute("error", szErrMsg);
    }
  }

  return 0;
}

int CErrorReportSender::CalcFileMD5Hash(CString sFileName, CString& sMD5Hash)
{
  FILE* f = NULL;
  BYTE buff[512];
  MD5 md5;
  MD5_CTX md5_ctx;
  unsigned char md5_hash[16];
  int i;

  sMD5Hash.Empty();

#if _MSC_VER<1400
  f = _tfopen(sFileName.GetBuffer(0), _T("rb"));
#else
  _tfopen_s(&f, sFileName.GetBuffer(0), _T("rb"));
#endif

  if(f==NULL) 
    return -1;

  md5.MD5Init(&md5_ctx);
  
  while(!feof(f))
  {
    size_t count = fread(buff, 1, 512, f);
    if(count>0)
    {
      md5.MD5Update(&md5_ctx, buff, (unsigned int)count);
    }
  }

  fclose(f);
  md5.MD5Final(md5_hash, &md5_ctx);

  for(i=0; i<16; i++)
  {
    CString number;
    number.Format(_T("%02x"), md5_hash[i]);
    sMD5Hash += number;
  }

  return 0;
}

// This method restarts the application
BOOL CErrorReportSender::RestartApp()
{
  if(g_CrashInfo.m_bAppRestart==FALSE)
    return FALSE;

  m_Assync.SetProgress(_T("Restarting the application..."), 0, false);

  STARTUPINFO si;
  memset(&si, 0, sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(PROCESS_INFORMATION));  

  CString sCmdLine;
  if(g_CrashInfo.m_sRestartCmdLine.IsEmpty())
  {
    // Format this way to avoid first empty parameter
    sCmdLine.Format(_T("\"%s\""), g_CrashInfo.GetReport(m_nCurReport).m_sImageName);
  }
  else
  {
    sCmdLine.Format(_T("\"%s\" \"%s\""), g_CrashInfo.GetReport(m_nCurReport).m_sImageName, 
      g_CrashInfo.m_sRestartCmdLine.GetBuffer(0));
  }
  BOOL bCreateProcess = CreateProcess(
    g_CrashInfo.GetReport(m_nCurReport).m_sImageName, sCmdLine.GetBuffer(0), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
  if(!bCreateProcess)
  {    
    m_Assync.SetProgress(_T("Error restarting the application!"), 0, false);
    return FALSE;
  }

  m_Assync.SetProgress(_T("Application restarted OK."), 0, false);
  return TRUE;
}

LONG64 CErrorReportSender::GetUncompressedReportSize(ErrorReportInfo& eri)
{
  m_Assync.SetProgress(_T("Calculating total size of files to compress..."), 0, false);

  LONG64 lTotalSize = 0;
  std::map<CString, ERIFileItem>::iterator it;
  HANDLE hFile = INVALID_HANDLE_VALUE;  
  CString sMsg;
  BOOL bGetSize = FALSE;
  LARGE_INTEGER lFileSize;

  for(it=eri.m_FileItems.begin(); it!=eri.m_FileItems.end(); it++)
  {    
    if(m_Assync.IsCancelled())    
      return 0;

    CString sFileName = it->second.m_sSrcFile.GetBuffer(0);
    hFile = CreateFile(sFileName, 
      GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL); 
    if(hFile==INVALID_HANDLE_VALUE)
    {
      sMsg.Format(_T("Couldn't open file %s"), sFileName);
      m_Assync.SetProgress(sMsg, 0, false);
      continue;
    }
    
    bGetSize = GetFileSizeEx(hFile, &lFileSize);
    if(!bGetSize)
    {
      sMsg.Format(_T("Couldn't get file size of %s"), sFileName);
      m_Assync.SetProgress(sMsg, 0, false);
      CloseHandle(hFile);
      continue;
    }

    lTotalSize += lFileSize.QuadPart;
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;
  }

  return lTotalSize;
}

// This method compresses the files contained in the report and produces ZIP archive.
BOOL CErrorReportSender::CompressReportFiles(ErrorReportInfo& eri)
{ 
  BOOL bStatus = FALSE;
  strconv_t strconv;
  zipFile hZip = NULL;
  CString sMsg;
  LONG64 lTotalSize = 0;
  LONG64 lTotalCompressed = 0;
  BYTE buff[1024];
  DWORD dwBytesRead=0;
  HANDLE hFile = INVALID_HANDLE_VALUE;  
  std::map<CString, ERIFileItem>::iterator it;
    
  if(m_bExport)
    m_Assync.SetProgress(_T("[exporting_report]"), 0, false);
  else
    m_Assync.SetProgress(_T("[compressing_files]"), 0, false);
  
  lTotalSize = GetUncompressedReportSize(eri);
  
  sMsg.Format(_T("Total file size for compression is %I64d bytes"), lTotalSize);
  m_Assync.SetProgress(sMsg, 0, false);

  if(m_bExport)
    m_sZipName = m_sExportFileName;  
  else
    m_sZipName = eri.m_sErrorReportDirName + _T(".zip");  
    
  sMsg.Format(_T("Creating ZIP archive file %s"), m_sZipName);
  m_Assync.SetProgress(sMsg, 1, false);

  hZip = zipOpen((const char*)m_sZipName.GetBuffer(0), APPEND_STATUS_CREATE);
  if(hZip==NULL)
  {
    m_Assync.SetProgress(_T("Failed to create ZIP file."), 100, true);
    goto cleanup;
  }

  for(it=eri.m_FileItems.begin(); it!=eri.m_FileItems.end(); it++)
  { 
    if(m_Assync.IsCancelled())    
      goto cleanup;
    
    CString sDstFileName = it->second.m_sDestFile.GetBuffer(0);
    CString sFileName = it->second.m_sSrcFile.GetBuffer(0);
    CString sDesc = it->second.m_sDesc;

    sMsg.Format(_T("Compressing %s"), sDstFileName);
    m_Assync.SetProgress(sMsg, 0, false);
        
    HANDLE hFile = CreateFile(sFileName, 
      GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL); 
    if(hFile==INVALID_HANDLE_VALUE)
    {
      sMsg.Format(_T("Couldn't open file %s"), sFileName);
      m_Assync.SetProgress(sMsg, 0, false);
      continue;
    }

    BY_HANDLE_FILE_INFORMATION fi;
    GetFileInformationByHandle(hFile, &fi);

    SYSTEMTIME st;
    FileTimeToSystemTime(&fi.ftCreationTime, &st);

    zip_fileinfo info;
    info.dosDate = 0;
    info.tmz_date.tm_year = st.wYear;
    info.tmz_date.tm_mon = st.wMonth-1;
    info.tmz_date.tm_mday = st.wDay;
    info.tmz_date.tm_hour = st.wHour;
    info.tmz_date.tm_min = st.wMinute;
    info.tmz_date.tm_sec = st.wSecond;
    info.external_fa = FILE_ATTRIBUTE_NORMAL;
    info.internal_fa = FILE_ATTRIBUTE_NORMAL;

    int n = zipOpenNewFileInZip( hZip, (const char*)strconv.t2a(sDstFileName.GetBuffer(0)), &info,
              NULL, 0, NULL, 0, strconv.t2a(sDesc), Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    if(n!=0)
    {
      sMsg.Format(_T("Couldn't compress file %s"), sDstFileName);
      m_Assync.SetProgress(sMsg, 0, false);
      continue;
    }

    for(;;)
    {
      if(m_Assync.IsCancelled())    
        goto cleanup;

      BOOL bRead = ReadFile(hFile, buff, 1024, &dwBytesRead, NULL);
      if(!bRead || dwBytesRead==0)
        break;

      int res = zipWriteInFileInZip(hZip, buff, dwBytesRead);
      if(res!=0)
      {
        zipCloseFileInZip(hZip);
        sMsg.Format(_T("Couldn't write to compressed file %s"), sDstFileName);
        m_Assync.SetProgress(sMsg, 0, false);        
        break;
      }

      lTotalCompressed += dwBytesRead;

      float fProgress = 100.0f*lTotalCompressed/lTotalSize;
      m_Assync.SetProgress((int)fProgress, false);
    }

    zipCloseFileInZip(hZip);
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;
  }

  if(lTotalSize==lTotalCompressed)
    bStatus = TRUE;

cleanup:

  if(hZip!=NULL)
    zipClose(hZip, NULL);

  if(hFile!=INVALID_HANDLE_VALUE)
    CloseHandle(hFile);

  if(bStatus)
    m_Assync.SetProgress(_T("Finished compressing files...OK"), 100, true);
  else
    m_Assync.SetProgress(_T("File compression failed."), 100, true);

  if(m_bExport)
  {
    if(bStatus)
      m_Assync.SetProgress(_T("[end_exporting_report_ok]"), 100, false);    
    else
      m_Assync.SetProgress(_T("[end_exporting_report_failed]"), 100, false);    
  }
  else
  {    
    m_Assync.SetProgress(_T("[end_compressing_files]"), 100, false);   
  }

  return bStatus;
}

// This method sends the error report over the Internet
BOOL CErrorReportSender::SendReport()
{
  int status = 1;

  m_Assync.SetProgress(_T("[sending_report]"), 0);

  std::multimap<int, int> order;
 
  std::pair<int, int> pair3(g_CrashInfo.m_uPriorities[CR_SMAPI], CR_SMAPI);
  order.insert(pair3);

  std::pair<int, int> pair2(g_CrashInfo.m_uPriorities[CR_SMTP], CR_SMTP);
  order.insert(pair2);

  std::pair<int, int> pair1(g_CrashInfo.m_uPriorities[CR_HTTP], CR_HTTP);
  order.insert(pair1);

  std::multimap<int, int>::reverse_iterator rit;
  
  for(rit=order.rbegin(); rit!=order.rend(); rit++)
  {
    m_Assync.SetProgress(_T("[sending_attempt]"), 0);
    m_SendAttempt++;    

    if(m_Assync.IsCancelled()){ break; }

    int id = rit->second;

    BOOL bResult = FALSE;

    if(id==CR_HTTP)
      bResult = SendOverHTTP();    
    else if(id==CR_SMTP)
      bResult = SendOverSMTP();  
    else if(id==CR_SMAPI)
      bResult = SendOverSMAPI();

    if(bResult==FALSE)
      continue;

    if(id==CR_SMAPI && bResult==TRUE)
    {
      status = 0;
      break;
    }

    if(0==m_Assync.WaitForCompletion())
    {
      status = 0;
      break;
    }
  }

  // Remove compressed error report file
  Utility::RecycleFile(m_sZipName, true);

  if(status==0)
  {
    m_Assync.SetProgress(_T("[status_success]"), 0);
    g_CrashInfo.GetReport(m_nCurReport).m_DeliveryStatus = DELIVERED;
    // Delete report files
    Utility::RecycleFile(g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, true);    
  }
  else
  {
    g_CrashInfo.GetReport(m_nCurReport).m_DeliveryStatus = FAILED;    
    m_Assync.SetProgress(_T("[status_failed]"), 0);    

    // Check if we should store files for later delivery or we should remove them
    if(!g_CrashInfo.m_bQueueEnabled)
    {
      // Delete report files
      Utility::RecycleFile(g_CrashInfo.GetReport(m_nCurReport).m_sErrorReportDirName, true);    
    }
  }

  m_Assync.SetCompleted(status);
  
  return 0;
}

// This method sends the report over HTTP request
BOOL CErrorReportSender::SendOverHTTP()
{  
  strconv_t strconv;

  if(g_CrashInfo.m_uPriorities[CR_HTTP]==CR_NEGATIVE_PRIORITY)
  {
    m_Assync.SetProgress(_T("Sending error report over HTTP is disabled (negative priority); skipping."), 0);
    return FALSE;
  }

  if(g_CrashInfo.m_sUrl.IsEmpty())
  {
    m_Assync.SetProgress(_T("No URL specified for sending error report over HTTP; skipping."), 0);
    return FALSE;
  }

  m_Assync.SetProgress(_T("Sending error report over HTTP..."), 0);
  m_Assync.SetProgress(_T("Preparing HTTP request data..."), 0);

  CHttpRequest request;
  request.m_sUrl = g_CrashInfo.m_sUrl;  
    
  request.m_aTextFields[_T("appname")] = strconv.t2a(g_CrashInfo.GetReport(m_nCurReport).m_sAppName);
  request.m_aTextFields[_T("appversion")] = strconv.t2a(g_CrashInfo.GetReport(m_nCurReport).m_sAppVersion);
  request.m_aTextFields[_T("crashguid")] = strconv.t2a(g_CrashInfo.GetReport(m_nCurReport).m_sCrashGUID);
  request.m_aTextFields[_T("emailfrom")] = strconv.t2a(g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom);
  request.m_aTextFields[_T("emailsubject")] = strconv.t2a(g_CrashInfo.m_sEmailSubject);
  request.m_aTextFields[_T("description")] = strconv.t2a(g_CrashInfo.GetReport(m_nCurReport).m_sDescription);

  CString sMD5Hash;
  CalcFileMD5Hash(m_sZipName, sMD5Hash);
  request.m_aTextFields[_T("md5")] = strconv.t2a(sMD5Hash);

  if(g_CrashInfo.m_bHttpBinaryEncoding)
  {
    CHttpRequestFile f;
    f.m_sSrcFileName = m_sZipName;
    f.m_sContentType = _T("application/zip");  
    request.m_aIncludedFiles[_T("crashrpt")] = f;  
  }
  else
  {
    m_Assync.SetProgress(_T("Base-64 encoding file attachment, please wait..."), 1);

    std::string sEncodedData;
    int nRet = Base64EncodeAttachment(m_sZipName, sEncodedData);
    if(nRet!=0)
    {
      return FALSE;
    }
    request.m_aTextFields[_T("crashrpt")] = sEncodedData;
  }


  BOOL bSend = m_HttpSender.SendAssync(request, &m_Assync);  
  return bSend;
}

int CErrorReportSender::Base64EncodeAttachment(CString sFileName, 
										std::string& sEncodedFileData)
{
  strconv_t strconv;
  
  int uFileSize = 0;
  BYTE* uchFileData = NULL;  
  struct _stat st;  

  int nResult = _tstat(sFileName, &st);
  if(nResult != 0)
    return 1;  // File not found.
  
  // Allocate buffer of file size
  uFileSize = st.st_size;
  uchFileData = new BYTE[uFileSize];

  // Read file data to buffer.
  FILE* f = NULL;
#if _MSC_VER<1400
  f = _tfopen(sFileName, _T("rb"));
#else
  /*errno_t err = */_tfopen_s(&f, sFileName, _T("rb"));  
#endif 

  if(!f || fread(uchFileData, uFileSize, 1, f)!=1)
  {
    delete [] uchFileData;
    uchFileData = NULL;
    return 2; // Coudln't read file data.
  }
  
  fclose(f);
    
  sEncodedFileData = base64_encode(uchFileData, uFileSize);

  delete [] uchFileData;

  // OK.
  return 0;
}


// This method formats the E-mail message text
CString CErrorReportSender::FormatEmailText()
{
  CString sFileTitle = m_sZipName;
  sFileTitle.Replace('/', '\\');
  int pos = sFileTitle.ReverseFind('\\');
  if(pos>=0)
    sFileTitle = sFileTitle.Mid(pos+1);

  CString sText;

  sText += _T("This is the error report from ") + g_CrashInfo.m_sAppName + 
    _T(" ") + g_CrashInfo.GetReport(m_nCurReport).m_sAppVersion+_T(".\n\n");
 
  if(!g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom.IsEmpty())
  {
    sText += _T("This error report was sent by ") + g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom + _T(".\n");
    sText += _T("If you need additional info about the problem, you may want to contact this user again.\n\n");
  }     

  if(!g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom.IsEmpty())
  {
    sText += _T("The user has provided the following problem description:\n<<< ") + 
      g_CrashInfo.GetReport(m_nCurReport).m_sDescription + _T(" >>>\n\n");    
  }

  sText += _T("You may find detailed information about the error in files attached to this message:\n\n");
  sText += sFileTitle + _T(" is a ZIP archive which contains crash description XML (crashrpt.xml), crash minidump (crashdump.dmp) ");
  sText += _T("and possibly other files that your application added to the crash report.\n\n");

  sText += sFileTitle + _T(".md5 file contains MD5 hash for the ZIP archive. You might want to use this file to check integrity of the error report.\n\n");
  
  sText += _T("For additional information, see FAQ http://code.google.com/p/crashrpt/wiki/FAQ\n");

  return sText;
}

// This method sends the report over SMTP 
BOOL CErrorReportSender::SendOverSMTP()
{  
  strconv_t strconv;

  if(g_CrashInfo.m_uPriorities[CR_SMTP]==CR_NEGATIVE_PRIORITY)
  {
    m_Assync.SetProgress(_T("Sending error report over SMTP is disabled (negative priority); skipping."), 0);
    return FALSE;
  }

  if(g_CrashInfo.m_sEmailTo.IsEmpty())
  {
    m_Assync.SetProgress(_T("No E-mail address is specified for sending error report over SMTP; skipping."), 0);
    return FALSE;
  }
  m_EmailMsg.m_sFrom = (!g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom.IsEmpty())?
    g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom:g_CrashInfo.m_sEmailTo;
  m_EmailMsg.m_sTo = g_CrashInfo.m_sEmailTo;
  m_EmailMsg.m_nRecipientPort = g_CrashInfo.m_nSmtpPort;
  m_EmailMsg.m_sSubject = g_CrashInfo.m_sEmailSubject;

  if(g_CrashInfo.m_sEmailText.IsEmpty())
    m_EmailMsg.m_sText = FormatEmailText();
  else
    m_EmailMsg.m_sText = g_CrashInfo.m_sEmailText;
  m_EmailMsg.m_aAttachments.insert(m_sZipName);  

  // Create and attach MD5 hash file
  CString sErrorRptHash;
  CalcFileMD5Hash(m_sZipName, sErrorRptHash);
  CString sFileTitle = m_sZipName;
  sFileTitle.Replace('/', '\\');
  int pos = sFileTitle.ReverseFind('\\');
  if(pos>=0)
    sFileTitle = sFileTitle.Mid(pos+1);
  sFileTitle += _T(".md5");
  CString sTempDir;
  Utility::getTempDirectory(sTempDir);
  CString sTmpFileName = sTempDir +_T("\\")+ sFileTitle;
  FILE* f = NULL;
  _TFOPEN_S(f, sTmpFileName, _T("wt"));
  if(f!=NULL)
  {   
    LPCSTR szErrorRptHash = strconv.t2a(sErrorRptHash.GetBuffer(0));
    fwrite(szErrorRptHash, strlen(szErrorRptHash), 1, f);
    fclose(f);
    m_EmailMsg.m_aAttachments.insert(sTmpFileName);  
  }

  // Set SMTP proxy server (if specified)
  if ( !g_CrashInfo.m_sSmtpProxyServer.IsEmpty())
    m_SmtpClient.SetSmtpProxy( g_CrashInfo.m_sSmtpProxyServer, g_CrashInfo.m_nSmtpProxyPort);

  // Send mail
  int res = m_SmtpClient.SendEmailAssync(&m_EmailMsg, &m_Assync); 
  return (res==0);
}

// This method sends the report over Simple MAPI
BOOL CErrorReportSender::SendOverSMAPI()
{  
  strconv_t strconv;

  if(g_CrashInfo.m_uPriorities[CR_SMAPI]==CR_NEGATIVE_PRIORITY)
  {
    m_Assync.SetProgress(_T("Sending error report over SMAPI is disabled (negative priority); skipping."), 0);
    return FALSE;
  }

  if(g_CrashInfo.m_sEmailTo.IsEmpty())
  {
    m_Assync.SetProgress(_T("No E-mail address is specified for sending error report over Simple MAPI; skipping."), 0);
    return FALSE;
  }

  if(g_CrashInfo.m_bSilentMode)
  {
    m_Assync.SetProgress(_T("Simple MAPI may require user interaction (not acceptable for non-GUI mode); skipping."), 0);
    return FALSE;
  }

  m_Assync.SetProgress(_T("Sending error report using Simple MAPI"), 0, false);
  m_Assync.SetProgress(_T("Initializing MAPI"), 1);

  BOOL bMAPIInit = m_MapiSender.MAPIInitialize();
  if(!bMAPIInit)
  {
    m_Assync.SetProgress(m_MapiSender.GetLastErrorMsg(), 100, false);
    return FALSE;
  }
  
  if(m_SendAttempt!=0)
  {
    m_Assync.SetProgress(_T("[confirm_launch_email_client]"), 0);
    int confirm = 1;
    m_Assync.WaitForFeedback(confirm);
    if(confirm!=0)
    {
      m_Assync.SetProgress(_T("Cancelled by user"), 100, false);
      return FALSE;
    }
  }

  CString msg;
  CString sMailClientName;
  m_MapiSender.DetectMailClient(sMailClientName);
  
  msg.Format(_T("Launching the default email client (%s)"), sMailClientName);
  m_Assync.SetProgress(msg, 10);

  m_MapiSender.SetFrom(g_CrashInfo.GetReport(m_nCurReport).m_sEmailFrom);
  m_MapiSender.SetTo(g_CrashInfo.m_sEmailTo);
  m_MapiSender.SetSubject(g_CrashInfo.m_sEmailSubject);
  CString sFileTitle = m_sZipName;
  sFileTitle.Replace('/', '\\');
  int pos = sFileTitle.ReverseFind('\\');
  if(pos>=0)
    sFileTitle = sFileTitle.Mid(pos+1);
    
  if(g_CrashInfo.m_sEmailText.IsEmpty())
    m_MapiSender.SetMessage(FormatEmailText());
  else
    m_MapiSender.SetMessage(g_CrashInfo.m_sEmailText);
  m_MapiSender.AddAttachment(m_sZipName, sFileTitle);

  // Create and attach MD5 hash file
  CString sErrorRptHash;
  CalcFileMD5Hash(m_sZipName, sErrorRptHash);
  sFileTitle += _T(".md5");
  CString sTempDir;
  Utility::getTempDirectory(sTempDir);
  CString sTmpFileName = sTempDir +_T("\\")+ sFileTitle;
  FILE* f = NULL;
  _TFOPEN_S(f, sTmpFileName, _T("wt"));
  if(f!=NULL)
  { 
    LPCSTR szErrorRptHash = strconv.t2a(sErrorRptHash.GetBuffer(0));
    fwrite(szErrorRptHash, strlen(szErrorRptHash), 1, f);
    fclose(f);
    m_MapiSender.AddAttachment(sTmpFileName, sFileTitle);  
  }
  
  BOOL bSend = m_MapiSender.Send();
  if(!bSend)
    m_Assync.SetProgress(m_MapiSender.GetLastErrorMsg(), 100, false);
  else
    m_Assync.SetProgress(_T("Sent OK"), 100, false);
  
  return bSend;
}




