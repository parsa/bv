#if !defined(AFX_HEXEDITOR_H__20030608_F1CD_9419_8871_0080AD509054__INCLUDED_)
#define AFX_HEXEDITOR_H__20030608_F1CD_9419_8871_0080AD509054__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <zmouse.h>


class CMapFile
{
public:
   CFile f;
   HANDLE m_hMapFile;
   DWORD m_dwSize;
   LPBYTE m_p;

   CMapFile() : m_hMapFile(NULL), m_p(NULL), m_dwSize(0UL)
   {
   }

   ~CMapFile()
   {
      Close();
   }

   bool Open(LPCTSTR pstrFilename)
   {
      Close();
      if( !f.Open(pstrFilename, GENERIC_READ | GENERIC_WRITE) ) return false;
      m_dwSize = f.GetSize();
      m_hMapFile = ::CreateFileMapping(f, NULL, PAGE_READWRITE, 0, 0, NULL);
      if( m_hMapFile == NULL ) return false;
      m_p = (LPBYTE) ::MapViewOfFile(m_hMapFile, SECTION_ALL_ACCESS, 0, 0, 0);
      if( m_p == NULL ) return false;
      return true;
   }

   void Close()
   {
      if( m_p ) ::UnmapViewOfFile(m_p);
      m_p = NULL;
      m_dwSize = 0;
      if( m_hMapFile ) ::CloseHandle(m_hMapFile);
      m_hMapFile = NULL;
      f.Close();
   }
   
   inline bool IsOpen() const
   {
      return m_hMapFile != NULL;
   }
   
   inline LPBYTE GetData() const
   {
      return (LPBYTE) m_p;
   }
   
   inline DWORD GetSize() const
   {
      return m_dwSize;
   }
};


class CHexEditorCtrl : 
   public CWindowImpl<CHexEditorCtrl>,
   public COffscreenDraw<CHexEditorCtrl>
{
public:
   DECLARE_WND_CLASS_EX(_T("BVRDE_HexEditor"), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, COLOR_WINDOW + 1)

   enum { BYTES_PR_LINE = 16 };

   CHexEditorCtrl();
   virtual ~CHexEditorCtrl();

   typedef struct tagUNDOENTRY {
      DWORD dwPos;
      BYTE bValue;
   } UNDOENTRY;

   IProject* m_pProject;
   IView* m_pView;
   //
   CString m_sFilename;
   CMapFile m_File;                        // Memory mapped file image
   DWORD m_dwPos;                          // File Position of top of view
   CFont m_fontEditor;                     // Font used in Editor
   CAccelerator m_accel;                   // Keyboard shortcuts
   TEXTMETRIC m_tmEditor;                  // Font text metrics
   bool  m_bShowAddress;                   // Show address dump?
   bool  m_bShowData;                      // Show Hex dump?
   bool  m_bShowAscii;                     // Show ASCII dump?
   TCHAR m_cInvalidAscii;                  // Character to use when char is unprintable
   BYTE  m_iDataSize;                      // Size of address (1, 2, 4)
   RECT  m_rcData;                         // Rectangle of data (hex) area
   RECT  m_rcAscii;                        // Rectangle of ASCII area
   DWORD m_dwSelStart;                     // Selection start
   DWORD m_dwSelEnd;                       // Selection end
   DWORD m_dwDigitOffset;                  // Cursor digit-position inside value
   bool  m_bDataActive;                    // Selection is in data pane!
   SIZE  m_szMargin;                       // Left/top margin
   BOOL  m_bReadOnly;                      // Is control read-only?
   CSimpleArray<UNDOENTRY> m_aUndostack;   // Undo stack

   // Management

   void Init(IProject* pProject, IView* pView);
   BOOL PreTranslateMessage(MSG* pMsg);
   void OnIdle(IUpdateUI* pUIBase);
   void OnFinalMessage(HWND hWnd);

   // Operations

   BOOL SetFilename(LPCTSTR pstrFilename);
   BOOL GetModify() const;
   int GetLineHeight() const;
   int GetLinesPrPage() const;
   int GetDataSize() const;
   void SetDataSize(int iSize);
   void SetInvalidChar(TCHAR ch);
   void SetReadOnly(BOOL bReadOnly = TRUE);
   BOOL GetReadOnly() const;
   void SetDislayOptions(BOOL bShowAddress, BOOL bShowData, BOOL bShowAscii);
   BOOL SetSel(DWORD dwStart, DWORD dwEnd, BOOL bNoScroll = FALSE);
   void GetSel(DWORD& dwStart, DWORD& dwEnd) const;
   SIZE GetMargins() const;
   void SetMargine(SIZE szMargins);
   BOOL Undo();
   void EmptyUndoBuffer();

   BOOL CanUndo() const;
   BOOL CanCopy() const;

   // Implementation

   void RecalcCaret();
   void RecalcPosition(DWORD dwPos);
   void AssignDigitValue(DWORD& dwPos, DWORD& dwCursorPos, BYTE bValue);
   void AssignCharValue(DWORD& dwPos, DWORD& dwCursorPos, BYTE bValue);
   BOOL GetPosFromPoint(POINT pt, DWORD& dwPos, bool& bDataActive);

   ATLINLINE TCHAR toupper(WPARAM c) const { return (TCHAR)( c - 'a' + 'A' ); };
   ATLINLINE bool isdigit(WPARAM c) const { return c >= '0' && c <= '9'; };
   ATLINLINE bool isprint(TCHAR c) const { WORD w; ::GetStringTypeW(CT_CTYPE1, &c, 1, &w); return w != C1_CNTRL; };

   void DoPaint(CDCHandle dc);

   // Message map and handlers

   BEGIN_MSG_MAP(CHexEditorCtrl)
      MESSAGE_HANDLER(WM_CREATE, OnCreate)
      MESSAGE_HANDLER(WM_SIZE, OnSize)
      MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
      MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
      MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
      MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
      MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
      MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
      MESSAGE_HANDLER(WM_CHAR, OnChar)
      MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
      MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
      MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
      MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
      MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
      MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
      COMMAND_ID_HANDLER(ID_EDIT_COPY, OnEditCopy)
      COMMAND_ID_HANDLER(ID_EDIT_UNDO, OnEditUndo)
      COMMAND_ID_HANDLER(ID_NEXT_PANE, OnNextPane)
      COMMAND_ID_HANDLER(ID_DATASIZE_BYTE, OnChangeView)
      COMMAND_ID_HANDLER(ID_DATASIZE_WORD, OnChangeView)
      COMMAND_ID_HANDLER(ID_DATASIZE_DWORD, OnChangeView)
      CHAIN_MSG_MAP( COffscreenDraw<CHexEditorCtrl> )
   END_MSG_MAP()

   LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnQueryEndSession(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnVScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
   LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnKillFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);      
   LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);   
   LRESULT OnEditCopy(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
   LRESULT OnEditUndo(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
   LRESULT OnNextPane(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
   LRESULT OnChangeView(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
};


#endif // !defined(AFX_HEXEDITOR_H__20030608_F1CD_9419_8871_0080AD509054__INCLUDED_)
