#if !defined(AFX_GLOBALS_H__20030315_A892_7CB2_47EF_0080AD509054__INCLUDED_)
#define AFX_GLOBALS_H__20030315_A892_7CB2_47EF_0080AD509054__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "MiInfo.h"


//////////////////////////////////////////////////////////////
//

class CTextFile;
class CRemoteProject;


//////////////////////////////////////////////////////////////
//

#define PLUGIN_NAME        "Remote C++"
#define PLUGIN_DESCRIPTION "Allows compiling and debugging of remote C++ projects."

#define TERM_MARKER _T("###BVRDE###")


#define FR_WRAP  0x40000000
#define FR_INSEL 0x20000000

enum
{
   DEBUG_CMD_SET_CURLINE,
   DEBUG_CMD_SET_RUNNING,
   DEBUG_CMD_DEBUG_START,
   DEBUG_CMD_DEBUG_STOP,
   DEBUG_CMD_CLEAR_BREAKPOINTS,
   DEBUG_CMD_REQUEST_BREAKPOINTS,
   DEBUG_CMD_GET_CARET_TEXT,
   DEBUG_CMD_HOVERINFO,
   DEBUG_CMD_FINDTEXT,
   DEBUG_CMD_FOLDCURSOR,
};

typedef enum LAZYACTION
{
   LAZY_OPEN_VIEW = 1,
   LAZY_GUI_ACTION,
   LAZY_SHOW_MESSAGE,
   LAZY_SET_STATUSBARTEXT,
   LAZY_DEBUGCOMMAND,
   LAZY_SEND_VIEW_MESSAGE,
   LAZY_SET_DEBUG_BREAKPOINT,
   LAZY_DEBUG_START_EVENT,
   LAZY_DEBUG_STOP_EVENT,
   LAZY_DEBUG_KILL_EVENT,
   LAZY_DEBUG_INFO,
};

typedef enum GUIACTION
{
   GUI_ACTION_ACTIVATEVIEW = 1,
   GUI_ACTION_STOP_ANIMATION,
};

typedef struct
{
   LAZYACTION Action;
   HWND hWnd;
   TCHAR szFilename[MAX_PATH];
   long lLineNum;
   TCHAR szMessage[400];
   TCHAR szCaption[128];
   UINT iFlags;
   WPARAM wParam;
   CMiInfo MiInfo;
} LAZYDATA;


//////////////////////////////////////////////////////////////
//

typedef enum VT100COLOR
{
   VT100_DEFAULT = 0,
   VT100_HIDDEN = 8,
   VT100_BLACK = 30,
   VT100_RED = 31,
   VT100_GREEN = 32,
   VT100_YELLOW = 33,
   VT100_BLUE = 34,
   VT100_MAGENTA = 35,
   VT100_CYAN = 36,
   VT100_WHITE = 37,
};

class IOutputLineListener
{
public:
   virtual void OnIncomingLine(VT100COLOR nColor, LPCTSTR pstrText) = 0;
};

class IShellCallback
{
public:
   virtual bool AddLineListener(IOutputLineListener* pListener) = 0;
   virtual bool RemoveLineListener(IOutputLineListener* pListener) = 0;
   virtual void BroadcastLine(VT100COLOR nColor, LPCTSTR pstrText) = 0;
};

class IRemoteCommandProtocol
{
public:
   virtual void Init(CRemoteProject* pProject, IShellCallback* pCallback = NULL) = 0;
   virtual void Clear() = 0;
   virtual bool Load(ISerializable* pArc) = 0;
   virtual bool Save(ISerializable* pArc) = 0;
   virtual bool Start() = 0;
   virtual bool Stop() = 0;
   virtual void SignalStop() = 0;
   virtual bool IsConnected() const = 0;   
   virtual bool IsBusy() const = 0;
   //
   virtual bool ReadData(CString& s, DWORD dwTimeout = 0) = 0;
   virtual bool WriteData(LPCTSTR pstrData) = 0;
   virtual bool WriteSignal(BYTE bCmd) = 0;
   virtual bool WriteScreenSize(int w, int h) = 0;
   virtual bool WaitForConnection() = 0;
   //
   virtual CString GetParam(LPCTSTR pstrName) const = 0;
   virtual void SetParam(LPCTSTR pstrName, LPCTSTR pstrValue) = 0;
};

class IRemoteFileProtocol
{
public:
   virtual void Clear() = 0;
   virtual bool Load(ISerializable* pArc) = 0;
   virtual bool Save(ISerializable* pArc) = 0;
   virtual bool Start() = 0;
   virtual bool Stop() = 0;
   virtual void SignalStop() = 0;
   virtual bool IsConnected() const = 0;   
   //
   virtual bool LoadFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE* ppOut, DWORD* pdwSize = NULL) = 0;
   virtual bool SaveFile(LPCTSTR pstrFilename, bool bBinary, LPBYTE ppOut, DWORD dwSize) = 0;  
   virtual bool SetCurPath(LPCTSTR pstrPath) = 0;
   virtual CString GetCurPath() = 0;
   virtual bool EnumFiles(CSimpleArray<WIN32_FIND_DATA>& aFiles) = 0;   
   //
   virtual CString GetParam(LPCTSTR pstrName) const = 0;
   virtual void SetParam(LPCTSTR pstrName, LPCTSTR pstrValue) = 0;
};


//////////////////////////////////////////////////////////////
//

void SecClearPassword();
CString SecGetPassword();

void AppendRtfText(CRichEditCtrl ctrlEdit, LPCTSTR pstrText, DWORD dwMask = 0, DWORD dwEffects = 0, COLORREF clrText = 0);
void GenerateError(IDevEnv* pDevEnv, UINT nErr);
CString GetSystemErrorText(DWORD dwErr);
BOOL MergeMenu(HMENU hMenu, HMENU hMenuSource, UINT nPosition);
CString ToString(long lValue);

CString GetFileTypeFromFilename(LPCTSTR pstrFilename);
CTextFile* CreateViewFromFilename(IDevEnv* pDevEnv, LPCTSTR pstrFilename, CRemoteProject* pCppProject, IProject* pProject, IElement* pParent);

void PumpIdleMessages();


#endif // !defined(AFX_GLOBALS_H__20030315_A892_7CB2_47EF_0080AD509054__INCLUDED_)
