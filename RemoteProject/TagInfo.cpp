
#include "StdAfx.h"
#include "resource.h"

#include "TagInfo.h"
#include "Project.h"

#pragma code_seg( "MISC" )


////////////////////////////////////////////////////////
//

static CComAutoCriticalSection g_csTagData;

struct CLockTagDataInit
{
   CLockTagDataInit() { g_csTagData.Lock(); };
   ~CLockTagDataInit() { g_csTagData.Unlock(); };
};


////////////////////////////////////////////////////////
// CTagInfo

CTagInfo::CTagInfo() :
   m_bLoaded(false)
{
}

CTagInfo::~CTagInfo()
{
   // See the Clear() method for an explanation why we need to call it twice here
   Clear();
   Clear();
}

// Operations

void CTagInfo::Init(CRemoteProject* pProject)
{
   m_pProject = pProject;
}

bool CTagInfo::MergeFile(LPCTSTR /*pstrFilename*/)
{
   // TODO: Currently we just reload all the tag files again upon the
   //       file merge event. I assume that it would be better to maintain
   //       optimal sorting when dealing with multiple ctag files, but this 
   //       was never implemented.
   return _LoadTags();
}

void CTagInfo::Clear()
{
   CLockTagDataInit lock;
   // To alliviate a possible multi-thread problem we keep the "cleaned" tag memory
   // in a temporary list and delete it the next time around.
   // BUG: Investigate a real thread-safe cleanup solution.
   for( int i = 0; i < m_aDelayedFiles.GetSize(); i++ ) free(m_aDelayedFiles[i]);
   m_aDelayedFiles.RemoveAll();
   for( int j = 0; j < m_aFiles.GetSize(); j++ ) m_aDelayedFiles.Add(m_aFiles[j]);
   m_aFiles.RemoveAll();
   m_aTags.RemoveAll();
   m_bLoaded = false;
   m_bSorted = false;
}

bool CTagInfo::IsLoaded() const
{
   return m_bLoaded;
}

bool CTagInfo::IsAvailable() const
{
   ATLASSERT(m_pProject);
   for( int i = 0; i < m_pProject->GetItemCount(); i++ ) {
      IView* pView = m_pProject->GetItem(i);
      CString sName;
      pView->GetName(sName.GetBufferSetLength(MAX_PATH), MAX_PATH);
      sName.ReleaseBuffer();
      sName.MakeUpper();
      if( sName.Find(_T("TAGS")) >= 0 ) return true;
   }
   return false;
}

bool CTagInfo::FindItem(LPCTSTR pstrName, LPCTSTR pstrOwner, int iInheritance, DWORD dwTimeout, CSimpleValArray<TAGINFO*>& aResult)
{
   ATLASSERT(!::IsBadStringPtr(pstrName,-1));

   if( !m_bLoaded ) _LoadTags();
   if( m_aTags.GetSize() == 0 ) return false;

   CLockTagDataInit lock;

   CString sParentType;
   LPCTSTR pstrParent = NULL;   // Owner's parent (inheritance)
   if( pstrOwner != NULL && --iInheritance >= 0 && ::GetTickCount() < dwTimeout ) 
   {
      CSimpleValArray<TAGINFO*> aOwner;
      if( FindItem(pstrOwner, NULL, 0, dwTimeout, aOwner) ) {
         sParentType = _FindTagParent(aOwner[0]);
         if( !sParentType.IsEmpty() ) pstrParent = sParentType;
      }
   }

   CSimpleValArray<TAGINFO*> aResult1;
   CSimpleValArray<TAGINFO*> aResult2;
   CSimpleValArray<TAGINFO*> aResult3;
   if( m_bSorted )
   {
      // Binary search on sorted list
      int min = 0;
      int max = m_aTags.GetSize();
      int n = max / 2; 
      while( min < max ) { 
          int cmp = _tcscmp(pstrName, m_aTags[n].pstrName); 
          if( cmp == 0 ) break;
          if( cmp < 0 ) max = n; else min = n + 1;
          n = (min + max) / 2;
      }
      // Find first instance of the particular string
      if( min >= max ) return false;
      if( _tcscmp(pstrName, m_aTags[n].pstrName) != 0 ) return false;
      while( n > 0 && _tcscmp(pstrName, m_aTags[n - 1].pstrName) == 0 ) n--;
      while( n < max && _tcscmp(pstrName, m_aTags[n].pstrName) == 0 ) {
         if( pstrOwner != NULL && _tcscmp(m_aTags[n].pstrOwner, pstrOwner) == 0 ) {
            TAGINFO* pTag = &m_aTags.GetData()[n];
            aResult1.Add(pTag);
         }
         else if( pstrParent != NULL && _tcscmp(m_aTags[n].pstrOwner, pstrParent) == 0 ) {
            TAGINFO* pTag = &m_aTags.GetData()[n];
            aResult2.Add(pTag);
         }
         else if( pstrOwner == NULL ) {
            TAGINFO* pTag = &m_aTags.GetData()[n];
            aResult3.Add(pTag);
         }
         n++;
      }
   }
   else
   {
      // Scan list sequentially
      int nCount = m_aTags.GetSize();
      for( int n = 0; n < nCount; n++ ) {
         if( _tcscmp(m_aTags[n].pstrName, pstrName) == 0 ) {
            if( pstrOwner != NULL && _tcscmp(m_aTags[n].pstrOwner, pstrOwner) == 0 ) {
               TAGINFO* pTag = &m_aTags.GetData()[n];
               aResult1.Add(pTag);
            }
            else if( pstrParent != NULL && _tcscmp(m_aTags[n].pstrOwner, pstrParent) == 0 ) {
               TAGINFO* pTag = &m_aTags.GetData()[n];
               aResult2.Add(pTag);
            }
            else if( pstrOwner == NULL ) {
               TAGINFO* pTag = &m_aTags.GetData()[n];
               aResult3.Add(pTag);
            }
         }
      }
   }
   for( int x1 = 0; x1 < aResult1.GetSize(); x1++ ) aResult.Add(aResult1[x1]);
   for( int x2 = 0; x2 < aResult2.GetSize(); x2++ ) aResult.Add(aResult2[x2]);
   for( int x3 = 0; x3 < aResult3.GetSize(); x3++ ) aResult.Add(aResult3[x3]);

   return aResult.GetSize() > 0;
}

void CTagInfo::GetItemInfo(const TAGINFO* pTag, CTagDetails& Info)
{
   ATLASSERT(pTag);
   if( pTag == NULL ) return;
   Info.TagType = pTag->Type;
   Info.Protection = pTag->Protection;
   Info.sName = pTag->pstrName;
   Info.sBase = pTag->pstrOwner;
   Info.iLineNum = pTag->iLineNum;
   Info.sDeclaration = pTag->pstrDeclaration;
   Info.sRegExMatch = pTag->pstrRegExMatch;
   Info.sNamespace = pTag->pstrNamespace;
   Info.sComment = pTag->pstrComment;
   Info.sFilename = pTag->pstrFile;
   Info.sMemberOfScope = _T("");
   // CTAGS and similar will output regular expression stuff that
   // isn't even valid with Scintilla's reg.ex parser. So we strip
   // pre/post reg.ex stuff.
   Info.sRegExMatch.TrimLeft(_T("/$~^* \t.,;"));
   Info.sRegExMatch.TrimRight(_T("/$~^* \t.,;"));
   Info.sRegExMatch.Replace(_T("\\/"), _T("/"));
   Info.sDeclaration.Replace(_T("\\/"), _T("/"));
   Info.sDeclaration.TrimLeft(_T("/$~^* \t.,;"));
   Info.sDeclaration.TrimRight(_T("/$~^* \t.,;"));
   int iPos = Info.sDeclaration.Find(_T("// "));
   if( iPos > 0 ) {
      Info.sComment = Info.sDeclaration.Mid(iPos + 3);
      Info.sDeclaration = Info.sDeclaration.Left(iPos);
      Info.sDeclaration.TrimRight();
   }
}

bool CTagInfo::GetOuterList(CSimpleValArray<TAGINFO*>& aResult)
{
   if( !m_bLoaded ) _LoadTags();
   if( m_aTags.GetSize() == 0 ) return false;

   CLockTagDataInit lock;

   // List all classes/structs available in TAG file...
   int nCount = m_aTags.GetSize();
   for( int iIndex = 0; iIndex < nCount; iIndex++ ) {
      const TAGINFO& info = m_aTags[iIndex];
      switch( info.Type ) {
      case TAGTYPE_CLASS:
      case TAGTYPE_STRUCT:
      case TAGTYPE_TYPEDEF:
      case TAGTYPE_NAMESPACE:
         TAGINFO* pTag = &m_aTags.GetData()[iIndex];
         aResult.Add(pTag);
         break;
      }
   }

   return aResult.GetSize() > 0;
}

bool CTagInfo::GetGlobalList(CSimpleValArray<TAGINFO*>& aResult)
{
   if( !m_bLoaded ) _LoadTags();
   if( m_aTags.GetSize() == 0 ) return false;

   CLockTagDataInit lock;

   // List all classes/structs available in TAG file...
   int nCount = m_aTags.GetSize();
   for( int iIndex = 0; iIndex < nCount; iIndex++ ) {
      const TAGINFO& info = m_aTags[iIndex];
      // Must not be parent of something
      if( info.pstrOwner[0] != '\0' ) continue;
      if( info.pstrFile == NULL ) continue;
      // Functions and variables are accepted
      switch( info.Type ) {
      case TAGTYPE_MEMBER:
      case TAGTYPE_FUNCTION:
         TAGINFO* pTag = &m_aTags.GetData()[iIndex];
         aResult.Add(pTag);
      }
   }

   return aResult.GetSize() > 0;
}

bool CTagInfo::MatchSymbols(LPCTSTR pstrPattern, volatile bool& bCancel, CSimpleValArray<TAGINFO*>& aResult)
{
   if( !m_bLoaded ) _LoadTags();
   if( m_aTags.GetSize() == 0 ) return false;

   CLockTagDataInit lock;

   int nCount = m_aTags.GetSize();
   for( int iIndex = 0; iIndex < nCount; iIndex++ ) {
      const TAGINFO& info = m_aTags[iIndex];
      if( wildcmp(pstrPattern, info.pstrName) ) {
         TAGINFO* pTag = &m_aTags.GetData()[iIndex];
         aResult.Add(pTag);
      }
      if( bCancel ) return false;
   }

   return aResult.GetSize() > 0;
}

bool CTagInfo::GetMemberList(LPCTSTR pstrType, int iInheritance, DWORD dwTimeout, CSimpleValArray<TAGINFO*>& aResult)
{
   if( !m_bLoaded ) _LoadTags();
   if( m_aTags.GetSize() == 0 ) return false;

   CLockTagDataInit lock;

   // Now look up the members
   // OPTI: We'll have to look at all entries in the CTAG file here.
   //       To otherwise optimize this we could 'link' the entries at load-time, 
   //       which would be a slow/nasty process...
   int nCount = m_aTags.GetSize();
   for( int iIndex = 0; iIndex < nCount; iIndex++ ) {
      const TAGINFO& info = m_aTags[iIndex];
      if( _tcscmp(info.pstrOwner, pstrType) == 0 ) {
         TAGINFO* pTag = &m_aTags.GetData()[iIndex];
         aResult.Add(pTag);
      }
   }

   // Scan parent class as well?
   if( --iInheritance >= 0 && ::GetTickCount() < dwTimeout ) 
   {
      CSimpleValArray<TAGINFO*> aOwner;
      if( FindItem(pstrType, NULL, 0, dwTimeout, aOwner) ) {
         for( int i = 0; i < aOwner.GetSize() && ::GetTickCount() < dwTimeout; i++ ) {
            switch( aOwner[i]->Type ) {
            case TAGTYPE_CLASS:
            case TAGTYPE_STRUCT:
            case TAGTYPE_TYPEDEF:
               {
                  CString sParent = _FindTagParent(aOwner[i]);
                  if( !sParent.IsEmpty() ) GetMemberList(sParent, iInheritance, dwTimeout, aResult);
               }
               break;
            }
         }
      }
   }

   return aResult.GetSize() > 0;
}

bool CTagInfo::GetNamespaceList(LPCTSTR /*pstrType*/, DWORD /*dwTimeout*/, CSimpleValArray<TAGINFO*>& /*aResult*/)
{
   // CTAG files don't store namespace information!
   return false;
}

// Implementation

CString CTagInfo::_FindTagParent(const TAGINFO* pTag) const
{
   // Extract inheritance type.
   // HACK: We simply scoop up the " class CFoo : public CBar" text from
   //       the CTAG line. Unfortunately CTAG doesn't really carry that
   //       much information to safely determine the inheritance tree!
   static LPCTSTR aTokens[] = 
   {
      _T("public"),
      _T("protected"),
      _T("private"),
      _T("typedef"),
      _T("virtual"),
      _T(" : "),
   };
   for( int i = 0; i < sizeof(aTokens) / sizeof(aTokens[0]); i++ ) {
      LPCTSTR p = _tcsstr(pTag->pstrDeclaration, aTokens[i]);
      if( p == NULL ) continue;
      p += _tcslen(aTokens[i]);
      while( _istspace(*p) ) p++;
      CString sName;
      while( _istalnum(*p) || *p == '_' ) sName += *p++;
      return sName;      
   }
   return _T("");
}

bool CTagInfo::_LoadTags()
{
   ATLASSERT(m_aFiles.GetSize()==0);

   _pDevEnv->ShowStatusText(ID_DEFAULT_PANE, CString(MAKEINTRESOURCE(IDS_STATUS_LOADTAG)));

   Clear();

   CLockTagDataInit lock;

   // Mark as "loaded" even if we don't actually find any
   // tag files to load. This prevent repeated attempts to
   // load the files in the future.
   m_bLoaded = true;
   m_bSorted = true;

   // Look for all files in the project that starts with the name "TAGS".
   // BUG: We can accidentially load a perfectly good C++ files named "tags" but
   //      the parser will test for a valid ctags file. It will however slow things
   //      down.
   int i;
   for( i = 0; i < m_pProject->GetItemCount(); i++ ) {
      IView* pView = m_pProject->GetItem(i);
      CString sName;
      pView->GetName(sName.GetBufferSetLength(MAX_PATH), MAX_PATH);
      sName.ReleaseBuffer();
      sName.MakeUpper();
      if( sName.Find(_T("TAGS")) < 0 ) continue;
      // Load the file content. This gets us an UNICODE version
      // which is just what we need...
      CComBSTR bstrText;
      if( !pView->GetText(&bstrText) ) return false;
      if( bstrText.Length() == 0 ) continue;
      // Done. Now parse it...
      LPTSTR pstrText = (LPTSTR) malloc( (bstrText.Length() + 1) * sizeof(TCHAR) );
      if( pstrText == NULL ) return false;
      _tcscpy(pstrText, bstrText);
      if( _ParseTagFile(pstrText) ) {
         // File parsed successfully! Notice that we
         // own the memory because we only store pointers
         // into the text memory.
         m_aFiles.Add(pstrText);
      }
      else {
         free(pstrText);
      }
   }

   if( m_aFiles.GetSize() > 1 ) m_bSorted = false;

   // Check if list is really sorted.
   // NOTE: When multiple files are encountered we cannot rely
   //       on the list being sorted because we don't merge the files.
   //       This will significantly slow down the processing!!
   for( i = 0; m_bSorted && i < m_aTags.GetSize() - 50; i += 50 ) {
      if( _tcscmp(m_aTags[i].pstrName, m_aTags[i + 25].pstrName) > 0 ) m_bSorted = false;
   }

   return true;
}

bool CTagInfo::_ParseTagFile(LPTSTR pstrText)
{
   // Validate that this really is a tag file
   if( _tcsncmp(pstrText, _T("!_TAG"), 5) != 0 ) return false;

   // Parse all lines...
   int nCount = 0;
   for( LPTSTR pstrNext = NULL; *pstrText != '\0'; pstrText = pstrNext ) {
      // First find the EOL marker
      pstrNext = _tcschr(pstrText, '\n');
      if( pstrNext == NULL ) pstrNext = pstrText + _tcslen(pstrText) - 1;
      *pstrNext = '\0';
      if( *(pstrNext - 1) == '\r' ) *(pstrNext - 1) = '\0';
      pstrNext++;

      // Ignore comments; interpret header directives.
      if( *pstrText == '!' ) {
         if( _tcsstr(pstrText, _T("!_TAG_FILE_SORTED\t0")) != NULL ) m_bSorted = false;
         continue;
      }

      // Parse tag structure...
      TAGINFO info;

      LPTSTR p = _tcschr(pstrText, '\t');
      ATLASSERT(p);
      if( p == NULL ) continue;
      *p = '\0';
      info.pstrName = pstrText;

      pstrText = p + 1;
      p = _tcschr(pstrText, '\t');
      ATLASSERT(p);
      if( p == NULL ) continue;
      *p = '\0';
      info.pstrFile = pstrText;

      pstrText = p + 1;
      p = _tcschr(pstrText, '\t');
      ATLASSERT(p);
      if( p == NULL ) continue;
      *p = '\0';
      if( *(p - 2) == ';' ) *(p - 2) = '\0';
      info.pstrRegExMatch = pstrText;
      info.pstrDeclaration = pstrText;

      // Prefill before parsing extra fields
      static LPCTSTR pstrEmpty = _T("");
      info.Type = TAGTYPE_UNKNOWN;
      info.TagSource = TAGSOURCE_CTAGS;
      info.Protection = TAGPROTECTION_GLOBAL;
      info.pstrOwner = pstrEmpty;
      info.pstrComment = pstrEmpty;
      info.pstrNamespace = pstrEmpty;
      info.iLineNum = -1;

      if( _istdigit(info.pstrDeclaration[0]) ) {
         info.iLineNum = _ttoi(info.pstrDeclaration);
         info.pstrDeclaration = pstrEmpty;
         info.pstrRegExMatch = pstrEmpty;
      }

      // Parse the optional properties
      pstrText = p + 1;
      while( *pstrText != '\0' ) {
         p = _tcschr(pstrText, '\t');
         if( p != NULL ) *p = '\0';
         _ResolveExtraField(info, pstrText);
         if( p == NULL ) break;
         pstrText = p + 1;
      }

      // Correct weird things in the fields
      p = const_cast<LPTSTR>(_tcsstr(info.pstrOwner, _T("::<ano")));
      if( p != NULL ) *p = '\0';

      // Struggle with implementation vs function...
      if( nCount > 0 && info.Type == TAGTYPE_PROTOTYPE && info.pstrOwner[0] != '\0' ) {
         TAGINFO* prev = m_aTags.GetData() + nCount - 1;
         if( prev->Type == TAGTYPE_FUNCTION && _tcscmp(prev->pstrName, info.pstrName) == 0 && _tcscmp(prev->pstrOwner, info.pstrOwner) == 0 ) {
            prev->Type = TAGTYPE_IMPLEMENTATION;
            info.Type = TAGTYPE_FUNCTION;
         }
      }

      if( info.Type == TAGTYPE_PROTOTYPE ) continue;
      if( info.Type == TAGTYPE_INTRINSIC ) continue;

      m_aTags.Add(info);
      nCount++;
   }
   return true;
}

void CTagInfo::_ResolveExtraField(TAGINFO& info, LPCTSTR pstrField) const
{
   // Resolve 'kind' (type). Sample formats:
   //    c
   //    class
   //    kind:class
   if( info.Type == TAGTYPE_UNKNOWN )
   {
      if( pstrField[0] != '\0' && pstrField[1] == '\0' ) {
         switch( pstrField[0] ) {
         case 'e': info.Type = TAGTYPE_ENUM; break;
         case 'c': info.Type = TAGTYPE_CLASS; break;
         case 'm': info.Type = TAGTYPE_MEMBER; break;
         case 'v': info.Type = TAGTYPE_MEMBER; break;
         case 'd': info.Type = TAGTYPE_DEFINE; break;
         case 's': info.Type = TAGTYPE_STRUCT; break;
         case 'u': info.Type = TAGTYPE_STRUCT; break;
         case 't': info.Type = TAGTYPE_TYPEDEF; break;
         case 'g': info.Type = TAGTYPE_TYPEDEF; break;
         case 'f': info.Type = TAGTYPE_FUNCTION; break;
         case 'p': info.Type = TAGTYPE_PROTOTYPE; break;
         case 'F': info.Type = TAGTYPE_INTRINSIC; break;
         }
      }
      else if( _tcsncmp(pstrField, _T("kind:"), 5) == 0 ) {
         if( _tcscmp(pstrField + 5, _T("class")) == 0 )           info.Type = TAGTYPE_CLASS;
         else if( _tcscmp(pstrField + 5, _T("macro")) == 0 )      info.Type = TAGTYPE_DEFINE;
         else if( _tcscmp(pstrField + 5, _T("member")) == 0 )     info.Type = TAGTYPE_MEMBER;
         else if( _tcscmp(pstrField + 5, _T("struct")) == 0 )     info.Type = TAGTYPE_STRUCT;
         else if( _tcscmp(pstrField + 5, _T("typedef")) == 0 )    info.Type = TAGTYPE_TYPEDEF;
         else if( _tcscmp(pstrField + 5, _T("enumerator")) == 0 ) info.Type = TAGTYPE_ENUM;
         else if( _tcscmp(pstrField + 5, _T("function")) == 0 )   info.Type = TAGTYPE_FUNCTION;
         else if( _tcscmp(pstrField + 5, _T("prototype")) == 0 )  info.Type = TAGTYPE_PROTOTYPE;
      }
      else if( _tcscmp(pstrField, _T("class")) == 0 )      info.Type = TAGTYPE_CLASS;
      else if( _tcscmp(pstrField, _T("macro")) == 0 )      info.Type = TAGTYPE_DEFINE;
      else if( _tcscmp(pstrField, _T("member")) == 0 )     info.Type = TAGTYPE_MEMBER;
      else if( _tcscmp(pstrField, _T("variable")) == 0 )   info.Type = TAGTYPE_MEMBER;
      else if( _tcscmp(pstrField, _T("struct")) == 0 )     info.Type = TAGTYPE_STRUCT;
      else if( _tcscmp(pstrField, _T("typedef")) == 0 )    info.Type = TAGTYPE_TYPEDEF;
      else if( _tcscmp(pstrField, _T("enumerator")) == 0 ) info.Type = TAGTYPE_ENUM;
      else if( _tcscmp(pstrField, _T("function")) == 0 )   info.Type = TAGTYPE_FUNCTION;
      else if( _tcscmp(pstrField, _T("prototype")) == 0 )  info.Type = TAGTYPE_PROTOTYPE;
   }
   // Resolve owner. Sample format:
   //    struct:<typename>
   if( info.pstrOwner[0] == '\0' )
   {
      if( _tcsncmp(pstrField, _T("struct:"), 7) == 0 )     info.pstrOwner = pstrField + 7;
      else if( _tcsncmp(pstrField, _T("class:"), 6) == 0 ) info.pstrOwner = pstrField + 6;
      else if( _tcsncmp(pstrField, _T("enum:"), 5) == 0 )  info.pstrOwner = pstrField + 5;
   }
   // Resolve protection. Sample format:
   //    access:private
   if( info.Protection == TAGPROTECTION_GLOBAL )
   {
      if( _tcsncmp(pstrField, _T("access:"), 7) == 0 ) {
         if( _tcscmp(pstrField + 7, _T("public")) == 0 )         info.Protection = TAGPROTECTION_PUBLIC;
         else if( _tcscmp(pstrField + 7, _T("protected")) == 0 ) info.Protection = TAGPROTECTION_PROTECTED;
         else if( _tcscmp(pstrField + 7, _T("private")) == 0 )   info.Protection = TAGPROTECTION_PRIVATE;
      }
   }
   // Resolve declaration. Sample format:
   //    signature:<reg.ex>
   if( info.pstrDeclaration[0] == '\0' )
   {
      if( _tcsncmp(pstrField, _T("signature:"), 10) == 0 ) info.pstrDeclaration = pstrField + 10;
   }
   // Resolve line-number. Sample format:
   //    line:54
   if( _tcsncmp(pstrField, _T("line:"), 5) == 0 ) info.iLineNum = _ttoi(pstrField + 5);
}

