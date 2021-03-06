
#include "StdAfx.h"

#include "MiInfo.h"

#pragma code_seg( "MISC" )


/////////////////////////////////////////////////////////////////
// CMiInfo

CMiInfo::CMiInfo(const CMiInfo& src) :
   m_iSearchIndex(-1),
   m_pstrData(NULL),
   m_plRefCount(NULL)
{
   Copy(src);
}

CMiInfo::CMiInfo(LPCTSTR pstrInput /*= NULL*/) :
   m_iSearchIndex(-1),
   m_pstrData(NULL),
   m_plRefCount(NULL)
{
   if( pstrInput != NULL ) Parse(pstrInput);
}

CMiInfo::~CMiInfo()
{
   Release();
}

// Operations

void CMiInfo::Release()
{
   // TODO: Really need to better protect the data members
   //       with a thread-lock.
   if( m_plRefCount != NULL && ::InterlockedDecrement(m_plRefCount) == 0 ) {
      free(m_plRefCount);
   }
   m_pstrData = NULL;
   m_plRefCount = NULL;
   m_aItems.RemoveAll();
}

bool CMiInfo::Parse(LPCTSTR pstrInput)
{
   ATLASSERT(!::IsBadStringPtr(pstrInput,-1));
   Release();
   ATLASSERT(m_pstrData==NULL);
   if( *pstrInput == ',' ) pstrInput++;
   // We're using a slightly complicated (and naive) buffer/reference-counting scheme
   // to keep the data alive as long as its being used.
   // Here we allocate the buffer for the parse string + 4 bytes for the ref.count.
   // The parse string is copied to the memory location after the ref.count.
   DWORD dwLength = (_tcslen(pstrInput) + 1) * sizeof(TCHAR);
   m_plRefCount = (LONG*) malloc(dwLength + sizeof(LONG));
   ATLASSERT(m_plRefCount);
   if( m_plRefCount == NULL ) return false;
   *m_plRefCount = 1L;
   m_pstrData = (LPTSTR) (m_plRefCount + 1);
   memcpy(m_pstrData, pstrInput, dwLength);
#ifdef _DEBUG
   ::OutputDebugString(_T("DBG: "));
   ::OutputDebugString(pstrInput);
   ::OutputDebugString(_T("\n"));
#endif // _DEBUG
   if( *pstrInput == '\0' ) return true;
   return _ParseString(m_pstrData, 0, _T(""), _T(""), 0);
}

CString CMiInfo::GetItem(LPCTSTR pstrKey, 
                         LPCTSTR pstrGroup /*= NULL*/, 
                         LPCTSTR pstrFrame /*= NULL*/, 
                         long lPosition /*= 0*/)
{
   long lCurPos = 0;
   for( int i = 0; i < m_aItems.GetSize(); i++ ) {
      const MIINFO& info = m_aItems[i];
      if( _tcscmp(info.pstrKey, pstrKey) != 0 ) continue;
      if( pstrFrame != NULL && _tcscmp(info.pstrFrame, pstrFrame) != 0 ) continue;
      if( pstrGroup != NULL && _tcscmp(info.pstrGroup, pstrGroup) != 0 ) continue;
      if( lCurPos++ != lPosition ) continue;
      // Found it; remember position in list so we can continue
      // at this point using FindNext().
      m_iSearchIndex = i;
      return info.pstrValue;
   }
   m_iSearchIndex = INT_MAX;
   return _T("");
}

CString CMiInfo::GetSubItem(LPCTSTR pstrKey) const
{
   ATLASSERT(m_iSearchIndex>=0 && m_iSearchIndex<m_aItems.GetSize());
   short iIndex = m_aItems[m_iSearchIndex].iIndex;
   for( int i = m_iSearchIndex; i < m_aItems.GetSize(); i++ ) {
      const MIINFO& info = m_aItems[i];
      if( info.iIndex != iIndex ) break;
      if( _tcscmp(info.pstrKey, pstrKey) != 0 ) continue;
      return info.pstrValue;
   }
   return _T("");
}

CString CMiInfo::FindNext(LPCTSTR pstrKey, 
                          LPCTSTR pstrGroup /*= NULL*/, 
                          LPCTSTR pstrFrame /*= NULL*/)
{
   ATLASSERT(m_iSearchIndex>=0 && m_iSearchIndex<m_aItems.GetSize());
   for( int i = m_iSearchIndex + 1; i < m_aItems.GetSize(); i++ ) {
      const MIINFO& info = m_aItems[i];
      if( _tcscmp(info.pstrKey, pstrKey) != 0 ) continue;
      if( pstrFrame != NULL && _tcscmp(info.pstrFrame, pstrFrame) != 0 ) continue;
      if( pstrGroup != NULL && _tcscmp(info.pstrGroup, pstrGroup) != 0 ) continue;
      m_iSearchIndex = i;
      return info.pstrValue;
   }
   return _T("");
}

void CMiInfo::Copy(const CMiInfo& src)
{
   Release();
   m_pstrData = src.m_pstrData;
   m_plRefCount = src.m_plRefCount;
   for( int i = 0; i < src.m_aItems.GetSize(); i++ ) {
      MIINFO mi = src.m_aItems[i];
      m_aItems.Add(mi);
   }
   if( m_plRefCount != NULL ) ::InterlockedIncrement(m_plRefCount);
}

// Implementation

bool CMiInfo::_ParseString(LPTSTR pstrSrc, int iStart, LPCTSTR pstrGroup, LPCTSTR pstrFrame, short iIndex)
{
   int iEnd = 0;
   while( pstrSrc[iStart] != '\0' ) {

      // Extract key if available.
      // There's a number of examples to consider:
      //   register-names=["eax","ecx","edx"]
      //   stack=[frame={level="0",addr="0x77f82926",func="??"}]
      //   func="main",args=[{name="a",value="1"}, {name="b",value="2"}]
      LPCTSTR pstrKey = pstrFrame;
      if( pstrSrc[iStart] != '{' 
          && pstrSrc[iStart] != '[' 
          && pstrSrc[iStart] != '\"' ) 
      {
         pstrKey = pstrSrc + iStart;
         while( pstrSrc[iStart] != '\0' && pstrSrc[iStart] != '=' ) {
            iStart++;
         }
         if( pstrSrc[iStart] == '=' ) pstrSrc[iStart++] = '\0';
      }

      // Check for array (frame)
      if( pstrSrc[iStart] == '[' ) {
         iStart++;
         iEnd = iStart;
         if( !_FindBlockEnd(pstrSrc, iEnd) ) return false;
         if( !_ParseString(pstrSrc, iStart, pstrGroup, pstrKey, 0) ) return false;
         iEnd++;
         goto next;
      }
   
      // Check for nested record (group)
      while( pstrSrc[iStart] == '{' ) {
         iStart++;
         iEnd = iStart;
         if( !_FindBlockEnd(pstrSrc, iEnd) ) return false;
         if( !_ParseString(pstrSrc, iStart, pstrKey, pstrFrame, ++iIndex) ) return false;
         iEnd++;
         goto next;
      }

      // Extract value string
      ATLASSERT(pstrSrc[iStart]=='\"');
      if( pstrSrc[iStart++] != '\"' ) return false;
      iEnd = iStart;
      while( pstrSrc[iEnd] != '\0' && pstrSrc[iEnd] != '\"' ) {
         if( pstrSrc[iEnd] == '\\' ) iEnd++;
         iEnd++;
      }
      if( pstrSrc[iEnd] == '\"' ) pstrSrc[iEnd] = '\0';

      // Convert it from escaped back to plain text
      _ConvertToPlainText(pstrSrc + iStart);

      // Add item
      {
         MIINFO info = { 0 };
         info.pstrFrame = pstrFrame;
         info.pstrGroup = pstrGroup;
         info.pstrKey = pstrKey;
         info.pstrValue = pstrSrc + iStart;
         info.iIndex = iIndex;
         m_aItems.Add(info);
         //ATLTRACE("Item: [%ls=%ls] [f=%ls] [g=%ls] (i=%ld)\n", info.pstrKey, info.pstrValue, info.pstrFrame, info.pstrGroup, (long) iIndex);
      }

      iEnd++;

next:
      // Check next item
      iStart = iEnd;
      if( pstrSrc[iStart] == ',' ) iStart++;
   }
   return true;
}

bool CMiInfo::_FindBlockEnd(LPTSTR pstrSrc, int& iPos) const
{
   ATLASSERT(!::IsBadStringPtr(pstrSrc,-1));
   int iLevel = 1;
   while( true ) {
      switch( pstrSrc[iPos] ) {
      case '\0':
         return false;
      case '{':
      case '[':
         iLevel++;
         break;
      case '}':
      case ']':
         if( --iLevel != 0 ) break;
         pstrSrc[iPos] = '\0';
         return true;
      case '\"':
         iPos++;
         while( pstrSrc[iPos] != '\0' && pstrSrc[iPos] != '\"' ) 
         {
            if( pstrSrc[iPos] == '\\' ) iPos++;
            iPos++;
         }
         if( pstrSrc[iPos] == '\0' ) return false;
      }
      iPos++;
   }   
   return false;  // Unreachable...
}

void CMiInfo::_ConvertToPlainText(LPTSTR pstrSrc)
{
   ATLASSERT(!::IsBadWritePtr(pstrSrc,_tcslen(pstrSrc)));
   // Converts from C-style (escaped) to ASCII text.
   // Notice that the string is converted inplace!
   // TODO: Do we need to be aware of codepage or utf-8 here?
   LPCTSTR pstrText = pstrSrc;
   LPTSTR pstrDest = pstrSrc;
   while( *pstrSrc != '\0' ) {
      if( *pstrSrc == '\\' ) {
         pstrSrc++;
         // We translate cursor-chars (\r\n\t) to spaces
         if( *pstrSrc == 'r' || *pstrSrc == 'n' || *pstrSrc == 't' ) *pstrSrc = ' ';
      }
      *pstrDest++ = *pstrSrc++;
   }
   *pstrDest = '\0';
}

