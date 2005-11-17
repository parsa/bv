
#include "StdAfx.h"
#include "resource.h"

#include "Project.h"

#include "ViewSerializer.h"


// Constructor / destructor

CRemoteProject::CRemoteProject() :
   m_Dispatch(this),
   m_pQuickWatchDlg(NULL),
   m_bLoaded(false),
   m_bIsDirty(false)
{
   SecClearPassword();
}

CRemoteProject::~CRemoteProject()
{
   Close();
}

// IProject

BOOL CRemoteProject::Initialize(IDevEnv* pEnv, LPCTSTR pstrPath)
{
   ATLASSERT(pEnv);

   m_bLoaded = true;

   m_wndMain = pEnv->GetHwnd(IDE_HWND_MAIN);
   ATLASSERT(m_wndMain.IsWindow());

   m_sPath = pstrPath;

   _InitializeData();

   _pDevEnv->AddAppListener(this);
   _pDevEnv->AddTreeListener(this);
   _pDevEnv->AddWizardListener(this);

   m_CompileManager.Init(this);
   m_DebugManager.Init(this);
   m_TagManager.Init(this);

   return TRUE;
}

BOOL CRemoteProject::Close()
{
   if( !m_bLoaded ) return TRUE;
   m_bLoaded = false;

   m_FileManager.SignalStop();
   m_DebugManager.SignalStop();
   m_CompileManager.SignalStop();

   _pDevEnv->RemoveAppListener(this);
   _pDevEnv->RemoveTreeListener(this);
   _pDevEnv->RemoveIdleListener(this);
   _pDevEnv->RemoveWizardListener(this);

   m_viewOutput.Close();
   m_viewDebugLog.Close();
   m_viewClassTree.Close();
   m_viewCompileLog.Close();

   Reset();
   m_FileManager.Stop();
   m_DebugManager.Stop();
   m_CompileManager.Stop();

   if( m_pQuickWatchDlg ) delete m_pQuickWatchDlg;
   m_pQuickWatchDlg = NULL;

   return TRUE;
}

BOOL CRemoteProject::GetName(LPTSTR pstrName, UINT cchMax) const
{
   _tcsncpy(pstrName, m_sName, cchMax);
   return TRUE;
}

BOOL CRemoteProject::SetName(LPCTSTR pstrName)
{
   ATLASSERT(!::IsBadStringPtr(pstrName,-1));
   m_sName = pstrName;
   m_bIsDirty = true;
   return TRUE;
}

BOOL CRemoteProject::GetType(LPTSTR pstrType, UINT cchMax) const
{
   _tcsncpy(pstrType, _T("Project"), cchMax);
   return TRUE;
}

BOOL CRemoteProject::GetClass(LPTSTR pstrType, UINT cchMax) const
{
   _tcsncpy(pstrType, _T(PLUGIN_NAME), cchMax);
   return TRUE;
}

BOOL CRemoteProject::IsDirty() const
{
   if( !m_bLoaded ) return FALSE;
   if( m_bIsDirty ) return TRUE;
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      if( m_aFiles[i]->IsDirty() ) return TRUE;
   }
   return FALSE;
}

IDispatch* CRemoteProject::GetDispatch()
{
   CRemoteProject* pThis = const_cast<CRemoteProject*>(this);
   return &pThis->m_Dispatch;
}

INT CRemoteProject::GetItemCount() const
{
   return m_aFiles.GetSize();
}

IView* CRemoteProject::GetItem(INT iIndex)
{
   if( iIndex < 0 || iIndex > m_aFiles.GetSize() ) return NULL;
   return m_aFiles[iIndex];
}

BOOL CRemoteProject::Reset()
{
   m_sName.Empty();
   // Close the views nicely before killing them off
   int i;
   for( i = 0; i < m_aFiles.GetSize(); i++ ) m_aFiles[i]->CloseView();
   for( i = 0; i < m_aDependencies.GetSize(); i++ ) m_aDependencies[i]->CloseView();
   m_aFiles.RemoveAll();
   m_aLazyData.RemoveAll();
   m_aDependencies.RemoveAll();
   m_bIsDirty = false;
   return TRUE;
}

BOOL CRemoteProject::Load(ISerializable* pArc)
{
   Reset();

   pArc->Read(_T("name"), m_sName.GetBufferSetLength(128), 128);
   m_sName.ReleaseBuffer();

   if( !_LoadSettings(pArc) ) return FALSE;
   if( !_LoadFiles(pArc, this) ) return FALSE;

   return TRUE;
}

BOOL CRemoteProject::Save(ISerializable* pArc)
{
   pArc->Write(_T("name"), m_sName);
   pArc->Write(_T("type"), _T(PLUGIN_NAME));

   if( !_SaveSettings(pArc) ) return FALSE;
   if( !_SaveFiles(pArc, this) ) return FALSE;

   m_bIsDirty = false;
   return TRUE;
}

BOOL CRemoteProject::CreateProject()
{
   CTreeViewCtrl ctrlTree = _pDevEnv->GetHwnd(IDE_HWND_EXPLORER_TREE);

   CString sName;
   GetName(sName.GetBufferSetLength(128), 128);
   sName.ReleaseBuffer();

   HTREEITEM hRoot = ctrlTree.GetRootItem();
   HTREEITEM hItem = ctrlTree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, 
         sName, 
         6, 6, 
         0, 0,
         (LPARAM) this,
         hRoot, TVI_LAST);
   _PopulateTree(ctrlTree, this, hItem);
   ctrlTree.Expand(hRoot);
   ctrlTree.Expand(hItem);
   
   return TRUE;
}

void CRemoteProject::ActivateProject()
{
   _pDevEnv->AddIdleListener(this);
   _pDevEnv->AddCommandListener(this);

   if( !m_FileManager.IsConnected() ) m_FileManager.Start();
   if( !m_CompileManager.IsConnected() ) m_CompileManager.Start();

   m_viewCompileLog.Init(&m_CompileManager.m_ShellManager);
   m_viewDebugLog.Init(&m_DebugManager.m_ShellManager);
   m_viewClassTree.Init(this);
   m_viewStack.Init(this);
   m_viewRegister.Init(this);
   m_viewDisassembly.Init(this);
   m_viewMemory.Init(this);
   m_viewBreakpoint.Init(this);
   m_viewVariable.Init(this);
   m_viewThread.Init(this);
   m_viewWatch.Init(this);

   m_viewClassTree.Populate();
   m_viewRemoteDir.Init(this);

   // Project filenames are relative to the solution, so
   // we need to change current Windows path.
   ::SetCurrentDirectory(m_sPath);
}

void CRemoteProject::DeactivateProject()
{
   // Need to remove the classbrowser view because
   // activating a new project should display new items
   m_viewClassTree.Clear();
   m_viewRemoteDir.Detach();

   // Don't keep dependencies around
   for( int i = 0; i < m_aDependencies.GetSize(); i++ ) m_aDependencies[i]->CloseView();
   m_aDependencies.RemoveAll();

   _pDevEnv->RemoveIdleListener(this);
   _pDevEnv->RemoveCommandListener(this);
}

void CRemoteProject::ActivateUI()
{
   CMenu menu;
   menu.LoadMenu(IDR_MAIN);
   ATLASSERT(menu.IsMenu());

   CMenuHandle menuMain = _pDevEnv->GetMenuHandle(IDE_HWND_MAIN);
   CMenuHandle menuFile = menuMain.GetSubMenu(0);
   CMenuHandle menuView = menuMain.GetSubMenu(2);
   MergeMenu(menuFile.GetSubMenu(1), menu.GetSubMenu(5), 2);
   MergeMenu(menuMain, menu.GetSubMenu(0), 3);
   MergeMenu(menuMain, menu.GetSubMenu(1), 4);
   MergeMenu(menuView, menu.GetSubMenu(3), 3);
   MergeMenu(menuView.GetSubMenu(4), menu.GetSubMenu(4), 2);

   CMenuHandle menuViews = menuView.GetSubMenu(2);
   menuViews.AppendMenu(MF_STRING, ID_VIEW_FILEMANAGER, CString(MAKEINTRESOURCE(IDS_FILEMANAGER)));

   // Propagate dirty flag to project itself.
   // This will allow us to memorize if the project was ever changed
   // and is especially useful when we need to determine if a compile
   // is needed before the process can be run.
   m_bIsDirty |= (IsDirty() == TRUE);
}

void CRemoteProject::DeactivateUI()
{
}

// IAppMessageListener

LRESULT CRemoteProject::OnAppMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
   LRESULT lResult = 0;
   bHandled = ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult);
   return lResult;
}

BOOL CRemoteProject::PreTranslateMessage(MSG* pMsg)
{
   if( _pDevEnv->GetSolution()->GetFocusProject() != this ) return FALSE;
   if( !m_accel.IsNull() && m_accel.TranslateAccelerator(m_wndMain, pMsg) ) return TRUE;
   return FALSE;
}

// IIdleListener

void CRemoteProject::OnIdle(IUpdateUI* pUIBase)
{
   CString sFileType;
   IElement* pElement = _GetSelectedTreeElement();
   if( pElement ) {
      pElement->GetType(sFileType.GetBufferSetLength(64), 64);
      sFileType.ReleaseBuffer();
   }

   int iMode = m_ctrlMode.GetCurSel();
   m_CompileManager.SetParam(_T("BuildMode"), iMode == 0 ? _T("release") : _T("debug"));

   BOOL bCompiling = m_CompileManager.IsCompiling();
   BOOL bProcessStarted = m_DebugManager.IsBusy();
   BOOL bDebugging = m_DebugManager.IsDebugging();
   BOOL bBusy = bCompiling || bProcessStarted;
   BOOL bDebugBreak = m_DebugManager.IsBreaked();
   BOOL bIsFolder = sFileType == _T("Project") || sFileType == _T("Folder");

   pUIBase->UIEnable(ID_VIEW_FILEMANAGER, TRUE);
   pUIBase->UISetCheck(ID_VIEW_FILEMANAGER, m_viewRemoteDir.IsWindow());

   pUIBase->UIEnable(ID_PROJECT_SET_DEFAULT, !bBusy);
   pUIBase->UIEnable(ID_FILE_OPENSOLUTION, !bBusy);
   pUIBase->UIEnable(ID_FILE_CLOSESOLUTION, !bBusy);
   pUIBase->UIEnable(ID_NEW_SOLUTION, !bBusy);
   pUIBase->UIEnable(ID_NEW_PROJECT, !bBusy);
   pUIBase->UIEnable(ID_ADD_PROJECT, !bBusy);

   pUIBase->UIEnable(ID_FILE_ADD_FOLDER, bIsFolder && !bBusy);
   pUIBase->UIEnable(ID_FILE_ADD_LOCAL, bIsFolder && !bBusy);
   pUIBase->UIEnable(ID_FILE_ADD_REMOTE, bIsFolder && !bBusy);
   pUIBase->UIEnable(ID_FILE_RENAME, pElement != NULL && !bBusy);
   pUIBase->UIEnable(ID_FILE_DELETE, !bBusy);

   pUIBase->UIEnable(ID_EDIT_BREAK, bCompiling | bDebugging);

   pUIBase->UIEnable(ID_VIEW_OPEN, pElement != NULL && !bIsFolder);
   pUIBase->UIEnable(ID_VIEW_PROPERTIES, pElement != NULL);

   pUIBase->UIEnable(ID_BUILD_COMPILE, sFileType == "CPP" && !bCompiling);
   pUIBase->UIEnable(ID_BUILD_CHECKSYNTAX, sFileType == "CPP" && !bCompiling);
   pUIBase->UIEnable(ID_BUILD_PROJECT, !bCompiling);
   pUIBase->UIEnable(ID_BUILD_REBUILD, !bCompiling);
   pUIBase->UIEnable(ID_BUILD_SOLUTION, !bCompiling);   
   pUIBase->UIEnable(ID_BUILD_CLEAN, !bCompiling);
   pUIBase->UIEnable(ID_BUILD_BUILDTAGS, !bCompiling);
   pUIBase->UIEnable(ID_BUILD_BUILDMAKEFILE, !bBusy);
   pUIBase->UIEnable(ID_BUILD_FILEWIZARD, pElement != NULL && !bBusy && !bIsFolder);   
   pUIBase->UIEnable(ID_BUILD_STOP, bCompiling);

   pUIBase->UIEnable(ID_DEBUG_START, (!bCompiling && !bProcessStarted) || bDebugBreak);
   pUIBase->UIEnable(ID_DEBUG_DEBUG, !bCompiling && !bProcessStarted);
   pUIBase->UIEnable(ID_DEBUG_STEP_INTO, bDebugging);
   pUIBase->UIEnable(ID_DEBUG_STEP_OVER, bDebugging);
   pUIBase->UIEnable(ID_DEBUG_STEP_OUT, bDebugging);
   pUIBase->UIEnable(ID_DEBUG_STEP_RUN, FALSE);
   pUIBase->UIEnable(ID_DEBUG_BREAK, bDebugging && !bDebugBreak);
   pUIBase->UIEnable(ID_DEBUG_STOP, bProcessStarted);
   pUIBase->UIEnable(ID_DEBUG_BREAKPOINT, FALSE);
   pUIBase->UIEnable(ID_DEBUG_CLEAR_BREAKPOINTS, TRUE);
   pUIBase->UIEnable(ID_DEBUG_QUICKWATCH, FALSE);
   pUIBase->UIEnable(ID_DEBUG_PROCESSES, !bBusy);
   pUIBase->UIEnable(ID_DEBUG_ARGUMENTS, !bDebugging);

   pUIBase->UIEnable(ID_VIEW_COMPILE_LOG, TRUE);
   pUIBase->UIEnable(ID_VIEW_DEBUG_LOG, TRUE);
   pUIBase->UIEnable(ID_VIEW_REGISTERS, bDebugging);
   pUIBase->UIEnable(ID_VIEW_MEMORY, bDebugging);
   pUIBase->UIEnable(ID_VIEW_DISASM, bDebugging);
   pUIBase->UIEnable(ID_VIEW_THREADS, bDebugging);
   pUIBase->UIEnable(ID_VIEW_VARIABLES, bDebugging);
   pUIBase->UIEnable(ID_VIEW_WATCH, bDebugging);
   pUIBase->UIEnable(ID_VIEW_CALLSTACK, bDebugging);
   pUIBase->UIEnable(ID_VIEW_BREAKPOINTS, bDebugging);
   pUIBase->UIEnable(ID_VIEW_DEBUGOUTPUT, bDebugging);

   pUIBase->UISetCheck(ID_VIEW_COMPILE_LOG, m_viewCompileLog.IsWindow() && m_viewCompileLog.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_DEBUG_LOG, m_viewDebugLog.IsWindow() && m_viewDebugLog.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_REGISTERS, m_viewRegister.IsWindow() && m_viewRegister.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_MEMORY, m_viewMemory.IsWindow() && m_viewMemory.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_DISASM, m_viewDisassembly.IsWindow() && m_viewDisassembly.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_THREADS, m_viewThread.IsWindow() && m_viewThread.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_VARIABLES, m_viewVariable.IsWindow() && m_viewVariable.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_WATCH, m_viewWatch.IsWindow() && m_viewWatch.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_CALLSTACK, m_viewStack.IsWindow() && m_viewStack.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_BREAKPOINTS, m_viewBreakpoint.IsWindow() && m_viewBreakpoint.IsWindowVisible());
   pUIBase->UISetCheck(ID_VIEW_DEBUGOUTPUT, m_viewOutput.IsWindow() && m_viewOutput.IsWindowVisible());
}

void CRemoteProject::OnGetMenuText(UINT wID, LPTSTR pstrText, int cchMax)
{
   AtlLoadString(wID, pstrText, cchMax);
}

// ITreeMessageListener

LRESULT CRemoteProject::OnTreeMessage(LPNMHDR pnmh, BOOL& bHandled)
{
   if( _GetSelectedTreeElement() == NULL ) return 0;
   LRESULT lResult = 0;
   bHandled = ProcessWindowMessage(pnmh->hwndFrom, WM_NOTIFY, (WPARAM) pnmh->idFrom, (LPARAM) pnmh, lResult);
   return lResult;
}

// IWizardListener

BOOL CRemoteProject::OnInitWizard(IWizardManager* pManager, IProject* pProject, LPCTSTR pstrName)
{
   if( pProject != this ) return TRUE;

   static CFileTransferPage s_pageTransfer;
   static CCompilerPage s_pageCompiler;
   static CDebuggerPage s_pageDebugger;

   static CString sTransferTitle(MAKEINTRESOURCE(IDS_WIZARD_TITLE_TRANSFER));
   static CString sTransferSubTitle(MAKEINTRESOURCE(IDS_WIZARD_SUBTITLE_TRANSFER));
   static CString sStartTitle(MAKEINTRESOURCE(IDS_WIZARD_TITLE_STARTAPP));
   static CString sStartSubTitle(MAKEINTRESOURCE(IDS_WIZARD_SUBTITLE_STARTAPP));
   static CString sDebuggerTitle(MAKEINTRESOURCE(IDS_WIZARD_TITLE_DEBUGGER));
   static CString sDebuggerSubTitle(MAKEINTRESOURCE(IDS_WIZARD_SUBTITLE_DEBUGGER));
   
   s_pageTransfer.m_pProject = this;
   s_pageTransfer.SetHeaderTitle(sTransferTitle);
   s_pageTransfer.SetHeaderSubTitle(sTransferSubTitle);
   s_pageCompiler.m_pProject = this;
   s_pageCompiler.SetHeaderTitle(sStartTitle);
   s_pageCompiler.SetHeaderSubTitle(sStartSubTitle);
   s_pageDebugger.m_pProject = this;
   s_pageDebugger.SetHeaderTitle(sDebuggerTitle);
   s_pageDebugger.SetHeaderSubTitle(sDebuggerSubTitle);

   pManager->AddWizardPage(s_pageTransfer.IDD, s_pageTransfer);
   pManager->AddWizardPage(s_pageCompiler.IDD, s_pageCompiler);
   pManager->AddWizardPage(s_pageDebugger.IDD, s_pageDebugger);

   SetName(pstrName);
   return TRUE;
}

BOOL CRemoteProject::OnInitProperties(IWizardManager* pManager, IElement* pElement)
{
   if( pElement != this ) return TRUE;

   static CFileTransferPage s_pageTransfer;
   static CFileOptionsPage s_pageTransferOptions;
   static CCompilerPage s_pageCompiler;
   static CCompilerCommandsPage s_pageCompilerCommands;
   static CCompilerStepsPage s_pageCompilerSteps;
   static CDebuggerPage s_pageDebugger;
   static CDebuggerCommandsPage s_pageDebuggerCommands;

   static CString sTransferTitle(MAKEINTRESOURCE(IDS_CONFIG_SETTINGS));
   static CString sTransferOptionsTitle(MAKEINTRESOURCE(IDS_CONFIG_OPTIONS));
   static CString sStartTitle(MAKEINTRESOURCE(IDS_CONFIG_SETTINGS));
   static CString sCompilerCommandsTitle(MAKEINTRESOURCE(IDS_CONFIG_COMMANDS));
   static CString sCompilerStepsTitle(MAKEINTRESOURCE(IDS_CONFIG_STEPS));
   static CString sDebuggerTitle(MAKEINTRESOURCE(IDS_CONFIG_SETTINGS));
   static CString sDebuggerCommandsTitle(MAKEINTRESOURCE(IDS_CONFIG_COMMANDS));

   s_pageTransfer.m_pProject = this;
   s_pageTransfer.SetTitle((LPCTSTR)sTransferTitle);
   s_pageTransferOptions.m_pProject = this;
   s_pageTransferOptions.SetTitle((LPCTSTR)sTransferOptionsTitle);
   s_pageCompiler.m_pProject = this;
   s_pageCompiler.SetTitle((LPCTSTR)sStartTitle);
   s_pageCompilerCommands.m_pProject = this;
   s_pageCompilerCommands.SetTitle((LPCTSTR)sCompilerCommandsTitle);
   s_pageCompilerSteps.m_pProject = this;
   s_pageCompilerSteps.SetTitle((LPCTSTR)sCompilerStepsTitle);
   s_pageDebugger.m_pProject = this;
   s_pageDebugger.SetTitle((LPCTSTR)sDebuggerTitle);
   s_pageDebuggerCommands.m_pProject = this;
   s_pageDebuggerCommands.SetTitle((LPCTSTR)sDebuggerCommandsTitle);

   CString sConfiguration(MAKEINTRESOURCE(IDS_CONFIG_CONFIGURATION));
   pManager->AddWizardGroup(sConfiguration, CString(MAKEINTRESOURCE(IDS_CONFIG_TRANSFER)));
   pManager->AddWizardPage(s_pageTransfer.IDD, s_pageTransfer);
   pManager->AddWizardPage(s_pageTransferOptions.IDD, s_pageTransferOptions);
   //
   pManager->AddWizardGroup(sConfiguration, CString(MAKEINTRESOURCE(IDS_CONFIG_COMPILER)));
   pManager->AddWizardPage(s_pageCompiler.IDD, s_pageCompiler);
   pManager->AddWizardPage(s_pageCompilerCommands.IDD, s_pageCompilerCommands);
   pManager->AddWizardPage(s_pageCompilerSteps.IDD, s_pageCompilerSteps);
   //
   pManager->AddWizardGroup(sConfiguration, CString(MAKEINTRESOURCE(IDS_CONFIG_DEBUGGER)));
   pManager->AddWizardPage(s_pageDebugger.IDD, s_pageDebugger);
   pManager->AddWizardPage(s_pageDebuggerCommands.IDD, s_pageDebuggerCommands);

   return TRUE;
}

BOOL CRemoteProject::OnInitOptions(IWizardManager* pManager, ISerializable* pArc)
{
   return TRUE;
}

// ICustomCommandListener

void CRemoteProject::OnUserCommand(LPCTSTR pstrCommand, BOOL& bHandled)
{
   bHandled = FALSE;
   CRichEditCtrl ctrlEdit = _pDevEnv->GetHwnd(IDE_HWND_COMMANDVIEW);
   if( _tcsncmp(pstrCommand, _T("cc "), 3) == 0 ) 
   {
      if( !m_CompileManager.IsConnected() ) {
         // Compiler session not connected?
         AppendRtfText(ctrlEdit, CString(MAKEINTRESOURCE(IDS_ERR_COMPILER_NOT_RUNNING)), CFM_COLOR, 0, RGB(100,0,0));
      }
      else if( m_CompileManager.IsBusy() ) {
         // Already compiling another project?
         AppendRtfText(ctrlEdit, CString(MAKEINTRESOURCE(IDS_ERR_COMPILERBUSY)), CFM_COLOR, 0, RGB(100,0,0));
      }
      else {
         // Execute command and wait for completion
         m_CompileManager.DoAction(pstrCommand);
         DWORD dwStartTick = ::GetTickCount();
         while( m_CompileManager.GetParam(_T("InCommand")) == _T("true") ) {
            ::Sleep(100L);
            // HACK: Don't wait more than 5 secs!
            //       This timeout value may have to be adjusted in the future.
            if( ::GetTickCount() - dwStartTick > 5UL * 1000UL ) break;
         }
      }
      bHandled = TRUE;
   }
   if( _tcsncmp(pstrCommand, _T("gdb "), 4) == 0 ) 
   {
      if( !m_DebugManager.IsDebugging() ) {
         // Debug session not connected?
         AppendRtfText(ctrlEdit, CString(MAKEINTRESOURCE(IDS_ERR_DEBUGGER_NOT_RUNNING)), CFM_COLOR, 0, RGB(100,0,0));
      }
      else {
         // Execute command in debugger and wait for completion
         m_DebugManager.SetParam(_T("InCommand"), _T("true"));
         CString sCommand;
         sCommand.Format(_T("-interpreter-exec console \"%s\""), pstrCommand + 4);
         m_DebugManager.DoDebugCommand(sCommand);
         m_DebugManager.DoDebugCommand(_T(""));
         DWORD dwStartTime = ::GetTickCount();
         while( m_DebugManager.GetParam(_T("InCommand")) == _T("true") ) {
            ::Sleep(100L);
            // NOTE: We'll only wait 5 secs for completion. This is a
            //       bit of a hack, but we have very little control over
            //       what the debugger might be spitting out. At least stopping
            //       the tight loop will allow the user to abort the session!
            if( ::GetTickCount() - dwStartTime > 5000UL ) break;
         }
      }
      bHandled = TRUE;
   }
   if( _tcsicmp(pstrCommand, _T("help")) == 0 ) 
   {
      AppendRtfText(ctrlEdit, CString(MAKEINTRESOURCE(IDS_HELP)));
   }
}

void CRemoteProject::OnMenuCommand(LPCTSTR pstrType, LPCTSTR pstrCommand, LPCTSTR pstrArguments, LPCTSTR pstrPath, int iFlags, BOOL& bHandled)
{
   if( _pDevEnv->GetSolution()->GetActiveProject() != this ) return;
   if( _tcscmp(pstrType, _T("compiler")) == 0 ) {
      CRichEditCtrl ctrlEdit = _pDevEnv->GetHwnd(IDE_HWND_COMMANDVIEW);
      _pDevEnv->ActivateAutoHideView(ctrlEdit);
      CString sCommandline;
      sCommandline.Format(_T("%s %s %s"), 
         (iFlags & TOOLFLAGS_CONSOLEOUTPUT) != 0 ? _T("cc") : _T("cz"), 
         pstrCommand, 
         pstrArguments);
      m_CompileManager.DoAction(sCommandline);
      bHandled = TRUE;
   }
   if( _tcscmp(pstrType, _T("debugger")) == 0 ) {
      CString sCommand;
      sCommand.Format(_T("gdb %s %s"), pstrCommand, pstrArguments);
      OnUserCommand(sCommand, bHandled);
   }
}

// Operations

BOOL CRemoteProject::GetPath(LPTSTR pstrPath, UINT cchMax) const
{
   _tcsncpy(pstrPath, m_sPath, cchMax);
   return TRUE;
}

CTelnetView* CRemoteProject::GetDebugView() const
{
   return &m_viewDebugLog;
}

CTelnetView* CRemoteProject::GetOutputView() const
{
   return &m_viewOutput;
}

CClassView* CRemoteProject::GetClassView() const
{
   return &m_viewClassTree;
}

void CRemoteProject::SendViewMessage(UINT nCmd, LAZYDATA* pData)
{
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      DWORD dwRes = 0;
      m_aFiles[i]->SendMessage(WM_COMMAND, MAKEWPARAM(ID_DEBUG_EDIT_LINK, nCmd), (LPARAM) pData);
   }
}

bool CRemoteProject::OpenView(LPCTSTR pstrFilename, long lLineNum)
{
   IView* pView = FindView(pstrFilename, false);
   if( pView == NULL ) pView = _CreateDependencyFile(pstrFilename, ::PathFindFileName(pstrFilename));
   if( pView == NULL ) return false;
   return pView->OpenView(lLineNum) == TRUE;
}

IView* CRemoteProject::FindView(LPCTSTR pstrFilename, bool bLocally /*= false*/) const
{
   ATLASSERT(!::IsBadStringPtr(pstrFilename,-1));
   // Prepare filename (strip path)
   TCHAR szSearchFile[MAX_PATH + 1] = { 0 };
   _tcscpy(szSearchFile, pstrFilename);
   ::PathStripPath(szSearchFile);
   // No need to look for dummy filenames
   if( _tcslen(pstrFilename) < 2 ) return false;
   // Scan through all views and see if there's a filename-match
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      IView* pView = m_aFiles[i];
      TCHAR szFilename[MAX_PATH + 1] = { 0 };
      pView->GetFileName(szFilename, MAX_PATH);
      ::PathStripPath(szFilename);
      if( _tcsicmp(szSearchFile, szFilename) == 0 ) return pView;
   }
   // Scan for project files only?
   if( bLocally ) return NULL;
   // Locate file in another project
   ISolution* pSolution = _pDevEnv->GetSolution();
   for( int a = 0; a < pSolution->GetItemCount(); a++ ) {
      IProject* pProject = pSolution->GetItem(a);
      for( INT b = 0; b < pProject->GetItemCount(); b++ ) {
         IView* pView = pProject->GetItem(b);
         TCHAR szFilename[MAX_PATH + 1] = { 0 };
         pView->GetFileName(szFilename, MAX_PATH);
         ::PathStripPath(szFilename);
         if( _tcsicmp(szSearchFile, szFilename) == 0 ) return pView;
      }
   }
   // Look for the file in the dependencies
   for( int x = 0; x < m_aDependencies.GetSize(); x++ ) {
      IView* pView = m_aDependencies[x];
      TCHAR szFilename[MAX_PATH + 1] = { 0 };
      pView->GetFileName(szFilename, MAX_PATH);
      ::PathStripPath(szFilename);
      if( _tcsicmp(szSearchFile, szFilename) == 0 ) return pView;
   }
   return NULL;
}

bool CRemoteProject::GetTagInfo(LPCTSTR pstrValue, bool bAskDebugger, CSimpleArray<CString>& aResult, LPCTSTR pstrOwner /*= NULL*/)
{
   ATLASSERT(!::IsBadStringPtr(pstrValue,-1));
   // NOTE: GetTagInfo() is designed so that it *may* return a result
   //       immediately - but it may also delay the retrieval of information
   //       (which happens when it queries debug-information from the debugger).
   if( bAskDebugger && m_DebugManager.IsDebugging() ) return m_DebugManager.GetTagInfo(pstrValue);
   return m_TagManager.GetItemDeclaration(pstrValue, aResult, pstrOwner);
}

void CRemoteProject::InitializeToolBars()
{
   // NOTE: The toolbars are static members of this
   //       class so we need to initialize them only once.
   if( m_ctrlBuild.IsWindow() ) return;

   CWindow wndMain = _pDevEnv->GetHwnd(IDE_HWND_MAIN);

   m_ctrlDebug = CFrameWindowImplBase<>::CreateSimpleToolBarCtrl(wndMain, IDR_DEBUG, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE);
   m_ctrlBuild = CFrameWindowImplBase<>::CreateSimpleToolBarCtrl(wndMain, IDR_BUILD, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST);
   m_ctrlBookmarks = CFrameWindowImplBase<>::CreateSimpleToolBarCtrl(wndMain, IDR_BOOKMARKS, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST);
   m_ctrlSearch = CFrameWindowImplBase<>::CreateSimpleToolBarCtrl(wndMain, IDR_SEARCH, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST);

   m_ctrlMode.Create(m_ctrlBuild, CWindow::rcDefault, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS);
   m_ctrlMode.SetFont(AtlGetDefaultGuiFont());
   m_ctrlMode.AddString(CString(MAKEINTRESOURCE(IDS_RELEASE)));
   m_ctrlMode.AddString(CString(MAKEINTRESOURCE(IDS_DEBUG)));
   m_ctrlMode.SetCurSel(1);

   m_ctrlBuild.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
   _AddButtonText(m_ctrlBuild, ID_BUILD_PROJECT, IDS_BUILD);
   _AddControlToToolbar(m_ctrlBuild, m_ctrlMode, 90, 4, false);
   m_ctrlMode.SetWindowPos(NULL, 0, 0, 90, 120, SWP_NOMOVE | SWP_NOREDRAW);

   m_ctrlBookmarks.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
   _AddDropDownButton(m_ctrlBookmarks, ID_BOOKMARKS_TOGGLE);
   _AddDropDownButton(m_ctrlBookmarks, ID_BOOKMARKS_GOTO);
   _AddButtonText(m_ctrlBookmarks, ID_BOOKMARKS_TOGGLE, IDS_TOGGLE);
   _AddButtonText(m_ctrlBookmarks, ID_BOOKMARKS_GOTO, IDS_GOTO);

   _pDevEnv->AddToolBar(m_ctrlBuild, _T("CppBuild"), CString(MAKEINTRESOURCE(IDS_CAPTION_BUILD)));
   _pDevEnv->AddToolBar(m_ctrlDebug, _T("CppDebug"), CString(MAKEINTRESOURCE(IDS_CAPTION_DEBUG)));
   _pDevEnv->AddToolBar(m_ctrlBookmarks, _T("Bookmarks"), CString(MAKEINTRESOURCE(IDS_CAPTION_BOOKMARKS)));
   //_pDevEnv->AddToolBar(m_ctrlSearch, _T("Search"), CString(MAKEINTRESOURCE(IDS_CAPTION_SEARCH)));
}

// Implementation

void CRemoteProject::_InitializeData()
{
   // Initilize data. These control are mostly statics and
   // need only be initialized one.
   if( !m_accel.IsNull() ) return;

   CString sCaption;
   DWORD dwStyle;

   m_accel.LoadAccelerators(IDR_ACCELERATOR);

   // Create the Compile Log view
   sCaption.LoadString(IDS_CAPTION_COMPILELOG);
   dwStyle = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
   m_viewCompileLog.Init(&m_CompileManager.m_ShellManager);
   m_viewCompileLog.Create(m_wndMain, CWindow::rcDefault, sCaption, dwStyle, WS_EX_CLIENTEDGE);
   _pDevEnv->AddDockView(m_viewCompileLog, IDE_DOCK_HIDE, CWindow::rcDefault);

   // Create the Debug Log view
   COLORREF clrBack = BlendRGB(::GetSysColor(COLOR_WINDOW), RGB(0,0,0), 5);
   sCaption.LoadString(IDS_CAPTION_DEBUGLOG);
   dwStyle = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
   m_viewDebugLog.Init(&m_DebugManager.m_ShellManager);
   m_viewDebugLog.Create(m_wndMain, CWindow::rcDefault, sCaption, dwStyle, WS_EX_CLIENTEDGE);
   m_viewDebugLog.SetColors(::GetSysColor(COLOR_WINDOWTEXT), clrBack);
   _pDevEnv->AddDockView(m_viewDebugLog, IDE_DOCK_HIDE, CWindow::rcDefault);

   // Create the Class view
   dwStyle = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_BORDER;
   m_viewClassTree.Init(this);
   m_viewClassTree.Create(m_wndMain, CWindow::rcDefault, NULL, dwStyle);

   _AddCommandBarImages(IDR_TOOLIMAGES);

   // We'll always display the C++ toolbars
   _pDevEnv->ShowToolBar(m_ctrlBuild, TRUE, TRUE);
   _pDevEnv->ShowToolBar(m_ctrlDebug, TRUE, TRUE);
}

bool CRemoteProject::_LoadSettings(ISerializable* pArc)
{
   if( !pArc->ReadGroupBegin(_T("Settings")) ) return true;
   if( !m_FileManager.Load(pArc) ) return false;
   if( !m_CompileManager.Load(pArc) ) return false;
   if( !m_DebugManager.Load(pArc) ) return false;
   if( !pArc->ReadGroupEnd() ) return false;
   return true;
}

bool CRemoteProject::_SaveSettings(ISerializable* pArc)
{
   if( !pArc->WriteGroupBegin(_T("Settings")) ) return false;
   if( !m_FileManager.Save(pArc) ) return false;
   if( !m_CompileManager.Save(pArc) ) return false;
   if( !m_DebugManager.Save(pArc) ) return false;
   if( !pArc->WriteGroupEnd() ) return false;
   return true;
}

bool CRemoteProject::_LoadFiles(ISerializable* pArc, IElement* pParent)
{
   if( !pArc->ReadGroupBegin(_T("Files")) ) return true;
   while( pArc->ReadGroupBegin(_T("File")) ) {
      CString sType;
      pArc->Read(_T("type"), sType.GetBufferSetLength(64), 64);
      sType.ReleaseBuffer();
      CString sLocation;
      pArc->Read(_T("location"), sLocation.GetBufferSetLength(64), 64);
      sLocation.ReleaseBuffer();
      CString sFilename;
      pArc->Read(_T("filename"), sFilename.GetBufferSetLength(MAX_PATH), MAX_PATH);
      sFilename.ReleaseBuffer();
      // Create file object
      // NOTE: We prefer to create files marked as "remote" internally since the
      //       other plugins will not be able to load them from the remote server.
      IView* pView = NULL;
      if( sType == _T("Folder") ) pView = new CFolderFile(this, this, pParent);
      else if( sLocation == _T("remote") ) pView = Plugin_CreateView(sFilename, this, pParent);
      else pView = _pDevEnv->CreateView(sFilename, this, pParent);
      ATLASSERT(pView);
      if( pView == NULL ) return false;
      // Load file properties
      if( !pView->Load(pArc) ) {
         pView->CloseView();
         return false;
      }
      // Add file to collection
      m_aFiles.Add(pView);
      // Recurse into subfiles
      if( !_LoadFiles(pArc, pView) ) {
         m_aFiles.Remove(pView);
         pView->CloseView();
         return false;
      }
      if( !pArc->ReadGroupEnd() ) return false;
   }
   if( !pArc->ReadGroupEnd() ) return false;
   return true;
}

bool CRemoteProject::_SaveFiles(ISerializable* pArc, IElement* pParent)
{
   if( !pArc->WriteGroupBegin(_T("Files")) ) return false;
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      IView* pFile = m_aFiles[i];
      if( pFile->GetParent() == pParent ) {
         if( !pArc->WriteGroupBegin(_T("File")) ) return false;
         // Save file properties
         IElement* pElement = pFile;
         if( !pElement->Save(pArc) ) return false;
         // Recurse into sub-folders
         TCHAR szType[64] = { 0 };
         pElement->GetType(szType, 63);
         if( _tcscmp(szType, _T("Folder")) == 0 ) {
            if( !_SaveFiles(pArc, pFile) ) return false;
         }
         if( !pArc->WriteGroupEnd() ) return false;
      }
   }
   if( !pArc->WriteGroupEnd() ) return false;
   return true;
}

void CRemoteProject::_PopulateTree(CTreeViewCtrl& ctrlTree, 
                                   IElement* pParent, 
                                   HTREEITEM hParent) const
{
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      IView* pFile = m_aFiles[i];
      if( pFile->GetParent() == pParent ) {
         int iImage = _GetElementImage(pFile);
         CString sName;
         pFile->GetName(sName.GetBufferSetLength(128), 128);
         sName.ReleaseBuffer();
         HTREEITEM hNew = ctrlTree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, 
               sName, 
               iImage, iImage, 
               0, 0,
               (LPARAM) pFile,
               hParent, TVI_LAST);
         _PopulateTree(ctrlTree, pFile, hNew);
      }
   }
   TVSORTCB tvscb = { 0 };
   tvscb.hParent = hParent;
   tvscb.lParam = (LPARAM) this;
   tvscb.lpfnCompare = _SortTreeCB;
   ctrlTree.SortChildrenCB(&tvscb);
}

int CRemoteProject::_GetElementImage(IElement* pElement) const
{
   ATLASSERT(pElement);
   TCHAR szType[64] = { 0 };
   pElement->GetType(szType, 63);
   if( _tcscmp(szType, _T("Folder")) == 0 ) return IMAGE_FOLDER;
   if( _tcscmp(szType, _T("CPP")) == 0 ) return IMAGE_CPP;
   if( _tcscmp(szType, _T("XML")) == 0 ) return IMAGE_XML;
   if( _tcscmp(szType, _T("Java")) == 0 ) return IMAGE_JAVA;
   if( _tcscmp(szType, _T("HTML")) == 0 ) return IMAGE_HTML;
   if( _tcscmp(szType, _T("BASIC")) == 0 ) return IMAGE_BASIC;
   if( _tcscmp(szType, _T("Header")) == 0 ) return IMAGE_HEADER;
   if( _tcscmp(szType, _T("Makefile")) == 0 ) return IMAGE_MAKEFILE;
   return IMAGE_TEXT;
}

bool CRemoteProject::_ShouldProcessMessage() const
{
   // If there's a right-click highlight on the tree, we'll
   // accept the tree-item's owner, otherwise we'll accept
   // the active project only as owner.
   IProject* pThis = (IProject*) this;
   CTreeViewCtrl ctrlTree = _pDevEnv->GetHwnd(IDE_HWND_EXPLORER_TREE);
   HTREEITEM hItem = ctrlTree.GetDropHilightItem();
   if( hItem == NULL ) return _pDevEnv->GetSolution()->GetActiveProject() == pThis;
   while( true ) {
      IProject* lParam = (IProject*) ctrlTree.GetItemData(hItem);
      if( lParam == NULL ) return false;
      if( lParam == pThis ) return true;
      hItem = ctrlTree.GetParentItem(hItem);
      if( hItem == NULL ) return false;
   }
}

IElement* CRemoteProject::_GetSelectedTreeElement(HTREEITEM* phItem /*= NULL*/) const
{
   // Get the active/selected tree-item
   if( phItem ) *phItem = NULL;
   CTreeViewCtrl ctrlTree = _pDevEnv->GetHwnd(IDE_HWND_EXPLORER_TREE);
   HTREEITEM hItem = ctrlTree.GetDropHilightItem();
   if( hItem == NULL ) hItem = ctrlTree.GetSelectedItem();
   if( hItem == NULL ) return NULL;
   if( phItem ) *phItem = hItem;
   // Let's check that this tree-item actually belongs to this project
   LPARAM lParam = ctrlTree.GetItemData(hItem);
   if( lParam == NULL ) return NULL;
   bool bLocal = false;
   if( lParam == (LPARAM) this ) bLocal = true;
   for( int i = 0; !bLocal && i < m_aFiles.GetSize(); i++ ) if( (LPARAM) m_aFiles[i] == lParam ) bLocal = true;
   return bLocal ? (IElement*) lParam : NULL;
}

IElement* CRemoteProject::_GetDropTreeElement(HTREEITEM* phItem /*= NULL*/) const
{
   if( phItem ) *phItem = NULL;
   CTreeViewCtrl ctrlTree = _pDevEnv->GetHwnd(IDE_HWND_EXPLORER_TREE);
   HTREEITEM hItem = ctrlTree.GetDropHilightItem();
   if( hItem == NULL ) return NULL;
   if( phItem ) *phItem = hItem;
   LPARAM lParam = ctrlTree.GetItemData(hItem);
   if( lParam == NULL ) return NULL;
   bool bLocal = false;
   if( lParam == (LPARAM) this ) bLocal = true;
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) if( (LPARAM) m_aFiles[i] == lParam ) bLocal = true;
   if( !bLocal ) return NULL;
   return (IElement*) lParam;
}

UINT CRemoteProject::_GetMenuPosFromID(HMENU hMenu, UINT ID) const
{
   UINT nCount = ::GetMenuItemCount(hMenu);
   for( UINT i = 0; i < nCount; i++ ) {
      if( ::GetMenuItemID(hMenu, i) == ID ) return i;
   }
   return (UINT) -1;
}

int CALLBACK CRemoteProject::_SortTreeCB(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
   IView* pItem1 = (IView*) lParam1;
   IView* pItem2 = (IView*) lParam2;
   TCHAR szName1[128];
   TCHAR szName2[128];
   TCHAR szType1[64];
   TCHAR szType2[64];
   pItem1->GetName(szName1, 127);
   pItem2->GetName(szName2, 127);
   pItem1->GetType(szType1, 63);
   pItem2->GetType(szType2, 63);
   bool bIsFolder1 = _tcscmp(szType1, _T("Folder")) == 0;
   bool bIsFolder2 = _tcscmp(szType2, _T("Folder")) == 0;
   if( bIsFolder1 && !bIsFolder2 ) return -1;
   if( !bIsFolder1 && bIsFolder2 ) return 1;
   if( bIsFolder1 && bIsFolder2 ) return 0; // Folders are not sorted alphabetically!
   return _tcsicmp(szName1, szName2);
}

void CRemoteProject::_AddButtonText(CToolBarCtrl tb, UINT nID, UINT nRes)
{
#ifndef BTNS_SHOWTEXT
   const UINT BTNS_SHOWTEXT = 0x0040;
#endif  // BTNS_SHOWTEXT
   TBBUTTONINFO tbi = { 0 };
   tbi.cbSize = sizeof(TBBUTTONINFO),
   tbi.dwMask  = TBIF_STYLE;
   tb.GetButtonInfo(nID, &tbi);
   tbi.dwMask = TBIF_STYLE | TBIF_TEXT;
   tbi.fsStyle |= TBSTYLE_AUTOSIZE | BTNS_SHOWTEXT;
   CString s(MAKEINTRESOURCE(nRes));
   tbi.pszText = (LPTSTR) (LPCTSTR) s;
   tb.SetButtonInfo(nID, &tbi);
}

void CRemoteProject::_AddDropDownButton(CToolBarCtrl tb, UINT nID)
{
   tb.SetExtendedStyle(TBSTYLE_EX_DRAWDDARROWS); 

   TBBUTTONINFO tbi = { 0 };
   tbi.cbSize  = sizeof(TBBUTTONINFO);
   tbi.dwMask  = TBIF_STYLE;
   tb.GetButtonInfo(nID, &tbi);
   tbi.fsStyle |= BTNS_WHOLEDROPDOWN;
   tb.SetButtonInfo(nID, &tbi);
}

void CRemoteProject::_AddControlToToolbar(CToolBarCtrl tb, HWND hWnd, USHORT cx, UINT nCmdPos, bool bIsCommandId, bool bInsertBefore /*= true*/)
{
   ATLASSERT(::IsWindow(hWnd));
   static UINT s_nCmd = 45000; // Unique number for new toolbar separator
   int iIndex = nCmdPos;
   if( bIsCommandId ) iIndex = tb.CommandToIndex(nCmdPos);
   if( !bInsertBefore ) iIndex++;
   
   // Create a separator toolbar button
   TBBUTTON but = { 0 };
   but.fsStyle = TBSTYLE_SEP;
   but.fsState = TBSTATE_ENABLED;
   but.idCommand = s_nCmd;
   BOOL bRes = tb.InsertButton(iIndex, &but);
   ATLASSERT(bRes); bRes;
   TBBUTTONINFO info;
   info.cbSize = sizeof(info);
   info.dwMask = TBIF_SIZE;
   info.cx = cx;
   tb.SetButtonInfo(s_nCmd++, &info);

   // Chain the control to its new parent
   ::SetParent(hWnd, tb);
   // Position the new control on top of the separator
   RECT rc;
   tb.GetItemRect(iIndex, &rc);
   ::SetWindowPos(hWnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
}

bool CRemoteProject::_AddCommandBarImages(UINT nRes) const
{
   CImageListCtrl Images;
   Images.Create(16, 16, ILC_COLOR | ILC_MASK, 40, 1);
   CBitmap bmp;
   bmp.LoadBitmap(nRes);
   ATLASSERT(!bmp.IsNull());
   if( bmp.IsNull() ) return false;

   HRSRC hRsrc = ::FindResource(_Module.GetResourceInstance(), MAKEINTRESOURCE(nRes), (LPTSTR) RT_TOOLBAR);
   ATLASSERT(hRsrc);
   if( hRsrc == NULL ) return false;
   HGLOBAL hGlobal = ::LoadResource(_Module.GetResourceInstance(), hRsrc);
   ATLASSERT(hGlobal);
   if( hGlobal == NULL ) return false;

   struct _ToolBarData   // toolbar resource data
   {
      WORD wVersion;
      WORD wWidth;
      WORD wHeight;
      WORD wItemCount;
      WORD* GetItems() { return (WORD*)(this + 1); }
   };
   _ToolBarData* pData = (_ToolBarData*) ::LockResource(hGlobal);
   if( pData == NULL ) return FALSE;
   ATLASSERT(pData->wVersion==1);

   if( Images.Add(bmp, RGB(192,192,192)) == -1) return false;

   // Fill the array with command IDs
   WORD* pItems = pData->GetItems();
   int nItems = pData->wItemCount;
   for( int i = 0; i < nItems; i++ ) {
      CIcon icon = Images.GetIcon(i);
      ATLASSERT(!icon.IsNull());
      _pDevEnv->AddCommandBarImage(pItems[i], icon);
   }
   return true;
}

bool CRemoteProject::_CreateNewRemoteFile(HWND hWnd, IView* pView)
{
   CString sModule;
   sModule.Format(_T("%sTemplates.dll"), CModulePath());
   HINSTANCE hInst = ::LoadLibrary(sModule);
   if( hInst == NULL ) return false;

   typedef BOOL (APIENTRY* LPFNNEWFILE)(HWND, IDevEnv*, ISolution*, IProject*, IView*);
   LPFNNEWFILE fnCreate = (LPFNNEWFILE) ::GetProcAddress(hInst, "Templates_NewFile");
   if( fnCreate == NULL ) return false;
   if( !fnCreate(hWnd, _pDevEnv, _pDevEnv->GetSolution(), this, pView) ) return false;

   return TRUE;
}

bool CRemoteProject::_RunFileWizard(HWND hWnd, LPCTSTR pstrName, IView* pView)
{
   CString sModule;
   sModule.Format(_T("%sTemplates.dll"), CModulePath());
   HINSTANCE hInst = ::LoadLibrary(sModule);
   if( hInst == NULL ) return false;

   typedef BOOL (APIENTRY* LPFNRUNWIZARD)(HWND, LPCTSTR, IDevEnv*, ISolution*, IProject*, IView*);
   LPFNRUNWIZARD fnRunWizard = (LPFNRUNWIZARD) ::GetProcAddress(hInst, "Templates_RunWizard");
   if( fnRunWizard == NULL ) return false;
   if( !fnRunWizard(hWnd, pstrName, _pDevEnv, _pDevEnv->GetSolution(), this, pView) ) return false;

   return TRUE;
}

bool CRemoteProject::_CheckProjectFile(LPCTSTR pstrFilename, LPCTSTR pstrName, bool bRemote) 
{
   CString sUpperName = pstrName;
   sUpperName.MakeUpper();

   // Check that the file doesn't already exist in the
   // project filelist.
   for( int i = 0; i < m_aFiles.GetSize(); i++ ) {
      CString sName;
      m_aFiles[i]->GetFileName(sName.GetBufferSetLength(128), 128);
      sName.ReleaseBuffer();
      if( sName.CompareNoCase(pstrFilename) == 0 ) {
         GenerateError(_pDevEnv, IDS_ERR_FILEINCLUDED, 0);
         return false;
      }       
   }

   // Remove current tag information if it looks like a new
   // tag file has been added...
   if( sUpperName.Find(_T("TAGS")) >= 0 ) {
      m_viewClassTree.Lock();
      m_TagManager.m_TagInfo.MergeFile(pstrName);
      m_viewClassTree.Rebuild();
      m_viewClassTree.Unlock();
   }

   // New C++ files are added to class-view immediately if online-scanner
   // is running...
   if( bRemote 
       && (sUpperName.Find(_T(".H")) > 0 || sUpperName.Find(_T(".C")) > 0) ) 
   {
      if( m_TagManager.m_LexInfo.IsAvailable() ) {
         LPSTR pstrText = NULL;
         DWORD dwSize = 0;
         if( m_FileManager.LoadFile(pstrFilename, true, (LPBYTE*) &pstrText, &dwSize) ) {
            m_TagManager.m_LexInfo.MergeFile(pstrFilename, pstrText);
            free(pstrText);
         }
      }
   }

   return true;
}

IView* CRemoteProject::_CreateDependencyFile(LPCTSTR pstrFilename, LPCTSTR pstrName)
{
   // See if we can find the file...
   // The file may not be located in the project path, so we'll allow to
   // the system to search for the file.
   CString sFilename = m_FileManager.FindFile(pstrFilename);
   if( sFilename.IsEmpty() ) return NULL;
   
   // Create new object
   IView* pView = _pDevEnv->CreateView(pstrFilename, this, this);
   if( pView == NULL ) return false;

   // Load some default properties
   CViewSerializer arc;
   arc.Add(_T("name"), pstrName);
   arc.Add(_T("filename"), sFilename);
   arc.Add(_T("location"), _T("remote"));
   pView->Load(&arc);

   // Add view to collection
   m_aDependencies.Add(pView);

   return pView;
}

// Static members

CAccelerator CRemoteProject::m_accel;
CToolBarCtrl CRemoteProject::m_ctrlBuild;
CToolBarCtrl CRemoteProject::m_ctrlDebug;
CToolBarCtrl CRemoteProject::m_ctrlBookmarks;
CToolBarCtrl CRemoteProject::m_ctrlSearch;
CComboBox CRemoteProject::m_ctrlMode;
CClassView CRemoteProject::m_viewClassTree;
CTelnetView CRemoteProject::m_viewDebugLog;
CTelnetView CRemoteProject::m_viewCompileLog;
CWatchView CRemoteProject::m_viewWatch;
CStackView CRemoteProject::m_viewStack;
CBreakpointView CRemoteProject::m_viewBreakpoint;
CRegisterView CRemoteProject::m_viewRegister;
CMemoryView CRemoteProject::m_viewMemory;
CDisasmView CRemoteProject::m_viewDisassembly;
CVariableView CRemoteProject::m_viewVariable;
CThreadView CRemoteProject::m_viewThread;
CTelnetView CRemoteProject::m_viewOutput;
CRemoteDirView CRemoteProject::m_viewRemoteDir;

