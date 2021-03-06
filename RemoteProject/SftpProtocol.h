#if !defined(AFX_SFTPPROTOCOL_H__20031123_EF85_33D9_7E7A_0080AD509054__INCLUDED_)
#define AFX_SFTPPROTOCOL_H__20031123_EF85_33D9_7E7A_0080AD509054__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include "cryptlib_wrapper.h"


////////////////////////////////////////////////////////
//

class CSftpProtocol;


////////////////////////////////////////////////////////
//

class CSftpThread : public CThreadImpl<CSftpThread>
{
public:
   DWORD Run();

   CSftpProtocol* m_pManager;
};


////////////////////////////////////////////////////////
//

class CSftpProtocol : public IRemoteFileProtocol
{
friend CSftpThread;
public:
   CSftpProtocol();
   virtual ~CSftpProtocol();

// IRemoteFileProtocol
public:
   void Clear();
   bool Load(ISerializable* pArc);
   bool Save(ISerializable* pArc);

   bool Start();
   bool Stop();
   void SignalStop();
   bool IsConnected() const;

   bool LoadFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE* ppOut, DWORD* pdwSize = NULL);
   bool SaveFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE ppOut, DWORD dwSize);
   bool DeleteFile(LPCTSTR pstrFilename);
   bool SetCurPath(LPCTSTR pstrPath);
   CString GetCurPath();
   CString FindFile(LPCTSTR pstrFilename);
   bool EnumFiles(CSimpleArray<WIN32_FIND_DATA>& aFiles, bool bUseCache);

   CString GetParam(LPCTSTR pstrName) const;
   void SetParam(LPCTSTR pstrName, LPCTSTR pstrValue);

   bool WaitForConnection();

protected:
   DWORD _SendInit();
   CString _ResolvePath(LPCTSTR pstrPath);
   bool _CheckFileExists(LPCTSTR pstrFilename);
   int _ReadData(CRYPT_SESSION cryptSession, LPVOID pData, int iMaxSize);
   int _WriteData(CRYPT_SESSION cryptSession, LPCVOID pData, int iSize);

protected:
   CString m_sHost;
   CString m_sUsername;
   CString m_sPassword;
   long m_lPort;
   CString m_sPath;
   CString m_sProxy;
   CString m_sSearchPath;
   BOOL m_bPassive;
   BOOL m_bCompatibilityMode;
   long m_lConnectTimeout;
   //
   CSftpThread m_thread;
   CString m_sCurDir;
   BYTE m_iVersion;
   DWORD m_dwMsgId;
   volatile CRYPT_SESSION m_cryptSession;
   volatile DWORD m_dwErrorCode;
   volatile bool m_bConnected;
};


#endif // !defined(AFX_SFTPPROTOCOL_H__20031123_EF85_33D9_7E7A_0080AD509054__INCLUDED_)

