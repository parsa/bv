
#include "StdAfx.h"
#include "resource.h"

#include "Project.h"


// Delayed UI processing

LRESULT CRemoteProject::OnProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled)
{
   // All interactions during compiling/debugging that could stir up
   // the user-interface needs to go through this method to avoid GUI/thread dead-locks.
   // This method is invoked solely through a PostMessage() to the main message-queue 
   // and executes a series of commands in a queue/list. This makes sure that all
   // GUI changes are called from the main thread only.

   if( _pDevEnv->GetSolution()->GetActiveProject() != this ) { bHandled = FALSE; return 0; }

   // For displaying a message-box asynchroniously
   CSimpleArray<CString> aDbgCmd;
   CString sMessage;
   CString sCaption;
   UINT iFlags;

   // Try to obtain the semaphore 
   // NOTE: WinNT specific API
   if( !::TryEnterCriticalSection(&_Module.m_csStaticDataInit) ) {
      // No need to block the thread; let's just re-post the request...
      m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
      return 0;
   }
   int i;
   for( i = 0; i < m_aLazyData.GetSize(); i++ ) {
      LAZYDATA& data = m_aLazyData[i];
      switch( data.Action ) {
      case LAZY_OPEN_VIEW:
         {
            OpenView(data.szFilename, data.lLineNum);
         }
         break;
      case LAZY_GUI_ACTION:
         {
            switch( data.wParam ) {
            case GUI_ACTION_ACTIVATEVIEW:
               {
                  CRichEditCtrl ctrlEdit = data.hWnd;
                  _pDevEnv->ActivateAutoHideView(ctrlEdit);
               }
               break;
            case GUI_ACTION_STOP_ANIMATION:
               {
                  _pDevEnv->PlayAnimation(FALSE, 0);
               }
               break;
            }
         }
         break;
      case LAZY_SHOW_MESSAGE:
         {
            // BUG: Multiple popup-messages will not be displayed
            //      because we overwrite the text variables here.
            sMessage = data.szMessage;
            sCaption = data.szCaption;
            iFlags = data.iFlags;
         }
         break;
      case LAZY_SET_STATUSBARTEXT:
         {
            _pDevEnv->ShowStatusText(ID_DEFAULT_PANE, data.szMessage, TRUE);
         }
         break;
      case LAZY_SEND_VIEW_MESSAGE:
         {
            // Broadcast message to all known views in project
            // HACK: Send this message to *ALL* editors - not just those
            //       belonging to this project. We'll like all editors to react
            //       on debugger line changes so all C++ files are updated.
            //for( int i = 0; i < m_aFiles.GetSize(); i++ ) m_aFiles[i]->SendMessage(WM_COMMAND, MAKEWPARAM(ID_DEBUG_EDIT_LINK, data.wParam), (LPARAM) &data);
            //m_wndMain.SendMessage(WM_COMMAND, MAKEWPARAM(ID_DEBUG_EDIT_LINK, data.wParam), (LPARAM) &data);
            CWindow wndMdiClient = _pDevEnv->GetHwnd(IDE_HWND_MDICLIENT);
            wndMdiClient.SendMessageToDescendants(WM_COMMAND, MAKEWPARAM(ID_DEBUG_EDIT_LINK, data.wParam), (LPARAM) &data);
         }
         break;
      case LAZY_DEBUGCOMMAND:
         {
            aDbgCmd.Add(CString(data.szMessage));
         }
         break;
      case LAZY_DEBUG_START_EVENT:
         {
            // Notify all views
            DelayedViewMessage(DEBUG_CMD_DEBUG_START);

            TCHAR szBuffer[32] = { 0 };
            _pDevEnv->GetProperty(_T("gui.debugViews.showStack"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_CALLSTACK, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showWatch"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_WATCH, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showThread"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_THREADS, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showRegister"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_REGISTERS, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showMemory"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_MEMORY, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showDisassembly"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_DISASM, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showVariable"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_VARIABLES, 0));
            _pDevEnv->GetProperty(_T("gui.debugViews.showBreakpoint"), szBuffer, 31);
            if( _tcscmp(szBuffer, _T("true")) == 0 ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_BREAKPOINTS, 0));
         }
         break;
      case LAZY_DEBUG_KILL_EVENT:
         {
            // Notify all views
            DelayedViewMessage(DEBUG_CMD_DEBUG_STOP);

            // If we're closing the debug session, then dispose
            // all debug views as well...

            TCHAR szBuffer[32] = { 0 };
            _tcscpy(szBuffer, (m_viewStack.IsWindow() && m_viewStack.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showStack"), szBuffer);
            _tcscpy(szBuffer, (m_viewWatch.IsWindow() && m_viewWatch.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showWatch"), szBuffer);
            _tcscpy(szBuffer, (m_viewThread.IsWindow() && m_viewThread.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showThread"), szBuffer);
            _tcscpy(szBuffer, (m_viewRegister.IsWindow() && m_viewRegister.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showRegister"), szBuffer);
            _tcscpy(szBuffer, (m_viewMemory.IsWindow() && m_viewMemory.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showMemory"), szBuffer);
            _tcscpy(szBuffer, (m_viewDisassembly.IsWindow() && m_viewDisassembly.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showDisassembly"), szBuffer);
            _tcscpy(szBuffer, (m_viewVariable.IsWindow() && m_viewVariable.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showVariable"), szBuffer);
            _tcscpy(szBuffer, (m_viewBreakpoint.IsWindow() && m_viewBreakpoint.IsWindowVisible()) ? _T("true") : _T("false"));
            _pDevEnv->SetProperty(_T("gui.debugViews.showBreakpoint"), szBuffer);

            if( m_viewStack.IsWindow() && m_viewStack.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_CALLSTACK, 0));
            if( m_viewWatch.IsWindow() && m_viewWatch.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_WATCH, 0));
            if( m_viewThread.IsWindow() && m_viewThread.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_THREADS, 0));
            if( m_viewRegister.IsWindow() && m_viewRegister.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_REGISTERS, 0));
            if( m_viewMemory.IsWindow() && m_viewMemory.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_MEMORY, 0));
            if( m_viewDisassembly.IsWindow() && m_viewDisassembly.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_DISASM, 0));
            if( m_viewVariable.IsWindow() && m_viewVariable.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_VARIABLES, 0));
            if( m_viewBreakpoint.IsWindow() && m_viewBreakpoint.IsWindowVisible() ) m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_VIEW_BREAKPOINTS, 0));
         }
         break;
      case LAZY_DEBUG_STOP_EVENT:
         {
            // Debugger stopped at some point (breakpoint, user break, etc).
            // Let's update the debug views with fresh values.

            if( m_viewBreakpoint.WantsData() ) {
               aDbgCmd.Add(CString(_T("-break-list")));
            }
            if( m_viewWatch.WantsData() ) {
               m_viewWatch.EvaluateValues(aDbgCmd);
            }
            if( m_viewThread.WantsData() || m_viewStack.WantsData() ) {
               aDbgCmd.Add(CString(_T("-thread-list-ids")));
            }
            if( m_viewStack.WantsData() ) {
               aDbgCmd.Add(CString(_T("-stack-list-frames")));
            }
            if( m_viewDisassembly.WantsData() ) {
               m_viewDisassembly.PopulateView(aDbgCmd);
            }
            if( m_viewVariable.WantsData() ) {
               switch( m_viewVariable.GetCurSel() ) {
               case 0:
                  aDbgCmd.Add(CString(_T("-stack-list-locals 1")));
                  break;
               case 1:
                  aDbgCmd.Add(CString(_T("-stack-list-arguments 1 0 0")));
                  break;                   
               }
            }
            if( m_viewRegister.WantsData() ) {
               if( m_viewRegister.GetNameCount() == 0 ) {
                  aDbgCmd.Add(CString(_T("-data-list-register-names")));
               }
               aDbgCmd.Add(CString(_T("-data-list-register-values x")));
            }
         }
         break;
      case LAZY_DEBUG_INFO:
         {
            // New debug information has arrived. Spread it around to the
            // debug views since they are the likely candidates for this data.
            // Most of these messages originates from the LAZY_DEBUG_STOP_EVENT handling
            // above anyway.

            if( m_viewStack.WantsData() ) m_viewStack.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewWatch.WantsData() ) m_viewWatch.SetInfo(data.szMessage, data.MiInfo);               
            if( m_viewThread.WantsData() ) m_viewThread.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewRegister.WantsData() ) m_viewRegister.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewMemory.WantsData() ) m_viewMemory.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewDisassembly.WantsData() ) m_viewDisassembly.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewVariable.WantsData() ) m_viewVariable.SetInfo(data.szMessage, data.MiInfo);
            if( m_viewBreakpoint.WantsData() ) m_viewBreakpoint.SetInfo(data.szMessage, data.MiInfo);
            if( m_pQuickWatchDlg && m_pQuickWatchDlg->IsWindow() && m_pQuickWatchDlg->IsWindowVisible() ) m_pQuickWatchDlg->SetInfo(data.szMessage, data.MiInfo);

            if( _tcscmp(data.szMessage, _T("value")) == 0 ) {
               // Pass information to editors, since it might be mouse hover information...
               // NOTE: Must use SendMessage() rather than delayed message because
               //       of scope of 'data' structure.
               data.Action = LAZY_SEND_VIEW_MESSAGE;
               data.wParam = DEBUG_CMD_HOVERINFO;
               m_wndMain.SendMessage(WM_COMMAND, MAKEWPARAM(ID_DEBUG_EDIT_LINK, data.wParam), (LPARAM) &data);
               break;
            }
         }
         break;
      }
   }
   m_aLazyData.RemoveAll();
   ::LeaveCriticalSection(&_Module.m_csStaticDataInit);

   // Now that we're not blocking, send debug commands
   for( i = 0; i < aDbgCmd.GetSize(); i++ ) m_DebugManager.DoDebugCommand(aDbgCmd[i]);

   // Display message to user if available
   if( !sMessage.IsEmpty() ) {
      HWND hWnd = ::GetActiveWindow();
      _pDevEnv->ShowMessageBox(hWnd, sMessage, sCaption, iFlags | MB_MODELESS);
   }
   return 0;   
}

void CRemoteProject::DelayedMessage(LPCTSTR pstrMessage, LPCTSTR pstrCaption, UINT iFlags)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_SHOW_MESSAGE;
   _tcscpy(data.szCaption, pstrCaption);
   _tcscpy(data.szMessage, pstrMessage);
   data.iFlags = iFlags;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedOpenView(LPCTSTR pstrFilename, long lLineNum)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_OPEN_VIEW;
   _tcscpy(data.szFilename, pstrFilename);
   data.lLineNum = lLineNum;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedGuiAction(UINT iAction, LPCTSTR pstrFilename, long lLineNum)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_GUI_ACTION;
   data.wParam = iAction;
   _tcscpy(data.szFilename, pstrFilename == NULL ? _T("") : pstrFilename);
   data.lLineNum = lLineNum;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedGuiAction(UINT iAction, HWND hWnd)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_GUI_ACTION;
   data.wParam = iAction;
   data.hWnd = hWnd;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedStatusBar(LPCTSTR pstrText)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_SET_STATUSBARTEXT;
   _tcscpy(data.szMessage, pstrText);
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedDebugCommand(LPCTSTR pstrCommand)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_DEBUGCOMMAND;
   _tcscpy(data.szMessage, pstrCommand);
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
   ATLASSERT(_tcslen(pstrCommand)<(sizeof(data.szMessage)/sizeof(TCHAR))-1);
}

void CRemoteProject::DelayedViewMessage(WPARAM wCmd, LPCTSTR pstrFilename /*= NULL*/, long lLineNum /*= -1*/, UINT iFlags /*= 0*/)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_SEND_VIEW_MESSAGE;
   data.wParam = wCmd;
   _tcscpy(data.szFilename, pstrFilename == NULL ? _T("") : pstrFilename);
   _tcscpy(data.szMessage, _T(""));
   _tcscpy(data.szCaption, _T(""));
   data.lLineNum = lLineNum;
   data.iFlags = iFlags;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedDebugBreakpoint(LPCTSTR pstrFilename, long lLineNum)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = LAZY_SET_DEBUG_BREAKPOINT;
   _tcscpy(data.szFilename, pstrFilename);
   data.lLineNum = lLineNum;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedDebugEvent(LAZYACTION event /*= LAZY_DEBUG_STOP_EVENT*/)
{
   CLockStaticDataInit lock;
   LAZYDATA data;
   data.Action = event;
   m_aLazyData.Add(data);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
}

void CRemoteProject::DelayedDebugInfo(LPCTSTR pstrCommand, CMiInfo& info)
{
   CLockStaticDataInit lock;
   LAZYDATA dummy;
   m_aLazyData.Add(dummy);
   LAZYDATA& data = m_aLazyData[m_aLazyData.GetSize() - 1];
   data.Action = LAZY_DEBUG_INFO;
   _tcscpy(data.szMessage, pstrCommand);
   data.MiInfo.Copy(info);
   m_wndMain.PostMessage(WM_COMMAND, MAKEWPARAM(ID_PROCESS, 0));
   ATLASSERT(_tcslen(pstrCommand)<(sizeof(data.szMessage)/sizeof(TCHAR))-1);
}
