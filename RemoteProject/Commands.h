#if !defined(AFX_COMMANDS_H__20050606_24A9_E070_94B5_0080AD509054__INCLUDED_)
#define AFX_COMMANDS_H__20050606_24A9_E070_94B5_0080AD509054__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CBuildSolutionThread : public CThreadImpl<CBuildSolutionThread>
{
public:
   DWORD Run();

   CRemoteProject* m_pProject;
};


#endif // !defined(AFX_COMMANDS_H__20050606_24A9_E070_94B5_0080AD509054__INCLUDED_)

