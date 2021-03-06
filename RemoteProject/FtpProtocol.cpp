
#include "StdAfx.h"
#include "resource.h"

#include "Project.h"

#include "Authen.h"

#include "FtpProtocol.h"

#pragma code_seg( "PROTOCOLS" )


////////////////////////////////////////////////////////
//

class CFtpCancelThread : public CThreadImpl<CFtpCancelThread>
{
public:
   CFtpCancelThread(HINTERNET hInternet, DWORD dwTimeout) : 
      m_hInternet(hInternet), 
      m_dwTimeout(dwTimeout)
   {
      m_event.Create();
   }

   DWORD Run()
   {
      if( !m_event.WaitForEvent(m_dwTimeout) ) ::InternetCloseHandle(m_hInternet);
      return 0;
   }

   void SetEvent()
   {
      m_event.SetEvent();
   }

   HINTERNET m_hInternet;
   DWORD m_dwTimeout;
   CEvent m_event;
};


////////////////////////////////////////////////////////
//

DWORD CFtpThread::Run()
{
   ATLASSERT(m_pManager);

   // Get connect parameters
   // TODO: Protect these guys with thread-lock
   CString sHost = m_pManager->GetParam(_T("Host"));
   CString sUsername = m_pManager->GetParam(_T("Username"));
   CString sPassword = m_pManager->GetParam(_T("Password"));
   long lPort = _ttol(m_pManager->GetParam(_T("Port")));
   CString sPath = m_pManager->GetParam(_T("Path"));
   CString sProxy = m_pManager->GetParam(_T("Proxy"));
   BOOL bPassive = (m_pManager->GetParam(_T("Passive")) == _T("true"));
   long lConnectTimeout = _ttol(m_pManager->GetParam(_T("ConnectTimeout"))) - 2;

   if( lPort == 0 ) lPort = INTERNET_DEFAULT_FTP_PORT;
   if( lConnectTimeout < 5 ) lConnectTimeout = 5;

   // Prompt for password?
   if( sPassword.IsEmpty() ) sPassword = SecGetPassword();

   m_pManager->m_dwErrorCode = 0;
   m_pManager->m_bConnected = false;

   m_pManager->m_hInternet = ::InternetOpen(_T("Mozilla/4.0 (compatible; MSIE 5.5; BVRDE)"),
      sProxy.IsEmpty() ? INTERNET_OPEN_TYPE_DIRECT : INTERNET_OPEN_TYPE_PROXY,
      sProxy.IsEmpty() ? NULL : static_cast<LPCTSTR>(sProxy),
      NULL,
      0);

   m_pManager->m_dwErrorCode = ::GetLastError();

   if( ShouldStop() ) return 0;

   if( m_pManager->m_hInternet == NULL )  return 0;

   DWORD dwTimeout = (DWORD) lConnectTimeout * 1000UL;
   ::InternetSetOption(m_pManager->m_hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

   // HACK: See Q176420 why INTERNET_OPTION_CONNECT_TIMEOUT is not working!
   CFtpCancelThread cancel(m_pManager->m_hInternet, dwTimeout);
   cancel.Start();

   m_pManager->m_hFTP = InternetConnect(m_pManager->m_hInternet,
      sHost,
      (INTERNET_PORT) lPort,
      sUsername.IsEmpty() ? NULL : static_cast<LPCTSTR>(sUsername),
      sPassword.IsEmpty() ? NULL : static_cast<LPCTSTR>(sPassword),      
      bPassive ? INTERNET_FLAG_PASSIVE : 0);

   cancel.SetEvent();
   cancel.WaitForThread();

   m_pManager->m_dwErrorCode = ::GetLastError();

   if( ShouldStop() ) return 0;

   if( m_pManager->m_hFTP == NULL ) return 0;

   BOOL bRes = ::FtpSetCurrentDirectory(m_pManager->m_hFTP, sPath);

   m_pManager->m_dwErrorCode = ::GetLastError();

   // FIX: See Q238311
   if( !bRes && m_pManager->m_dwErrorCode == ERROR_INVALID_PARAMETER ) {
      bRes = TRUE;
   }
   // Have better error description?
   if( !bRes && m_pManager->m_dwErrorCode == ERROR_INTERNET_EXTENDED_ERROR ) {
      m_pManager->_TranslateError();
      m_pManager->m_dwErrorCode = ::GetLastError();
   }
   if( !bRes ) return 0;

   m_pManager->m_dwErrorCode = 0;
   m_pManager->m_bConnected = true;

   return 0;
}

HINTERNET CFtpThread::InternetConnect(HINTERNET hInternet, LPCWSTR lpszServerName, INTERNET_PORT nServerPort, LPCWSTR lpszUserName, LPCWSTR lpszPassword, DWORD dwFlags)
{
   // HACK: WinInet FTP is quite unstable and causes
   //       frequent crashes when cancelled abruptly.
   __try
   {
      return ::InternetConnect(hInternet,
         lpszServerName,
         nServerPort,
         lpszUserName,
         lpszPassword,
         INTERNET_SERVICE_FTP,
         dwFlags,
         0);
   }
   __except(1)
   {
      return NULL;
   }
}


////////////////////////////////////////////////////////
//

CFtpProtocol::CFtpProtocol() :
   m_hInternet(NULL),
   m_hFTP(NULL),
   m_lPort(INTERNET_DEFAULT_FTP_PORT),
   m_dwErrorCode(0L),
   m_lConnectTimeout(0),
   m_dwLastCheck(0L),
   m_bCancel(false),
   m_bConnected(false)
{
   Clear();
}

CFtpProtocol::~CFtpProtocol()
{
   Stop();
}

void CFtpProtocol::Clear()
{
   m_sHost.Empty();
   m_sUsername.Empty();
   m_sPassword.Empty();
   m_lPort = INTERNET_DEFAULT_FTP_PORT;
   m_sPath.Empty();
   m_sProxy.Empty();
   m_bPassive = FALSE;
   m_bCompatibilityMode = FALSE;
   m_lConnectTimeout = 8;
}

bool CFtpProtocol::Load(ISerializable* pArc)
{
   Clear();

   if( !pArc->ReadItem(_T("FTP")) ) return false;
   pArc->Read(_T("host"), m_sHost.GetBufferSetLength(128), 128);
   m_sHost.ReleaseBuffer();
   pArc->Read(_T("port"), m_lPort);
   pArc->Read(_T("user"), m_sUsername.GetBufferSetLength(128), 128);
   m_sUsername.ReleaseBuffer();
   pArc->Read(_T("password"), m_sPassword.GetBufferSetLength(128), 128);
   m_sPassword.ReleaseBuffer();
   pArc->Read(_T("path"), m_sPath.GetBufferSetLength(MAX_PATH), MAX_PATH);
   m_sPath.ReleaseBuffer();
   pArc->Read(_T("searchPath"), m_sSearchPath.GetBufferSetLength(200), 200);
   m_sSearchPath.ReleaseBuffer();
   pArc->Read(_T("proxy"), m_sProxy.GetBufferSetLength(128), 128);
   m_sProxy.ReleaseBuffer();
   pArc->Read(_T("passive"), m_bPassive);
   pArc->Read(_T("compatibility"), m_bCompatibilityMode);
   pArc->Read(_T("connectTimeout"), m_lConnectTimeout);

   m_sPath.TrimRight(_T("/"));
   if( m_sPath.IsEmpty() ) m_sPath = _T("/");
   m_sPassword = SecDecodePassword(m_sPassword);
   if( m_lConnectTimeout <= 0 ) m_lConnectTimeout = 8;

   return true;
}

bool CFtpProtocol::Save(ISerializable* pArc)
{
   if( !pArc->WriteItem(_T("FTP")) ) return false;
   pArc->Write(_T("host"), m_sHost);
   pArc->Write(_T("port"), m_lPort);
   pArc->Write(_T("user"), m_sUsername);
   pArc->Write(_T("password"), SecEncodePassword(m_sPassword));
   pArc->Write(_T("path"), m_sPath);
   pArc->Write(_T("searchPath"), m_sSearchPath);
   pArc->Write(_T("proxy"), m_sProxy);
   pArc->Write(_T("passive"), m_bPassive);
   pArc->Write(_T("compatibility"), m_bCompatibilityMode);
   pArc->Write(_T("connectTimeout"), m_lConnectTimeout);
   return true;
}

bool CFtpProtocol::Start()
{
   Stop();
   m_thread.m_pManager = this;
   if( !m_thread.Start() ) return false;
   return true;
}

void CFtpProtocol::SignalStop()
{
   m_bCancel = true;
   m_thread.SignalStop();
}

bool CFtpProtocol::Stop()
{
   m_bCancel = true;
   m_thread.Stop();
   if( m_hFTP != NULL ) {
      ::InternetCloseHandle(m_hFTP);
      m_hFTP = NULL;
   }
   if( m_hInternet != NULL ) {
      ::InternetCloseHandle(m_hInternet);
      m_hInternet = NULL;
   } 
   m_bCancel = false;
   m_bConnected = false;
   m_dwErrorCode = 0L;
   return true;
}

bool CFtpProtocol::IsConnected() const
{
   return m_bConnected;
}

CString CFtpProtocol::GetParam(LPCTSTR pstrName) const
{
   CString sName = pstrName;
   if( sName == _T("Type") ) return _T("FTP");
   if( sName == _T("Path") ) return m_sPath;
   if( sName == _T("SearchPath") ) return m_sSearchPath;
   if( sName == _T("Host") ) return m_sHost;
   if( sName == _T("Username") ) return m_sUsername;
   if( sName == _T("Password") ) return m_sPassword;
   if( sName == _T("Port") ) return ToString(m_lPort);
   if( sName == _T("Passive") ) return m_bPassive ? _T("true") : _T("false");
   if( sName == _T("CompatibilityMode") ) return m_bCompatibilityMode ? _T("true") : _T("false");
   if( sName == _T("Proxy") ) return m_sProxy;
   if( sName == _T("Separator") ) return _T("/");
   if( sName == _T("ConnectTimeout") ) return ToString(m_lConnectTimeout);
   return _T("");
}

void CFtpProtocol::SetParam(LPCTSTR pstrName, LPCTSTR pstrValue)
{
   CString sName = pstrName;
   if( sName == _T("Path") ) m_sPath = pstrValue;
   if( sName == _T("SearchPath") ) m_sSearchPath = pstrValue;
   if( sName == _T("Host") ) m_sHost = pstrValue;
   if( sName == _T("Username") ) m_sUsername = pstrValue;
   if( sName == _T("Password") ) m_sPassword = pstrValue;
   if( sName == _T("Port") ) m_lPort = _ttol(pstrValue);
   if( sName == _T("Passive") ) m_bPassive = (_tcscmp(pstrValue, _T("true")) == 0);
   if( sName == _T("CompatibilityMode") ) m_bCompatibilityMode = (_tcscmp(pstrValue, _T("true")) == 0);
   if( sName == _T("Proxy") ) m_sProxy = pstrValue;
   if( sName == _T("ConnectTimeout") ) m_lConnectTimeout = _ttol(pstrValue);
   m_sPath.TrimRight(_T("/"));
   if( m_lConnectTimeout <= 0 ) m_lConnectTimeout = 8;
}

bool CFtpProtocol::LoadFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE* ppOut, DWORD* pdwSize /* = NULL*/)
{
   ATLASSERT(pstrFilename);
   ATLASSERT(ppOut);

   *ppOut = NULL;
   if( pdwSize != NULL ) *pdwSize = 0;
   
   // Connected?
   if( !WaitForConnection() ) return false;

   CString sCurDir = GetCurPath();

   // Construct remote filename
   CString sFilename = pstrFilename; 
   if( pstrFilename[0] != '/' && sCurDir.GetLength() > 1 ) sFilename.Format(_T("%s/%s"), sCurDir, pstrFilename);
   sFilename.Replace(_T('\\'), _T('/'));

   // Open file and read it
   DWORD dwFlags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
   dwFlags |= bBinary ? FTP_TRANSFER_TYPE_BINARY : FTP_TRANSFER_TYPE_ASCII;
   HINTERNET hFile = ::FtpOpenFile(m_hFTP, sFilename, GENERIC_READ, dwFlags, 0);
   if( hFile == NULL ) return _TranslateError();

   const SIZE_T BUFFER_SIZE = 4000;
   CAutoFree<BYTE> buffer(BUFFER_SIZE);
   DWORD dwPos = 0;
   while( true ) {
      DWORD dwBytesRead = 0;
      BOOL bRes = ::InternetReadFile(hFile, buffer.GetData(), buffer.GetSize(), &dwBytesRead);
      if( !bRes ) {
         DWORD dwErr = ::GetLastError();
         if( *ppOut != NULL ) { free(*ppOut); *ppOut = NULL; }
         ::InternetCloseHandle(hFile);
         ::SetLastError(dwErr);
         return false;
      }
      if( bRes && dwBytesRead == 0 ) break;
      *ppOut = (LPBYTE) realloc(*ppOut, dwPos + dwBytesRead);
      if( pdwSize != NULL ) *pdwSize += dwBytesRead;
      memcpy(*ppOut + dwPos, buffer.GetData(), dwBytesRead);
      dwPos += dwBytesRead;
   }

   ::InternetCloseHandle(hFile);

   return true;
}

bool CFtpProtocol::SaveFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE pData, DWORD dwSize)
{
   ATLASSERT(pstrFilename);
   ATLASSERT(pData);

   // Prevent save of an empty file
   if( dwSize == 0 ) {
      ::SetLastError(ERROR_EMPTY);
      return false;
   }

   // Connected?
   if( !WaitForConnection() ) return false;

   CString sCurDir = GetCurPath();

   // Construct remote filename
   CString sFilename = pstrFilename; 
   if( pstrFilename[0] != '/' && sCurDir.GetLength() > 1 ) sFilename.Format(_T("%s/%s"), sCurDir, pstrFilename);
   sFilename.Replace(_T('\\'), _T('/'));

   // Create and write file...
   DWORD dwFlags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
   dwFlags |= bBinary ? FTP_TRANSFER_TYPE_BINARY : FTP_TRANSFER_TYPE_ASCII;
   HINTERNET hFile = ::FtpOpenFile(m_hFTP, sFilename, GENERIC_WRITE, dwFlags, 0);
   if( hFile == NULL ) return _TranslateError();

   DWORD dwPos = 0;
   while( dwSize > 0 ) {
      DWORD dwBytesWritten = 0;
      BOOL bRes = ::InternetWriteFile(hFile, pData + dwPos, dwSize, &dwBytesWritten);
      if( !bRes ) {
         DWORD dwErr = ::GetLastError();
         ::InternetCloseHandle(hFile);
         ::SetLastError(dwErr);
         return false;
      }
      if( bRes && dwBytesWritten == 0 ) break;
      dwPos += dwBytesWritten;
      dwSize -= dwBytesWritten;
   }
   ::InternetCloseHandle(hFile);

   return true;
}

bool CFtpProtocol::DeleteFile(LPCTSTR pstrFilename)
{
   ATLASSERT(pstrFilename);
   // Connected?
   if( !WaitForConnection() ) return false;
   // Remove the file
   return ::FtpDeleteFile(m_hFTP, pstrFilename) == TRUE;
}

bool CFtpProtocol::SetCurPath(LPCTSTR pstrPath)
{
   ATLASSERT(pstrPath);
   if( !WaitForConnection() ) return false;
   bool bRes = ::FtpSetCurrentDirectory(m_hFTP, pstrPath) == TRUE;
   if( !bRes ) _TranslateError();
   return bRes;
}

CString CFtpProtocol::GetCurPath()
{
   CString sPath;
   sPath.Empty();
   if( !WaitForConnection() ) return _T("");
   TCHAR szPath[MAX_PATH] = { 0 };
   DWORD dwSize = MAX_PATH;
   BOOL bRes = ::FtpGetCurrentDirectory(m_hFTP, szPath, &dwSize);
   if( !bRes ) return _T("");
   return szPath;
}

bool CFtpProtocol::EnumFiles(CSimpleArray<WIN32_FIND_DATA>& aFiles, bool bUseCache)
{
   if( !WaitForConnection() ) return false;
   WIN32_FIND_DATA fd = { 0 };
   DWORD dwFlags = bUseCache ? 0 : INTERNET_FLAG_RELOAD;
   HINTERNET hFind = ::FtpFindFirstFile(m_hFTP, NULL, &fd, dwFlags, 0);
   if( hFind == NULL && ::GetLastError() == ERROR_NO_MORE_FILES ) return true;
   if( hFind == NULL ) return false;
   while( true ) {
      aFiles.Add(fd);
      if( !::InternetFindNextFile(hFind, &fd) ) break;
   }   
   ::InternetCloseHandle(hFind);
   return true;
}

CString CFtpProtocol::FindFile(LPCTSTR pstrFilename)
{
   // Connected?
   if( !WaitForConnection() ) return _T("");
   // Get file information
   // NOTE: We don't add the INTERNET_FLAG_RELOAD flag here because
   //       it's likely to search the static file pool.
   WIN32_FIND_DATA fd = { 0 };
   DWORD dwFlags = INTERNET_FLAG_DONT_CACHE;
   if( pstrFilename[0] == '/' || pstrFilename[0] == '.' ) {
      HINTERNET hFind = ::FtpFindFirstFile(m_hFTP, pstrFilename, &fd, dwFlags, 0);
      if( hFind != NULL ) {
         ::InternetCloseHandle(hFind);
         return pstrFilename;
      }
   }
   CString sPath = m_sSearchPath;
   while( !sPath.IsEmpty() ) {
      CString sSubPath = sPath.SpanExcluding(_T(";"));
      CString sFilename = sSubPath;
      if( sFilename.Right(1) != _T("/") ) sFilename += _T("/");
      sFilename += pstrFilename;
      DWORD dwLen = MAX_PATH;
      TCHAR szFilename[MAX_PATH] = { 0 };
      ::UrlCanonicalize(sFilename, szFilename, &dwLen, 0);
      HINTERNET hFind = ::FtpFindFirstFile(m_hFTP, szFilename, &fd, dwFlags, 0);
      if( hFind != NULL ) {
         ::InternetCloseHandle(hFind);
         return sFilename;
      }
      sPath = sPath.Mid(sSubPath.GetLength() + 1);
   }
   return _T("");
}

bool CFtpProtocol::WaitForConnection()
{
   // Wait for the thread to start and begin connecting to the FTP server.
   // Timeout after a few seconds. The thread is only a connect thread,
   // so when it stops, it either because of connect success or error.
   const DWORD SPAWNTIMEOUT = 5;
   DWORD dwTickStart = ::GetTickCount();
   while( m_thread.IsRunning() ) {
      if( ::GetTickCount() - dwTickStart > SPAWNTIMEOUT * 1000L ) {
         ::SetLastError(ERROR_TIMEOUT);
         return false;
      }
      PumpIdleMessages(200L);
   }
   if( !IsConnected() && !m_bCancel ) {
      // Did the server return some kind of error?
      if( m_dwErrorCode != 0 ) {
         ::SetLastError(m_dwErrorCode);
         return false;
      }
      // Attempt to reconnect to the remote host.
      // This will allow a periodically downed server to recover the connection.
      Start();
      dwTickStart = ::GetTickCount();
      ::Sleep(200L);
      while( m_thread.IsRunning() ) {
         if( m_bCancel 
             || ::GetTickCount() - dwTickStart > (DWORD) m_lConnectTimeout * 1000UL ) 
         {
            ::SetLastError(ERROR_TIMEOUT);
            return false;
         }
         PumpIdleMessages(200L);
      }
      // Check if the server returned an error this time!
      if( m_dwErrorCode != 0 ) {
         ::SetLastError(m_dwErrorCode);
         return false;
      }
      // At this point we can assume the connection is up
      // and running again...
   }

   ATLASSERT(m_hFTP);

   // If some period of time has passed since last
   // communication, then check if we've lost the line...
   const DWORD IDLETIMEOUT = 60L;
   if( m_dwLastCheck + (IDLETIMEOUT * 1000L) < ::GetTickCount() 
       && !m_bCancel ) 
   {
      m_dwLastCheck = ::GetTickCount();
      // Do a FTP NOOP command to check if connection is OK!
      // NOTE: Strange compile/link errors might occur with FtpCommand() on 
      //       systems without a recent Microsoft Platform SDK installed.
      BOOL bRes = ::FtpCommand(m_hFTP, FALSE, FTP_TRANSFER_TYPE_ASCII, _T("NOOP"), 0, NULL);
      if( !bRes ) {
         // Ooops, looks like a standard timeout reset.
         // Let's silently try to reconnect...
         Start();
         if( !WaitForConnection() ) return false;
      }
   }

   return true;
}

bool CFtpProtocol::_TranslateError()
{
   if( ::GetLastError() == ERROR_INTERNET_EXTENDED_ERROR ) {
      // Rats! MS Wininet usually just claims the server returned extended
      // error information, not trying to properly translate the error.
      // We'll convert basic FTP error codes ourselves...
      DWORD dwErr = 0;
      TCHAR szMessage[300] = { 0 };
      DWORD cchMax = 299;
      ::InternetGetLastResponseInfo(&dwErr, szMessage, &cchMax);
      ::SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
      if( _tcsstr(szMessage, _T("450 ")) != NULL ) ::SetLastError(ERROR_BUSY);
      if( _tcsstr(szMessage, _T("452 ")) != NULL ) ::SetLastError(ERROR_DISK_FULL);
      if( _tcsstr(szMessage, _T("550 ")) != NULL ) ::SetLastError(ERROR_FILE_NOT_FOUND);
      if( _tcsstr(szMessage, _T("552 ")) != NULL ) ::SetLastError(ERROR_DISK_FULL);
      if( _tcsstr(szMessage, _T("553 ")) != NULL ) ::SetLastError(ERROR_ACCESS_DENIED);
   }
   return false;
}

