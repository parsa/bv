#if !defined(AFX_PLUGIN_H__20030304_FEC7_8E13_D7CB_0080AD509054__INCLUDED_)
#define AFX_PLUGIN_H__20030304_FEC7_8E13_D7CB_0080AD509054__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


///////////////////////////////////////////////////
//

class CPlugin
{
private:
   typedef VOID (CALLBACK* LPFNSETMENU)(HMENU);
   typedef VOID (CALLBACK* LPFNSETPOPUPMENU)(IElement*, HMENU);

   CLoadLibrary m_Lib;
   CString m_sFilename;
   CString m_sName;
   LONG m_lType;
   BOOL m_bActive;
   BOOL m_bLoaded;

   LPFNSETMENU m_pSetMenu;
   LPFNSETPOPUPMENU m_pSetPopupMenu;

public:
   void Init(LPCTSTR pstrFilename, BOOL bActive);
   BOOL LoadPackage(IDevEnv* pDevEnv);
   //
   CString GetFilename() const;
   CString GetName() const;
   CString GetDescription() const;
   LONG GetType() const;
   BOOL IsLoaded() const;
   BOOL IsMarkedActive() const;
   void ChangeActiveState(BOOL bActive);

   UINT QueryAcceptFile(LPCTSTR pstrFilename) const;
   IView* CreateView(LPCTSTR pstrFilename, IProject* pProject, IElement* pParent) const;

   IProject* CreateProject();
   BOOL DestroyProject(IProject* pProject);

   void SetMenu(HMENU hMenu);
   void SetPopupMenu(IElement* pElement, HMENU hMenu);
};


///////////////////////////////////////////////////
//

class CPluginLoader
{
public:
   DWORD Run();

   void _LoadPlugins();
   BOOL _LoadPlugin(CPlugin& plugin);
   void _SendReadySignal();
};


///////////////////////////////////////////////////
//

extern CSimpleArray<CPlugin> g_aPlugins;



#endif // !defined(AFX_PLUGIN_H__20030304_FEC7_8E13_D7CB_0080AD509054__INCLUDED_)

