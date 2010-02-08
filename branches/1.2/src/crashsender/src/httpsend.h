/************************************************************************************* 
  This file is a part of CrashRpt library.

  CrashRpt is Copyright (c) 2003, Michael Carruth
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

// File: httpsend.h
// Description: Sends error report over HTTP connection.
// Authors: zexspectrum
// Date: 2009

#pragma once
#include "stdafx.h"
#include <string>
#include <map>
#include "AssyncNotification.h"

// HTTP request information
class CHttpRequest
{
public:
  CString m_sUrl;      // Script URL
  BOOL m_bMultiPart;   // TRUE==use multi-part content encoding, FALSE==use URL encoding + BASE64 encoding
  std::map<CString, CString> m_aTextFields;    // Array of text fields to include into POST data
  std::map<CString, CString> m_aIncludedFiles; // Array of binary files to include into POST data
};

// Sends HTTP request
class CHttpRequestSender
{
public:
  
  // Sends HTTP request assynchroniously
  BOOL SendAssync(CHttpRequest& Request, AssyncNotification* an);

private:

  void ParseURL(LPCTSTR szURL, LPTSTR szProtocol, UINT cbProtocol,
    LPTSTR szAddress, UINT cbAddress, DWORD &dwPort, LPTSTR szURI, UINT cbURI);

  BOOL InternalSend();

  static DWORD WINAPI WorkerThread(VOID* pParam);  

  AssyncNotification* m_Assync;
  CHttpRequest m_Request;
};


