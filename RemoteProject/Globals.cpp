
#include "StdAfx.h"
#include "resource.h"

#include "PasswordDlg.h"

#include "Globals.h"
#include "Files.h"


// Here we store the user's password after being prompted
// so we needn't prompt again. This implies that the same
// password should be used for both FTP and Telnet connection!
TCHAR g_szPassword[100];


CString ToString(long lValue)
{
   TCHAR szBuffer[32];
   ::wsprintf(szBuffer, _T("%ld"), lValue);
   return szBuffer;
}

void ConvertToCrLf(CString& s)
{
   s.Replace(_T("\\n"), _T("\r\n"));
}

CString ConvertFromCrLf(const CString& s)
{
   CString sRes = s;
   sRes.Remove(_T('\r'));
   sRes.Replace(_T("\n"), _T("\\n"));
   return sRes;
}



void SecClearPassword()
{
   // BUG: Protect this with semaphore!
   _tcscpy(g_szPassword, _T(""));
}

CString SecGetPassword()
{
   // Prompts for password if not known
   static CComAutoCriticalSection s_cs;
   s_cs.Lock();
   // If we already have a password, then use it
   CString sPassword = g_szPassword;
   if( !sPassword.IsEmpty() ) {      
      s_cs.Unlock();
      return sPassword;
   }
   // Prompt user for password
   CWaitCursor cursor;
   CPasswordDlg dlg;
   if( dlg.DoModal() != IDOK ) {
      s_cs.Unlock();
      return _T("");
   }
   sPassword = dlg.GetPassword();
   _tcscpy(g_szPassword, sPassword);
   s_cs.Unlock();
   return sPassword;
}


void AppendRtfText(CRichEditCtrl ctrlEdit, LPCTSTR pstrText, DWORD dwMask /*= 0*/, DWORD dwEffects /*= 0*/, COLORREF clrText /*= 0*/)
{
   ATLASSERT(ctrlEdit.IsWindow());
   ATLASSERT(!::IsBadStringPtr(pstrText,-1));
   // Yield control?
   if( ::InSendMessage() ) ::ReplyMessage(TRUE);
   // Get ready to append more text...
   ctrlEdit.HideSelection(TRUE);
   // Remove top lines if we've filled out the buffer
   GETTEXTLENGTHEX gtlx = { GTL_DEFAULT | GTL_CLOSE, 1200 };
   while( ctrlEdit.GetTextLengthEx(&gtlx) > ctrlEdit.GetLimitText() - 3000 ) {     
      LONG iStartPos = ctrlEdit.LineIndex(0);
      LONG iEndPos = ctrlEdit.LineIndex(1);
      ctrlEdit.SetSel(iStartPos, iEndPos);
      ctrlEdit.ReplaceSel(_T(""));
   }
   // Append line
   LONG iStartPos = 0;
   LONG iEndPos = 0;
   LONG iDummy = 0;
   CHARFORMAT cf;
   cf.cbSize = sizeof(CHARFORMAT);
   cf.dwMask = dwMask;;
   cf.dwEffects = dwEffects;
   cf.crTextColor = clrText;
   ctrlEdit.SetSel(-1, -1);
   ctrlEdit.GetSel(iStartPos, iDummy);
   ctrlEdit.ReplaceSel(pstrText);
   ctrlEdit.GetSel(iDummy, iEndPos);
   ctrlEdit.SetSel(iStartPos, iEndPos);
   ctrlEdit.SetSelectionCharFormat(cf);
   ctrlEdit.HideSelection(FALSE);
   ctrlEdit.SetSel(-1, -1);
}


void GenerateError(IDevEnv* pDevEnv, UINT nErr, DWORD dwErr /*= (DWORD)-1*/)
{
   // Create error message from system error code
   ATLASSERT(pDevEnv);
   ATLASSERT(nErr);
   if( dwErr == (DWORD)-1 ) dwErr = ::GetLastError();
   CString sMsg(MAKEINTRESOURCE(nErr));
   ATLASSERT(!sMsg.IsEmpty());
   if( dwErr != 0 ) {
      CString sTemp(MAKEINTRESOURCE(IDS_ERR_LASTERROR));
      sMsg += sTemp + GetSystemErrorText(dwErr);
   }
   HWND hWnd = ::GetActiveWindow();
   CString sCaption(MAKEINTRESOURCE(IDS_CAPTION_ERROR));
   pDevEnv->ShowMessageBox(hWnd, sMsg, sCaption, MB_ICONEXCLAMATION | MB_MODELESS);
}


CString GetSystemErrorText(DWORD dwErr)
{
   LPVOID lpMsgBuf = NULL;
   ::FormatMessage( 
       FORMAT_MESSAGE_ALLOCATE_BUFFER | 
       FORMAT_MESSAGE_FROM_SYSTEM | 
       FORMAT_MESSAGE_IGNORE_INSERTS,
       NULL,
       dwErr,
       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
       (LPTSTR) &lpMsgBuf,
       0,
       NULL);
   if( lpMsgBuf == NULL ) 
   {
      static LPCTSTR pstrModules[] = 
      { 
         _T("WININET"), 
         _T("WSOCK32"), 
         NULL 
      };
      for( LPCTSTR* ppstr = pstrModules; lpMsgBuf == NULL && *ppstr; ppstr++ ) {
         ::FormatMessage( 
             FORMAT_MESSAGE_ALLOCATE_BUFFER | 
             FORMAT_MESSAGE_FROM_HMODULE | 
             FORMAT_MESSAGE_IGNORE_INSERTS,
             ::GetModuleHandle(*ppstr),
             dwErr,
             MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
             (LPTSTR) &lpMsgBuf,
             0,
             NULL);
      }
   }
   CString s = (LPCTSTR) lpMsgBuf;
   s.Remove('\r');
   s.Replace(_T("\n"), _T(" "));
   // Free the buffer.
   if( lpMsgBuf ) ::LocalFree(lpMsgBuf);
   return s;
}

BOOL MergeMenu(HMENU hMenu, HMENU hMenuSource, UINT nPosition)
{
   ATLASSERT(::IsMenu(hMenu));
   ATLASSERT(::IsMenu(hMenuSource));
   // Get the HMENU of the popup
   if( hMenu == NULL ) return FALSE;
   if( hMenuSource == NULL ) return FALSE;
   // Make sure that we start with only one separator menu-item
   UINT iStartPos = 0;
   if( (::GetMenuState(hMenuSource, 0, MF_BYPOSITION) & (MF_SEPARATOR|MF_POPUP)) == MF_SEPARATOR ) {
      if( (nPosition == 0) || 
          (::GetMenuState(hMenu, nPosition - 1, MF_BYPOSITION) & MF_SEPARATOR) != 0 ) 
      {
         iStartPos++;
      }
   }
   // Go...
   UINT nMenuItems = ::GetMenuItemCount(hMenuSource);
   for( UINT i = iStartPos; i < nMenuItems; i++ ) {
      // Get state information
      UINT state = ::GetMenuState(hMenuSource, i, MF_BYPOSITION);
      TCHAR szItemText[256] = { 0 };
      int nLen = ::GetMenuString(hMenuSource, i, szItemText, (sizeof(szItemText) / sizeof(TCHAR)) - 1, MF_BYPOSITION);
      // Is this a separator?
      if( (state & MF_POPUP) != 0 ) {
         // Strip the HIBYTE because it contains a count of items
         state = (LOBYTE(state)) | MF_POPUP;
         // Then create the new submenu by using recursive call
         HMENU hSubMenu = ::CreateMenu();
         MergeMenu(hSubMenu, ::GetSubMenu(hMenuSource, i), 0);
         ATLASSERT(::GetMenuItemCount(hSubMenu)>0);
         // Non-empty popup -- add it to the shared menu bar
         ::InsertMenu(hMenu, nPosition++, state | MF_BYPOSITION, (UINT) hSubMenu, szItemText);
      }
      else if( (state & MF_SEPARATOR) != 0 ) {
         ::InsertMenu(hMenu, nPosition++, state | MF_STRING | MF_BYPOSITION, 0, _T(""));
      }
      else if( nLen > 0 ) {
         // Only non-empty items should be added
         ATLASSERT(szItemText[0]!='\0');
         // Here the state does not contain a count in the HIBYTE
         ::InsertMenu(hMenu, nPosition++, state | MF_BYPOSITION, ::GetMenuItemID(hMenuSource, i), szItemText);
      }
   }
   return TRUE;
}


CString GetFileTypeFromFilename(LPCTSTR pstrFilename)
{
   // Find file type based on file known extensions
   CString sFilename = pstrFilename;
   sFilename.MakeLower();
   TCHAR szExtension[MAX_PATH];
   _tcscpy(szExtension, sFilename);
   CString sExtension = ::PathFindExtension(szExtension);
   CString sKey;
   sKey.Format(_T("file.mappings%s"), sExtension);
   TCHAR szValue[200] = { 0 };
   if( _pDevEnv->GetProperty(sKey, szValue, 199) ) return szValue;  
   if( sExtension == _T(".") ) return _T("makefile");
   if( sFilename.Find(_T("make")) >= 0 ) return _T("makefile");
   if( sFilename.Find(_T(".mak")) >= 0 ) return _T("makefile");
   return _T("text");
}


CTextFile* CreateViewFromFilename(IDevEnv* pDevEnv, 
                                  LPCTSTR pstrFilename, 
                                  CRemoteProject* pCppProject, 
                                  IProject* pProject, 
                                  IElement* pParent)
{
   // Create new view object from type
   CString sType = GetFileTypeFromFilename(pstrFilename);
   if( sType == _T("cpp") ) return new CCppFile(pCppProject, pProject, pParent);
   if( sType == _T("header") ) return new CHeaderFile(pCppProject, pProject, pParent);
   if( sType == _T("makefile") ) return new CMakeFile(pCppProject, pProject, pParent);
   if( sType == _T("java") ) return new CJavaFile(pCppProject, pProject, pParent);
   if( sType == _T("basic") ) return new CBasicFile(pCppProject, pProject, pParent);
   if( sType == _T("pascal") ) return new CPascalFile(pCppProject, pProject, pParent);
   if( sType == _T("python") ) return new CPythonFile(pCppProject, pProject, pParent);
   if( sType == _T("perl") ) return new CPerlFile(pCppProject, pProject, pParent);
   if( sType == _T("xml") ) return new CXmlFile(pCppProject, pProject, pParent);
   if( sType == _T("html") ) return new CHtmlFile(pCppProject, pProject, pParent);
   if( sType == _T("php") ) return new CPhpFile(pCppProject, pProject, pParent);
   if( sType == _T("asp") ) return new CAspFile(pCppProject, pProject, pParent);
   return new CTextFile(pCppProject, pProject, pParent);
}


void PumpIdleMessages()
{
   HCURSOR hCursor = ::GetCursor();
   MSG msg = { 0 };
   while( ::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) ) { 
      switch( msg.message ) {
      case WM_PAINT:
      case WM_NCPAINT:
      case WM_MOUSEMOVE:
      case WM_NCMOUSEMOVE:
      case WM_TIMER:
      case 0x0118:    // WM_SYSTIMER (caret blink)
      case WM_KEYUP:  // Eats the remaining accellerator-keypress.
                      // Probably not a very good idea to put here!
         ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
         ::DispatchMessage(&msg); 
         break;
      default:
         if( msg.message >= WM_USER ) {
            ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
            ::DispatchMessage(&msg); 
         }
         else {
            ::SetCursor(hCursor);
            return;
         }
      }
   }
   ::SetCursor(hCursor);
}

