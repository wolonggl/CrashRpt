/*! \file   CrashRpt.cpp
 *  \brief  Implementation of CrashRpt API
 *  \date   2003-2009
 *  \author Copyright (c) 2003 Michael Carruth
 *  \author zexspectrum_1980@gmail.com
 *  \todo
 */

#include "stdafx.h"
#include "CrashRpt.h"
#include "CrashHandler.h"

CComAutoCriticalSection g_cs; // Critical section for thread-safe accessing error messages
std::map<DWORD, CString> g_sErrorMsg; // Last error messages for each calling thread.

CRASHRPTAPI LPVOID InstallW(LPGETLOGFILE pfnCallback, LPCWSTR pszEmailTo, LPCWSTR pszEmailSubject)
{
  CR_INSTALL_INFO info;
  memset(&info, 0, sizeof(CR_INSTALL_INFO));
  info.cb = sizeof(CR_INSTALL_INFO);
  info.pfnCrashCallback = pfnCallback;
  info.pszEmailTo = pszEmailTo;
  info.pszEmailSubject = pszEmailSubject;

  crInstall(&info);

  return NULL;
}

CRASHRPTAPI LPVOID InstallA(LPGETLOGFILE pfnCallback, LPCSTR pszEmailTo, LPCSTR pszEmailSubject)
{
  return InstallW(pfnCallback, CStringW(pszEmailTo), CStringW(pszEmailSubject));
}

CRASHRPTAPI void Uninstall(LPVOID lpState)
{
  crUninstall();  
}

CRASHRPTAPI void AddFileW(LPVOID lpState, LPCWSTR lpFile, LPCWSTR lpDesc)
{ 
  crAddFile(lpFile, lpDesc);
}

CRASHRPTAPI void AddFileA(LPVOID lpState, LPCSTR lpFile, LPCSTR lpDesc)
{
  AddFileW(lpState, CStringW(lpFile), CStringW(lpDesc));
}

CRASHRPTAPI void GenerateErrorReport(LPVOID lpState, PEXCEPTION_POINTERS pExInfo)
{
  CR_EXCEPTION_INFO ei;
  memset(&ei, 0, sizeof(CR_EXCEPTION_INFO));
  ei.cb = sizeof(CR_EXCEPTION_INFO);
  ei.exctype = CR_WIN32_UNHANDLED_EXCEPTION;

  crGenerateErrorReport(&ei);
}

CRASHRPTAPI int crInstallW(CR_INSTALL_INFOW* pInfo)
{
  crSetErrorMsg(_T("Success."));

  // Validate input parameters.
  if(pInfo==NULL || 
     pInfo->cb!=sizeof(CR_INSTALL_INFOW))
  {
    ATLASSERT(pInfo->cb==sizeof(CR_INSTALL_INFO));
    ATLASSERT(pInfo != NULL);        
    crSetErrorMsg(_T("pInfo is NULL or pInfo->cb member is not valid."));
    return 1; 
  }

  // Check if crInstall() already was called for current process.
  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();

  if(pCrashHandler!=NULL)
  {
    ATLASSERT(pCrashHandler==NULL);
    crSetErrorMsg(_T("Can't install crash handlers to the same process twice."));
    return 2; 
  }
  
  pCrashHandler = new CCrashHandler();
  if(pCrashHandler==NULL)
  {
    ATLASSERT(pCrashHandler!=NULL);
    crSetErrorMsg(_T("Error allocating memory for crash handler."));
    return 3; 
  }

  int nInitResult = pCrashHandler->Init(
    pInfo->pszAppName, 
    pInfo->pszAppVersion, 
    pInfo->pszCrashSenderPath,
    pInfo->pfnCrashCallback,
    pInfo->pszEmailTo,
    pInfo->pszEmailSubject,
    pInfo->pszUrl,
    &pInfo->uPriorities);
  
  if(nInitResult!=0)
  {
    ATLASSERT(nInitResult==0);        
    return 4;
  }

  // OK.  
  return 0;
}

CRASHRPTAPI int crInstallA(CR_INSTALL_INFOA* pInfo)
{
  if(pInfo==NULL)
    return crInstallW((CR_INSTALL_INFOW*)NULL);

  // Convert pInfo members to wide char

  CStringW sAppName;
  CStringW sAppVersion;
  CStringW sCrashSenderPath;
  CStringW sEmailSubject;
  CStringW sEmailTo;
  CStringW sUrl;

  CR_INSTALL_INFOW ii;
  memset(&ii, 0, sizeof(CR_INSTALL_INFOW));
  ii.cb = sizeof(CR_INSTALL_INFOW);
  ii.pfnCrashCallback = pInfo->pfnCrashCallback;

  if(pInfo->pszAppName!=NULL)
  {
    sAppName = CStringW(pInfo->pszAppName);
    ii.pszAppName = sAppName;
  }

  if(pInfo->pszAppVersion!=NULL)
  {
    sAppVersion = CStringW(pInfo->pszAppVersion);
    ii.pszAppVersion = sAppVersion;
  }

  if(pInfo->pszCrashSenderPath!=NULL)
  {
    sCrashSenderPath = CStringW(pInfo->pszCrashSenderPath);
    ii.pszCrashSenderPath = sCrashSenderPath;
  }

  if(pInfo->pszEmailSubject!=NULL)
  {
    sEmailSubject = CStringW(pInfo->pszEmailSubject);
    ii.pszEmailSubject = sEmailSubject;
  }

  if(pInfo->pszEmailTo!=NULL)
  {
    sEmailTo = CStringW(pInfo->pszEmailTo);
    ii.pszEmailTo = sEmailTo;
  }

  if(pInfo->pszUrl!=NULL)
  {
    sUrl = CStringW(pInfo->pszUrl);
    ii.pszUrl = sUrl;
  }

  memcpy(&ii.uPriorities, pInfo->uPriorities, 3*sizeof(UINT));

  return crInstallW(&ii);
}

CRASHRPTAPI int crUninstall()
{
  crSetErrorMsg(_T("Success."));

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();
  
  if(pCrashHandler==NULL)
  {    
    ATLASSERT(pCrashHandler!=NULL);
    crSetErrorMsg(_T("Crash handler wasn't preiviously installed for this process."));
    return 1; 
  }

  // Uninstall main thread's C++ exception handlers
  int nUnset = pCrashHandler->UnSetThreadCPPExceptionHandlers();
  if(nUnset!=0)
    return 2;

  int nDestroy = pCrashHandler->Destroy();
  if(nDestroy!=0)
    return 3;

  delete pCrashHandler;
    
  return 0;
}

// Sets C++ exception handlers for the calling thread
CRASHRPTAPI int crInstallToCurrentThread()
{
  crSetErrorMsg(_T("Success."));

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();
  
  if(pCrashHandler==NULL)
  {
    ATLASSERT(pCrashHandler!=NULL);
    crSetErrorMsg(_T("Crash handler was already installed for current thread."));
    return 1; 
  }

  int nResult = pCrashHandler->SetThreadCPPExceptionHandlers();
  if(nResult!=0)
    return 2; // Error?

  // Ok.
  return 0;
}

// Unsets C++ exception handlers from the calling thread
CRASHRPTAPI int crUninstallFromCurrentThread()
{
  crSetErrorMsg(_T("Success."));

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();

  if(pCrashHandler==NULL)
  {
    ATLASSERT(pCrashHandler!=NULL);
    crSetErrorMsg(_T("Crash handler wasn't previously installed for current thread."));
    return 1; // Invalid parameter?
  }

  int nResult = pCrashHandler->UnSetThreadCPPExceptionHandlers();
  if(nResult!=0)
    return 2; // Error?

  // OK.
  return 0;
}

CRASHRPTAPI int crAddFileW(PCWSTR pszFile, PCWSTR pszDesc)
{
  crSetErrorMsg(_T("Success."));

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();

  if(pCrashHandler==NULL)
  {
    ATLASSERT(pCrashHandler!=NULL);
    crSetErrorMsg(_T("Crash handler wasn't previously installed for current process."));
    return 1; // No handler installed for current process?
  }
   
  int nAddResult = pCrashHandler->AddFile(pszFile, pszDesc);
  if(nAddResult!=0)
  {
    ATLASSERT(nAddResult==0);
    return 2; // Couldn't add file?
  }

  // OK.
  return 0;
}

CRASHRPTAPI int crAddFileA(PCSTR pszFile, PCSTR pszDesc)
{
  // Convert parameters to wide char
  PCTSTR ptszFile = NULL;
  PCTSTR ptszDesc = NULL;

  CStringW sFile;
  CStringW sDesc;

  if(pszFile)
  {
    sFile = CStringW(pszFile);
    ptszFile = sFile;
  }

  if(pszDesc)
  {
    sDesc = CStringW(pszDesc);
    ptszDesc = sDesc;
  }

  return crAddFileW(ptszFile, ptszDesc);
}

CRASHRPTAPI int crGenerateErrorReport(
  CR_EXCEPTION_INFO* pExceptionInfo)
{
  crSetErrorMsg(_T("Unspecified error."));

  if(pExceptionInfo==NULL || 
     pExceptionInfo->cb!=sizeof(CR_EXCEPTION_INFO))
  {
    crSetErrorMsg(_T("Exception info is NULL or invalid."));
    ATLASSERT(pExceptionInfo!=NULL);
    ATLASSERT(pExceptionInfo->cb==sizeof(CR_EXCEPTION_INFO));
    return 1;
  }

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();

  if(pCrashHandler==NULL)
  {    
    // Handler is not installed for current process 
    crSetErrorMsg(_T("Crash handler wasn't previously installed for current process."));
    ATLASSERT(pCrashHandler!=NULL);
    return 2;
  } 

  return pCrashHandler->GenerateErrorReport(pExceptionInfo);  
}

CRASHRPTAPI int crGetLastErrorMsgW(PWSTR pszBuffer, UINT uBuffSize)
{
  if(pszBuffer==NULL)
    return -1; // Null pointer to buffer

  g_cs.Lock();

  DWORD dwThreadId = GetCurrentThreadId();
  std::map<DWORD, CString>::iterator it = g_sErrorMsg.find(dwThreadId);

  if(it==g_sErrorMsg.end())
  {
    // No error message for current thread.
    CString sErrorMsg = _T("No error.");
    _TCSNCPY_S(pszBuffer, uBuffSize, sErrorMsg.GetBuffer(), sErrorMsg.GetLength());
    int size =  sErrorMsg.GetLength();
    g_cs.Unlock();
    return size;
  }
  
  _TCSNCPY_S(pszBuffer, uBuffSize, it->second.GetBuffer(), it->second.GetLength());
  int size = it->second.GetLength();
  g_cs.Unlock();
  return size;
}

CRASHRPTAPI int crGetLastErrorMsgA(PSTR pszBuffer, UINT uBuffSize)
{  
  if(pszBuffer==NULL)
    return -1;

  CStringW sBufferW;
  WCHAR* pwszBuffer = sBufferW.GetBufferSetLength(uBuffSize);
  
  int res = crGetLastErrorMsgW(pwszBuffer, uBuffSize);

  CStringA sBufferA = sBufferW;

  STRCPY_S(pszBuffer, uBuffSize, sBufferA);

  return res;
}

int crSetErrorMsg(PTSTR pszErrorMsg)
{  
  g_cs.Lock();
  DWORD dwThreadId = GetCurrentThreadId();
  g_sErrorMsg[dwThreadId] = pszErrorMsg;
  g_cs.Unlock();
  return 0;
}


CRASHRPTAPI int crExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS* ep)
{
  crSetErrorMsg(_T("Success."));

  CCrashHandler *pCrashHandler = 
    CCrashHandler::GetCurrentProcessCrashHandler();

  if(pCrashHandler==NULL)
  {    
    crSetErrorMsg(_T("Crash handler wasn't previously installed for current process."));
    return EXCEPTION_CONTINUE_SEARCH; 
  }

  CR_EXCEPTION_INFO ei;
  memset(&ei, 0, sizeof(CR_EXCEPTION_INFO));
  ei.cb = sizeof(CR_EXCEPTION_INFO);  
  ei.exctype = CR_WIN32_UNHANDLED_EXCEPTION;
  ei.pexcptrs = ep;

  int nGenerate = pCrashHandler->GenerateErrorReport(&ei);
  if(!nGenerate)
    return EXCEPTION_CONTINUE_SEARCH;
    
  return EXCEPTION_EXECUTE_HANDLER;
}

//-----------------------------------------------------------------------------------------------
// Below crEmulateCrash() related stuff goes 


class CDerived;
class CBase
{
public:
   CBase(CDerived *derived): m_pDerived(derived) {};
   ~CBase();
   virtual void function(void) = 0;

   CDerived * m_pDerived;
};

class CDerived : public CBase
{
public:
   CDerived() : CBase(this) {};   // C4355
   virtual void function(void) {};
};

CBase::~CBase()
{
   m_pDerived -> function();
}

#include <float.h>
void sigfpe_test()
{ 
  // Code taken from http://www.devx.com/cplus/Article/34993/1954

  //Set the x86 floating-point control word according to what
  //exceptions you want to trap. 
  _clearfp(); //Always call _clearfp before setting the control
              //word
  //Because the second parameter in the following call is 0, it
  //only returns the floating-point control word
  unsigned int cw; 
#if _MSC_VER<1400
  cw = _controlfp(0, 0); //Get the default control
#else
  _controlfp_s(&cw, 0, 0); //Get the default control
#endif 
                                      //word
  //Set the exception masks off for exceptions that you want to
  //trap.  When a mask bit is set, the corresponding floating-point
  //exception is //blocked from being generating.
  cw &=~(EM_OVERFLOW|EM_UNDERFLOW|EM_ZERODIVIDE|
         EM_DENORMAL|EM_INVALID);
  //For any bit in the second parameter (mask) that is 1, the 
  //corresponding bit in the first parameter is used to update
  //the control word.  
  unsigned int cwOriginal;
#if _MSC_VER<1400
  cwOriginal = _controlfp(cw, MCW_EM); //Set it.
#else
  _controlfp_s(&cwOriginal, cw, MCW_EM); //Set it.
#endif
                              //MCW_EM is defined in float.h.
                              //Restore the original value when done:
                              //_controlfp(cwOriginal, MCW_EM);

  // Divide by zero

  float a = 1;
  float b = 0;
  float c = a/b;
  c; 
}

#define BIG_NUMBER 0x1fffffff
#pragma warning(disable: 4717) // avoid C4717 warning
int RecurseAlloc() 
{
   int *pi = new int[BIG_NUMBER];
   pi;
   RecurseAlloc();
   return 0;
}

CRASHRPTAPI int crEmulateCrash(unsigned ExceptionType)
{
  crSetErrorMsg(_T("Success."));

  switch(ExceptionType)
  {
  case CR_WIN32_UNHANDLED_EXCEPTION:
    {
      int *p = 0;
      *p = 0;
    }
    break;
  case CR_CPP_TERMINATE_CALL:
    {
      terminate();
    }
    break;
  case CR_CPP_UNEXPECTED_CALL:
    {
      unexpected();
    }
    break;
  case CR_CPP_PURE_CALL:
    {
      // pure virtual method call
      CDerived derived;
    }
    break;
  case CR_CPP_SECURITY_ERROR:
    {
      // Cause buffer overrun (/GS compiler option)

      char large_buffer[] = "This string is longer than 10 characters!!!";
      // vulnerable code
      char buffer[10];
#pragma warning(disable:4996) // avoid C4996 warning
      strcpy(buffer, large_buffer); // overrun buffer !!!      
    }
    break;
  case CR_CPP_INVALID_PARAMETER:
    {
      char* formatString;
      // Call printf_s with invalid parameters.
      formatString = NULL;
      printf(formatString);
    }
    break;
  case CR_CPP_NEW_OPERATOR_ERROR:
    {
      RecurseAlloc();
    }
    break;
  case CR_CPP_SIGABRT: 
    {
      abort();
    }
    break;
  case CR_CPP_SIGFPE:
    {
      // floating point exception ( /fp:except compiler option)
      sigfpe_test();
      return 1;
    }
    break;
  case CR_CPP_SIGILL: 
    {
      int result = raise(SIGILL);  
      ATLASSERT(result==0);
      crSetErrorMsg(_T("Error raising SIGILL."));
      return result;
    }
    break;
  case CR_CPP_SIGINT: 
    {
      int result = raise(SIGINT);  
      ATLASSERT(result==0);
      crSetErrorMsg(_T("Error raising SIGINT."));
      return result;
    }
    break;
  case CR_CPP_SIGSEGV: 
    {
      int result = raise(SIGSEGV);  
      ATLASSERT(result==0);
      crSetErrorMsg(_T("Error raising SIGSEGV."));
      return result;
    }
    break;
  case CR_CPP_SIGTERM: 
    {
     int result = raise(SIGTERM);  
     crSetErrorMsg(_T("Error raising SIGTERM."));
	 ATLASSERT(result==0);     
     return result;
    }
  default:
    ATLASSERT(0); // unknown type?
    crSetErrorMsg(_T("Unknown exception type specified."));
    return 1;
  }
 
  return 0;
}



