
#include "StdAfx.h"
#include "resource.h"

#include "HexEditor.h"
#include "Files.h"

#pragma code_seg( "MISC" )

#define ROUND(x) (x / m_iDataSize * m_iDataSize)


//////////////////////////////////////////////////////////////////////////////
// Constructor / destructor

CHexEditorCtrl::CHexEditorCtrl() : 
   m_dwPos(0), 
   m_bShowAddress(true),
   m_bShowData(true),
   m_bShowAscii(true),
   m_bDataActive(true),
   m_iDataSize(1),
   m_dwDigitOffset(0),
   m_cInvalidAscii('.'),
   m_dwSelStart((DWORD)-1),
   m_dwSelEnd((DWORD)-1),
   m_bReadOnly(FALSE)
{
   ::ZeroMemory(&m_rcData, sizeof(m_rcData));
   ::ZeroMemory(&m_rcAscii, sizeof(m_rcAscii));
   m_szMargin.cx = 3;
   m_szMargin.cy = 2;
   m_accel.LoadAccelerators(IDR_ACCELERATOR);
}

CHexEditorCtrl::~CHexEditorCtrl()
{
}


//////////////////////////////////////////////////////////////////////////////
// Operations

void CHexEditorCtrl::Init(IProject* pProject, IView* pView)
{
   m_pProject = pProject;
   m_pView = pView;
}

BOOL CHexEditorCtrl::PreTranslateMessage(MSG* pMsg)
{
   return m_accel.TranslateAccelerator(m_hWnd, pMsg);
}

void CHexEditorCtrl::OnIdle(IUpdateUI* pUIBase)
{
   pUIBase->UIEnable(ID_EDIT_UNDO, CanUndo());
   pUIBase->UIEnable(ID_EDIT_COPY, CanCopy());
   pUIBase->UIEnable(ID_EDIT_CUT, FALSE);
   pUIBase->UIEnable(ID_EDIT_PASTE, FALSE);
   pUIBase->UIEnable(ID_EDIT_DELETE, FALSE);
   pUIBase->UIEnable(ID_FILE_PRINT, FALSE);
   pUIBase->UIEnable(ID_EDIT_FIND, FALSE);
   pUIBase->UIEnable(ID_DATASIZE_BYTE, TRUE);
   pUIBase->UIEnable(ID_DATASIZE_WORD, TRUE);
   pUIBase->UIEnable(ID_DATASIZE_DWORD, TRUE);
   pUIBase->UISetCheck(ID_DATASIZE_BYTE, GetDataSize() == 1);
   pUIBase->UISetCheck(ID_DATASIZE_WORD, GetDataSize() == 2);
   pUIBase->UISetCheck(ID_DATASIZE_DWORD, GetDataSize() == 4);
}

void CHexEditorCtrl::OnFinalMessage(HWND /*hWnd*/)
{
   if( m_pProject == NULL ) delete m_pView;
}

BOOL CHexEditorCtrl::SetFilename(LPCTSTR pstrFilename)
{
   ATLASSERT(::IsWindow(m_hWnd));
   m_File.Close();
   if( !m_File.Open(pstrFilename) ) return FALSE;
   m_sFilename = pstrFilename;
   m_aUndostack.RemoveAll();
   m_dwPos = 0;
   SetScrollPos(SB_VERT, 0);
   PostMessage(WM_SIZE);                    // Resize scrollbars
   PostMessage(WM_SETFOCUS);                // Create caret
   m_dwSelStart = m_dwSelEnd = (DWORD) -1;  // See comment in DoPaint() for explanation
   return Invalidate();
}

BOOL CHexEditorCtrl::GetModify() const
{
   return CanUndo();
}

BOOL CHexEditorCtrl::Undo()
{
   if( m_aUndostack.GetSize() == 0 ) return FALSE;
   // Undo all changes to this position. This solves problems
   // where only one digit gets undone at a time.
   DWORD dwPos = ROUND(m_aUndostack[ m_aUndostack.GetSize() - 1 ].dwPos);
   while( m_aUndostack.GetSize() > 0 
          && ROUND(m_aUndostack[ m_aUndostack.GetSize() - 1 ].dwPos) == dwPos ) 
   {
      UNDOENTRY entry = m_aUndostack[ m_aUndostack.GetSize() - 1 ];
      m_aUndostack.RemoveAt( m_aUndostack.GetSize() - 1 );
      LPBYTE pData = m_File.GetData();
      *(pData + entry.dwPos) = entry.bValue;
   }
   SetSel(dwPos, dwPos);
   return Invalidate();
}

void CHexEditorCtrl::EmptyUndoBuffer()
{
   m_aUndostack.RemoveAll();
}

BOOL CHexEditorCtrl::CanUndo() const
{
   return m_aUndostack.GetSize() > 0;
}

BOOL CHexEditorCtrl::CanCopy() const
{
   return m_dwSelStart != m_dwSelEnd;
}

BOOL CHexEditorCtrl::GetReadOnly() const
{
   return m_bReadOnly;
}

void CHexEditorCtrl::SetReadOnly(BOOL bReadOnly)
{
   m_bReadOnly = bReadOnly;
}

int CHexEditorCtrl::GetLineHeight() const
{
   ATLASSERT(::IsWindow(m_hWnd));
   return m_tmEditor.tmHeight;
}

int CHexEditorCtrl::GetLinesPrPage() const
{
   ATLASSERT(::IsWindow(m_hWnd));
   RECT rcClient = { 0 };
   GetClientRect(&rcClient);
   return (rcClient.bottom - rcClient.top) / GetLineHeight();
}

void CHexEditorCtrl::SetDataSize(int iSize)
{
   ATLASSERT(::IsWindow(m_hWnd));
   ATLASSERT(iSize==1 || iSize==2 || iSize==4);   // BYTE / WORD / DWORD
   ATLASSERT((BYTES_PR_LINE % iSize)==0);         // Integral, please
   m_iDataSize = (BYTE) iSize;
   Invalidate();
}

int CHexEditorCtrl::GetDataSize() const
{
   return m_iDataSize;
}

void CHexEditorCtrl::SetInvalidChar(TCHAR ch)
{
   ATLASSERT(::IsWindow(m_hWnd));
   m_cInvalidAscii = ch;
   Invalidate();
}

SIZE CHexEditorCtrl::GetMargins() const
{
   return m_szMargin;
}

void CHexEditorCtrl::SetMargine(SIZE szMargins)
{
   ATLASSERT(::IsWindow(m_hWnd));
   m_szMargin = szMargins;
   Invalidate();
}

void CHexEditorCtrl::SetDislayOptions(BOOL bShowAddress, BOOL bShowData, BOOL bShowAscii)
{
   ATLASSERT(::IsWindow(m_hWnd));
   m_bShowAddress = bShowAddress == TRUE;
   m_bShowData = bShowData == TRUE;
   m_bShowAscii = bShowAscii == TRUE;
   Invalidate();
}

BOOL CHexEditorCtrl::SetSel(DWORD dwStart, DWORD dwEnd, BOOL bNoScroll /*= FALSE*/)
{
   ATLASSERT(::IsWindow(m_hWnd));
   if( !m_File.IsOpen() ) return FALSE;
   if( dwEnd == (DWORD) -1 ) dwEnd = m_File.GetSize();
   if( dwStart == dwEnd && dwEnd >= m_File.GetSize() ) dwEnd = m_File.GetSize() - 1;
   if( dwStart != dwEnd && dwEnd > m_File.GetSize() ) dwEnd = m_File.GetSize();
   if( dwStart >= m_File.GetSize() ) dwStart = m_File.GetSize() - 1;
   dwStart = ROUND(dwStart);
   dwEnd = ROUND(dwEnd);
   if( dwEnd == dwStart && m_dwSelStart != m_dwSelEnd ) ShowCaret();
   if( dwEnd != dwStart && m_dwSelStart == m_dwSelEnd ) HideCaret();
   m_dwSelStart = dwStart;
   m_dwSelEnd = dwEnd;
   m_dwDigitOffset = 0;
   if( !bNoScroll ) RecalcPosition(dwEnd);
   if( dwStart == dwEnd ) RecalcCaret();
   return Invalidate();
}

void CHexEditorCtrl::GetSel(DWORD& dwStart, DWORD& dwEnd) const
{
   dwStart = m_dwSelStart;
   dwEnd = m_dwSelEnd;
   if( dwStart > dwEnd) {        // Return values in normalized form
      DWORD dwTemp = dwStart;
      dwStart = dwEnd;
      dwEnd = dwTemp;
   }
}


//////////////////////////////////////////////////////////////////////////////
// Message handlers

LRESULT CHexEditorCtrl::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   if( !m_fontEditor.IsNull() ) m_fontEditor.DeleteObject();
   CLogFont lf = AtlGetStockFont(OEM_FIXED_FONT);
   _tcscpy(lf.lfFaceName, _T("Lucida Console"));     // This is the font we prefer
   lf.lfPitchAndFamily = FIXED_PITCH;                // Let Windows find a fixed-width font
   m_fontEditor.CreateFontIndirect(&lf);

   CClientDC dc = m_hWnd;
   HFONT hOldFont = dc.SelectFont(m_fontEditor);
   dc.GetTextMetrics(&m_tmEditor);
   dc.SelectFont(hOldFont);

   ModifyStyle(0, WS_VSCROLL);
   SetScrollPos(SB_VERT, 0, TRUE);

   SendMessage(WM_SETTINGCHANGE);

   return 0;
}

LRESULT CHexEditorCtrl::OnQueryEndSession(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{   
   if( GetModify() ) {
      TCHAR szBuffer[32] = { 0 };
      _pDevEnv->GetProperty(_T("editors.general.savePrompt"), szBuffer, 31);
      if( _tcscmp(szBuffer, _T("true")) == 0 ) {
         if( IDNO == _pDevEnv->ShowMessageBox(m_hWnd, CString(MAKEINTRESOURCE(IDS_SAVEFILE)), CString(MAKEINTRESOURCE(IDS_CAPTION_QUESTION)), MB_ICONINFORMATION | MB_YESNO) ) {
            while( Undo() ) /* */;
            return TRUE;
         }
      }
      if( SendMessage(WM_COMMAND, MAKEWPARAM(ID_FILE_SAVE, 0)) != 0 ) return FALSE;
   }
   return TRUE;
}

LRESULT CHexEditorCtrl::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   if( m_File.IsOpen() ) {
      CreateSolidCaret(2, m_tmEditor.tmHeight - 2);
      ShowCaret();
   }
   return 0;
}

LRESULT CHexEditorCtrl::OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   HideCaret();
   DestroyCaret();
   return 0;
}

LRESULT CHexEditorCtrl::OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   if( m_rcAscii.left == 0 && m_rcData.left == 0 ) return 0; // Not ready yet!
   // Determine what area the cursor is over and change
   // cursor shape...
   DWORD dwPos = GetMessagePos();
   POINT pt = { GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos) };
   ScreenToClient(&pt);
   DWORD dwDummy = 0;
   bool bDummy = false;
   ::SetCursor(::LoadCursor(NULL, MAKEINTRESOURCE(GetPosFromPoint(pt, dwDummy, bDummy) ? IDC_IBEAM : IDC_ARROW)));
   return TRUE;
}

LRESULT CHexEditorCtrl::OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   if( m_bReadOnly ) {
      ::MessageBeep((UINT)-1);
      return 0;
   }
   if( wParam < VK_SPACE ) return 0;
   if( m_dwSelStart != m_dwSelEnd ) SetSel(m_dwSelStart, m_dwSelStart);
   if( m_bDataActive ) {
      if( isdigit(wParam) || (toupper(wParam) >= 'A' && toupper(wParam) <= 'F') ) {
         BYTE b = (BYTE)( isdigit(wParam) ? wParam - '0' : toupper(wParam) - 'A' + 10 );
         AssignDigitValue(m_dwSelStart, m_dwDigitOffset, b);
      }
   }
   else {
      AssignCharValue(m_dwSelStart, m_dwDigitOffset, LOBYTE(wParam));
      if( HIBYTE(wParam) != 0 ) AssignCharValue(m_dwSelStart, m_dwDigitOffset, HIBYTE(wParam));
   }
   return 0;
}

LRESULT CHexEditorCtrl::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   if( GetCapture() == m_hWnd ) {
      ReleaseCapture();
      return 0;
   }
   DWORD dwPage = BYTES_PR_LINE * (DWORD) GetLinesPrPage();
   bool bCtrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
   bool bShift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
   switch( wParam ) {
   case VK_F6:
      PostMessage(WM_COMMAND, MAKEWPARAM(ID_NEXT_PANE, 0));
      return 0;
   case VK_DELETE:
      if( m_bDataActive && !m_bReadOnly ) {
         m_dwDigitOffset = 0;
         for( int i = 0; i < m_iDataSize * 2; i++ ) PostMessage(WM_CHAR, '0');
      }
      return 0;
   case VK_LEFT:
      if( m_dwSelEnd < m_iDataSize ) return 0;
      SetSel(bShift ? m_dwSelStart : m_dwSelEnd - m_iDataSize, m_dwSelEnd - m_iDataSize);
      return 0;
   case VK_RIGHT:
      if( m_dwSelStart + m_iDataSize > m_File.GetSize() ) return 0;
      SetSel(bShift ? m_dwSelStart : m_dwSelEnd + m_iDataSize, m_dwSelEnd + m_iDataSize);
      return 0;
   case VK_UP:
      if( bCtrl ) return SendMessage(WM_VSCROLL, SB_LINEUP);
      if( m_dwSelEnd < BYTES_PR_LINE ) return 0;
      SetSel(bShift ? m_dwSelStart : m_dwSelEnd - BYTES_PR_LINE, m_dwSelEnd - BYTES_PR_LINE);
      return 0;
   case VK_DOWN:
      if( bCtrl ) return SendMessage(WM_VSCROLL, SB_LINEDOWN);
      if( m_dwSelStart + BYTES_PR_LINE > m_File.GetSize() ) return 0;
      SetSel(bShift ? m_dwSelStart : m_dwSelEnd + BYTES_PR_LINE, m_dwSelEnd + BYTES_PR_LINE);
      return 0;
   case VK_HOME:
      if( bCtrl ) SetSel(bShift ? m_dwSelStart : 0, 0);
      else SetSel(bShift ? m_dwSelStart : m_dwSelEnd - (m_dwSelEnd % BYTES_PR_LINE), m_dwSelEnd - (m_dwSelEnd % BYTES_PR_LINE));
      return 0;
   case VK_END:
      if( bCtrl ) SetSel(bShift ? m_dwSelStart : m_File.GetSize() - 1, m_File.GetSize() - (bShift ? 0 : 1));
      else SetSel(bShift ? m_dwSelStart : (m_dwSelEnd | 0xF) + (bShift ? 1 : 0), (m_dwSelEnd | 0xF) + (bShift ? 1 : 0));
      return 0;
   case VK_PRIOR:
      if( bCtrl ) return SendMessage(WM_VSCROLL, SB_PAGEUP);
      if( m_dwSelEnd < dwPage ) SetSel(bShift ? m_dwSelStart : 0, 0);
      else SetSel(bShift ? m_dwSelStart : m_dwSelEnd - dwPage, m_dwSelEnd - dwPage);
      return 0;
   case VK_NEXT:
      if( bCtrl ) return SendMessage(WM_VSCROLL, SB_PAGEDOWN);
      SetSel(bShift ? m_dwSelStart : m_dwSelEnd + dwPage, m_dwSelEnd + dwPage);
      return 0;
   }
   return 0;
}

LRESULT CHexEditorCtrl::OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
   POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
   bool bDataActive = false;
   DWORD dwPos = 0;
   if( !GetPosFromPoint(pt, dwPos, bDataActive) ) return 0;
   m_bDataActive = bDataActive;
   SetSel(dwPos, dwPos + m_iDataSize);
   return 0;
}

LRESULT CHexEditorCtrl::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
   SetFocus();
   POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
   bool bDataActive = false;
   DWORD dwPos = 0;
   if( !GetPosFromPoint(pt, dwPos, bDataActive) ) return 0;
   m_bDataActive = bDataActive;
   SetSel(dwPos, dwPos);
   // If user is dragging the mouse, we'll initiate a selection...
   ClientToScreen(&pt);
   if( ::DragDetect(m_hWnd, pt) ) SetCapture();
   return 0;
}

LRESULT CHexEditorCtrl::OnLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
   if( GetCapture() != m_hWnd ) return 0;
   SendMessage(WM_MOUSEMOVE, wParam, lParam);
   ReleaseCapture();
   return 0;
}

LRESULT CHexEditorCtrl::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
   if( GetCapture() != m_hWnd ) return 0;
   POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
   // Mouse outside client rectangle? Let's scroll the view
   RECT rcClient = { 0 };
   GetClientRect(&rcClient);
   if( pt.y < 0 ) SendMessage(WM_VSCROLL, SB_LINEUP);
   if( pt.y > rcClient.bottom - rcClient.top ) SendMessage(WM_VSCROLL, SB_LINEDOWN);
   // Expand the selection if mouse is over a valid position?
   bool bDataActive = false;
   DWORD dwPos = 0;
   if( !GetPosFromPoint(pt, dwPos, bDataActive) ) return 0;
   if( m_bDataActive != bDataActive ) return 0;
   SetSel(m_dwSelStart, dwPos == 0 ? 0 : dwPos + m_iDataSize);
   return 0;
}

LRESULT CHexEditorCtrl::OnSettingChange(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
   CString sKey = _T("editors.binary.");

   TCHAR szBuffer[64] = { 0 };
   m_bReadOnly = false;
   if( _pDevEnv->GetProperty(sKey + _T("readOnly"), szBuffer, 63) ) m_bReadOnly = _tcscmp(szBuffer, _T("true")) == 0;

   Invalidate();

   bHandled = FALSE;
   return 0;
}

LRESULT CHexEditorCtrl::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   SetScrollRange(SB_VERT, 0, (int) (m_File.GetSize() / BYTES_PR_LINE) - GetLinesPrPage() + 1, TRUE);
   return 0;
}

LRESULT CHexEditorCtrl::OnVScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
   SCROLLINFO si = { sizeof(SCROLLINFO), SIF_POS | SIF_RANGE | SIF_TRACKPOS };
   GetScrollInfo(SB_VERT, &si);
   int nPos = m_dwPos / BYTES_PR_LINE;
   switch( LOWORD(wParam) ) {
   case SB_TOP:
      nPos = 0;
      break;
   case SB_BOTTOM:
      nPos = si.nMax;
      break;
   case SB_LINEUP:
      if( nPos > 0 ) nPos -= 1;
      break;
   case SB_LINEDOWN:
      nPos += 1;
      break;
   case SB_PAGEUP:
      if( nPos > GetLinesPrPage() ) nPos -= GetLinesPrPage(); else nPos = 0;
      break;
   case SB_PAGEDOWN:
      nPos += GetLinesPrPage();
      break;
   case SB_THUMBTRACK:      
   case SB_THUMBPOSITION:
      nPos = si.nTrackPos;
      break;
   }
   if( nPos < si.nMin ) nPos = si.nMin;
   if( nPos > si.nMax ) nPos = si.nMax;
   if( nPos == si.nPos ) return 0;
   SetScrollPos(SB_VERT, nPos, TRUE);
   m_dwPos = nPos * BYTES_PR_LINE;
   RecalcCaret();
   Invalidate();
   return 0;
}

LRESULT CHexEditorCtrl::OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
#if (_WIN32_WINNT >= 0x0400) || (_WIN32_WINDOWS > 0x0400) || defined(_WIN32_WCE)
   uMsg;
   int zDelta = (int) (short) HIWORD(wParam);
#else
   int zDelta = (uMsg == WM_MOUSEWHEEL) ? (int) (short) HIWORD(wParam) : (int) wParam;
#endif //!((_WIN32_WINNT >= 0x0400) || (_WIN32_WINDOWS > 0x0400) || defined(_WIN32_WCE))
   for( int i = 0; i < abs(zDelta / WHEEL_DELTA * 2); i++ ) SendMessage(WM_VSCROLL, zDelta > 0 ? SB_LINEUP : SB_LINEDOWN);
   return 0;
}

LRESULT CHexEditorCtrl::OnNextPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
   m_bDataActive = !m_bDataActive;
   RecalcCaret();
   Invalidate();
   return 0;
}

LRESULT CHexEditorCtrl::OnChangeView(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
   int iDataSize = 1;
   if( wID == ID_DATASIZE_WORD ) iDataSize = 2;
   if( wID == ID_DATASIZE_DWORD ) iDataSize = 4;
   SetDataSize(iDataSize);
   return 0;
}

LRESULT CHexEditorCtrl::OnEditUndo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
   Undo();
   return 0;
}

LRESULT CHexEditorCtrl::OnEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
   DWORD dwStart = 0;
   DWORD dwEnd = 0;
   GetSel(dwStart, dwEnd);
   if( dwStart == dwEnd ) return 0;
   if( dwEnd - dwStart > 0x10000 ) {
      if( IDNO == _pDevEnv->ShowMessageBox(m_hWnd, CString(MAKEINTRESOURCE(IDS_ERR_LARGECLIPBOARD)), CString(MAKEINTRESOURCE(IDS_CAPTION_WARNING)), MB_ICONQUESTION | MB_YESNO) ) return 0;
   }
   if( !::OpenClipboard(m_hWnd) ) return 0;
   LPBYTE pData = m_File.GetData();
   CString sText;
   sText.GetBuffer((dwEnd - dwStart) * 4);
   sText.ReleaseBuffer(0);
   if( m_bDataActive ) {
      TCHAR szBuffer[32];
      DWORD nCount = 0;
      for( DWORD i = dwStart; i < dwEnd; i++ ) {
         ::wsprintf(szBuffer, _T("%02X "), (long) *(pData + i));
         sText += szBuffer;
         if( (++nCount % BYTES_PR_LINE) == 0 ) sText += _T("\r\n");
      }
   }
   else {
      for( DWORD i = dwStart; i < dwEnd; i++ ) {
         TCHAR ch = *(pData + i);
         ch = isprint(ch) ? ch : m_cInvalidAscii;
         sText += ch;
      }
   }
#ifndef _UNICODE
   LPCSTR p = sText;
#else
   LPSTR p = (LPSTR) malloc( (sText.GetLength() + 1) * 2);
   AtlW2AHelper(p, sText, sText.GetLength() + 1);
#endif
   ::EmptyClipboard();
   HGLOBAL hglbCopy = GlobalAlloc(GMEM_DDESHARE, strlen(p) + 1);
   char* lptstrCopy = (char*) GlobalLock(hglbCopy);
   strcpy(lptstrCopy, p); 
   GlobalUnlock(hglbCopy);
   ::SetClipboardData(CF_TEXT, lptstrCopy); 
   ::CloseClipboard(); 
#ifdef _UNICODE
   free(p);
#endif
   return 0;
}


//////////////////////////////////////////////////////////////////////////////
// Implementation

BOOL CHexEditorCtrl::GetPosFromPoint(POINT pt, DWORD& dwPos, bool& bDataActive)
{
   ATLASSERT(::IsWindow(m_hWnd));
   ATLASSERT(m_rcData.left>0);   // Oops! Not initialized! Call UpdateWindow() or delay the call!!!
   // Get rectangles for columns. Expand them a bit so it's easier
   // to hit with the mouse...
   pt.x -= m_szMargin.cx;
   pt.y -= m_szMargin.cy;
   RECT rcData = m_rcData;
   RECT rcAscii = m_rcAscii;
   ::InflateRect(&rcData, 4, 0);
   ::InflateRect(&rcAscii, 4, 0);
   if( ::PtInRect(&rcData, pt) ) {
      ::OffsetRect(&rcData, -2, -2);
      int xpos = (pt.x - rcData.left) / (((m_iDataSize * 2) + 1) * m_tmEditor.tmAveCharWidth) * m_iDataSize;
      if( xpos < 0 ) xpos = 0;
      if( xpos > 15 ) xpos = 15;
      int ypos = (pt.y - rcData.top) / GetLineHeight();
      dwPos = m_dwPos + (DWORD) xpos + ((DWORD) ypos * BYTES_PR_LINE);
      bDataActive = true;
      return TRUE;
   }
   if( ::PtInRect(&rcAscii, pt) ) {
      ::OffsetRect(&rcAscii, 4, 0);
      int xpos = (pt.x - rcAscii.left) / m_tmEditor.tmAveCharWidth;
      if( xpos < 0 ) xpos = 0;
      if( xpos > 15 ) xpos = 15;
      int ypos = (pt.y - rcAscii.top) / GetLineHeight();
      dwPos = m_dwPos + (DWORD) xpos + ((DWORD) ypos * BYTES_PR_LINE);
      bDataActive = false;
      return TRUE;
   }
   return FALSE;
}

void CHexEditorCtrl::RecalcCaret()
{
   ATLASSERT(::IsWindow(m_hWnd));
   ATLASSERT(m_rcData.left>0 || m_rcAscii.left>0);   // Oops! Not initialized! Call UpdateWindow() or delay the call!!!
   // Selection-mode does not display a caret
   if( m_dwSelStart != m_dwSelEnd ) return;
   // We'll try to determine where to place the caret
   DWORD dwPos = m_dwSelStart;
   int ypos = m_szMargin.cy + (((dwPos - m_dwPos) / BYTES_PR_LINE) * GetLineHeight());
   if( m_bDataActive ) {
      int xpos = m_rcData.left;
      xpos += (dwPos % BYTES_PR_LINE) / m_iDataSize * m_tmEditor.tmAveCharWidth * ((m_iDataSize * 2) + 1);
      if( m_dwDigitOffset > 0 ) xpos += m_tmEditor.tmAveCharWidth * m_dwDigitOffset;
      ::SetCaretPos(xpos, ypos);
   }
   else {
      int xpos = m_rcAscii.left;
      xpos += ((dwPos % BYTES_PR_LINE) + (m_dwDigitOffset / 2)) * m_tmEditor.tmAveCharWidth;
      ::SetCaretPos(xpos, ypos);
   }
}

void CHexEditorCtrl::RecalcPosition(DWORD dwPos)
{
   // Is new selection-position out of bounds?
   // If so, we need to set a new view position.
   DWORD dwPage = (GetLinesPrPage() - 1) * BYTES_PR_LINE;
   if( dwPos < m_dwPos ) {
      m_dwPos = dwPos - (dwPos % BYTES_PR_LINE);
      SetScrollPos(SB_VERT, m_dwPos / BYTES_PR_LINE);
      Invalidate();
   }
   else if( dwPos > m_dwPos + dwPage ) {
      m_dwPos = dwPos - (dwPos % BYTES_PR_LINE);
      if( m_dwPos >= dwPage ) m_dwPos -= dwPage;
      else m_dwPos = 0;
      SetScrollPos(SB_VERT, m_dwPos / BYTES_PR_LINE);
      Invalidate();
   }
}

void CHexEditorCtrl::AssignDigitValue(DWORD& dwPos, DWORD& dwCursorPos, BYTE bValue)
{
   ATLASSERT(m_File.GetData()!=NULL);
   ATLASSERT(dwPos<m_File.GetSize());
   ATLASSERT(bValue<0x10);
   // Calculate new data value (byte oriented)
   LPBYTE pData = m_File.GetData();
   DWORD dwOffset = dwPos + (m_iDataSize - 1 - (dwCursorPos / 2));
   if( (dwCursorPos % 2) == 0 ) bValue = (BYTE) ((*(pData + dwOffset) & 0x0F) | (bValue << 4));
   else bValue = (BYTE) ((*(pData + dwOffset) & 0xF0) | bValue);
   // Create undo action
   UNDOENTRY undo = { dwOffset, *(pData + dwOffset) };
   m_aUndostack.Add(undo);
   // Assign value
   *(pData + dwOffset) = bValue;
   // Advance cursor (might need to only move the caret to next digit).
   DWORD dwTotalDigits = m_iDataSize * 2;
   if( ++m_dwDigitOffset >= dwTotalDigits ) SetSel(dwPos + m_iDataSize, dwPos + m_iDataSize);
   else RecalcCaret();
   Invalidate();
}

void CHexEditorCtrl::AssignCharValue(DWORD& dwPos, DWORD& dwCursorPos, BYTE bValue)
{
   ATLASSERT(m_File.GetData()!=NULL);
   ATLASSERT(dwPos<m_File.GetSize());
   // Calculate new data value (cursor moves one digit; a byte i 2 digits)
   LPBYTE pData = m_File.GetData();
   DWORD dwOffset = dwPos + (dwCursorPos / 2);
   // Create undo action
   UNDOENTRY undo = { dwOffset, *(pData + dwOffset) };
   m_aUndostack.Add(undo);
   // Assign value
   *(pData + dwOffset) = bValue;
   // Advance cursor (probably to next 'char' only)
   dwCursorPos += 2;
   DWORD dwTotalDigits = m_iDataSize * 2;
   if( dwCursorPos >= dwTotalDigits ) SetSel(dwPos + m_iDataSize, dwPos + m_iDataSize);
   else RecalcCaret();
   Invalidate();
}

void CHexEditorCtrl::DoPaint(CDCHandle dc)
{
   RECT rcClient = { 0 };
   GetClientRect(&rcClient);

   dc.FillSolidRect(&rcClient, ::GetSysColor(COLOR_WINDOW));

   rcClient.left += m_szMargin.cx;
   rcClient.top += m_szMargin.cy;

   HFONT hOldFont = dc.SelectFont(m_fontEditor);
   int nLines = GetLinesPrPage() + 1;
   int iHeight = GetLineHeight();
   
   LPBYTE pData = m_File.GetData();
   DWORD dwSize = m_File.GetSize();
   DWORD dwPos = m_dwPos;

   ::ZeroMemory(&m_rcData, sizeof(m_rcData));
   ::ZeroMemory(&m_rcAscii, sizeof(m_rcAscii));
   m_rcData.top = m_rcAscii.top = m_szMargin.cy;

   COLORREF clrTextH, clrBackH;
   COLORREF clrTextN = ::GetSysColor(m_bReadOnly ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT);
   COLORREF clrBackN = ::GetSysColor(COLOR_WINDOW);
   bool bHighlighted = false;

   COLORREF clrAddress = ::GetSysColor(COLOR_WINDOWTEXT);
   if( clrAddress == RGB(255,255,255) ) clrAddress = RGB(0,130,130);

   dc.SetBkMode(OPAQUE);

   DWORD dwSelStart = 0;
   DWORD dwSelEnd = 0;
   GetSel(dwSelStart, dwSelEnd);

   int ypos = rcClient.top;
   TCHAR szBuffer[64] = { 0 };
   for( int i = 0; i < nLines; i++ ) {
      int xpos = rcClient.left;
      // Draw address text
      if( m_bShowAddress && dwPos < dwSize ) 
      {
         dc.SetTextColor(clrAddress);
         dc.SetBkColor(clrBackN);
         ::wsprintf(szBuffer, _T("%08X  "), dwPos);
         RECT rcAddress = { xpos, ypos, xpos + 200, ypos + iHeight };
         dc.DrawText(szBuffer, -1, &rcAddress, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_NOCLIP);
         xpos += m_tmEditor.tmAveCharWidth * _tcslen(szBuffer);
      }
      // Draw hex values
      if( m_bShowData ) 
      {
         if( m_rcData.left == 0 ) m_rcData.left = xpos;

         clrBackH = ::GetSysColor(m_bDataActive ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
         clrTextH = ::GetSysColor(m_bDataActive ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);

         dc.SetTextColor(clrTextN);
         dc.SetBkColor(clrBackN);
         bHighlighted = false;

         for( DWORD j = 0; j < BYTES_PR_LINE; j += m_iDataSize ) {
            if( dwPos + j >= dwSize ) break;

            if( dwPos + j >= dwSelStart && dwPos + j < dwSelEnd ) {
               if( !bHighlighted ) {
                  dc.SetTextColor(clrTextH);
                  dc.SetBkColor(clrBackH);
                  bHighlighted = true;
               }
            }
            else {
               if( bHighlighted ) {
                  dc.SetTextColor(clrTextN);
                  dc.SetBkColor(clrBackN);
                  bHighlighted = false;
               }
            }

            LPTSTR p = szBuffer + (j == 0 ? 0 : 1);
            szBuffer[0] = ' ';
            switch( m_iDataSize ) {
            case 1:
               ::wsprintf( p, _T("%02X"), (long) *(pData + dwPos + j) );
               break;
            case 2:
               ::wsprintf( p, _T("%04X"), (long) *(LPWORD) (pData + dwPos + j) );
               break;
            case 4:
               ::wsprintf( p, _T("%08X"), (long) *(LPDWORD) (pData + dwPos + j) );
               break;
            default:
               ATLASSERT(false);
            }
            RECT rcData = { xpos, ypos, xpos + 200, ypos + iHeight };
            dc.DrawText(szBuffer, -1, &rcData, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_NOCLIP);
            xpos += m_tmEditor.tmAveCharWidth * _tcslen(szBuffer);
         }

         if( m_rcData.right == 0 ) m_rcData.right = xpos;
      }
      // Draw ASCII representation
      if( m_bShowAscii )
      {
         xpos += m_tmEditor.tmAveCharWidth * 3;

         if( m_rcAscii.left == 0 ) m_rcAscii.left = xpos;
         xpos = m_rcAscii.left;

         clrBackH = ::GetSysColor(m_bDataActive ? COLOR_BTNFACE : COLOR_HIGHLIGHT);
         clrTextH = ::GetSysColor(m_bDataActive ? COLOR_WINDOWTEXT : COLOR_HIGHLIGHTTEXT);

         dc.SetTextColor(clrTextN);
         dc.SetBkColor(clrBackN);
         bHighlighted = false;

         DWORD j = 0;
         for( ; j < BYTES_PR_LINE; j++ ) {
            if( dwPos + j >= dwSize ) break;

            if( dwPos + j >= dwSelStart && dwPos + j < dwSelEnd ) {
               if( !bHighlighted ) {
                  dc.SetTextColor(clrTextH);
                  dc.SetBkColor(clrBackH);
                  bHighlighted = true;
               }
            }
            else {
               if( bHighlighted ) {
                  dc.SetTextColor(clrTextN);
                  dc.SetBkColor(clrBackN);
                  bHighlighted = false;
               }
            }

            TCHAR ch = *(pData + dwPos + j);
            ch = isprint(ch) ? ch : m_cInvalidAscii;
            RECT rcAscii = { xpos, ypos, xpos + 100, ypos + iHeight };
            dc.DrawText(&ch, 1, &rcAscii, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_NOCLIP);
            xpos += m_tmEditor.tmAveCharWidth;
         }

         if( m_rcAscii.right == 0 ) m_rcAscii.right = xpos;
      }
      dwPos += BYTES_PR_LINE;
      ypos += iHeight;
   }

   dc.SelectFont(hOldFont);

   m_rcData.bottom = m_rcAscii.bottom = ypos;

   // HACK: Delayed activation of first caret position.
   //       We need the sizes of m_rcData and m_rcAscii before
   //       we can set selection (position caret)!
   if( m_dwSelStart == (DWORD) -1 ) SetSel(0, 0);
} 

