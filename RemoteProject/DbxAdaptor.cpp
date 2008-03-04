
#include "StdAfx.h"
#include "resource.h"

#include "Project.h"

#include "DbxAdaptor.h"

#pragma code_seg( "PROTOCOLS" )


////////////////////////////////////////////////////////
//

CDbxAdaptor::CDbxAdaptor() : m_State(DBX_UNKNOWN), m_bSeenPrompt(false), m_bThreadSupport(true)
{   
}

CDbxAdaptor::~CDbxAdaptor()
{
}

// IDebuggerAdaptor

void CDbxAdaptor::Init(CRemoteProject* /*pProject*/)
{
   m_sReturnValue.GetBuffer(8000);
   m_sReturnValue.ReleaseBuffer(0);
   m_sReturnValue = _T("(gdb)");
   m_aWatches.RemoveAll();
}

/**
 * Transform debugger input command from GDB MI format to DBX equivalent.
 */
CString CDbxAdaptor::TransformInput(LPCTSTR pstrInput)
{
   // Internally we use GDB MI commands to control a debugger, so we'll
   // have to convert all commands to native DBX syntax before sending
   // them to the remote shell.
   // Just like the GDB debugger is prefixing with 232 sentinel we'll
   // also prefix DBX commands. This allows us to recognize the echo
   // that some protocols will insist on delivering.
   CString sFullArgs;
   CSimpleArray<CString> aArgs;
   _SplitCommand(pstrInput, aArgs, sFullArgs);
   int iIndex = 0;
   CString sCommand = _GetArg(aArgs, iIndex);
   CString sOut;
   if( sCommand == _T("-exec-next") )
   {
      return _T("next;  ## dbx");
   }
   if( sCommand == _T("-exec-step") )
   {
      return _T("step;  ## dbx");
   }
   if( sCommand == _T("-exec-step-instruction") )
   {
      return _T("stepi;  ## dbx");
   }
   if( sCommand == _T("-exec-run") )
   {
      return _T("run;  ## dbx");
   }
   if( sCommand == _T("-exec-continue") )
   {
      return _T("cont;  ## dbx");
   }   
   if( sCommand == _T("-exec-finish") )
   {
      return _T("step up;  ## dbx");
   }
   if( sCommand == _T("-exec-until") )
   {
      CString sFile, sLineNum;
      _GetInputFileLineArgs(sFullArgs, sFile, sLineNum);
      sOut.Format(_T("stop at \"%s\"%s -temp; cont;  ## dbx"), sFile, sLineNum);
      return sOut;
   }
   if( sCommand == _T("-exec-jump") )
   {
      CString sFile, sLineNum;
      _GetInputFileLineArgs(sFullArgs, sFile, sLineNum);
      sOut.Format(_T("cont at %ld;  ## dbx"), _ttol(sLineNum.Mid(1)));
      return sOut;
   }
   if( sCommand == _T("-data-evaluate-expression") )
   {
      sOut.Format(_T("print %s;  ## dbx"), _GetArg(aArgs, iIndex));
      return sOut;
   }
   if( sCommand == _T("-data-read-memory") )
   {
      CString sAddress = _GetArg(aArgs, iIndex);
      CString sFormat = _GetArg(aArgs, iIndex);
      CString sByteSize = _GetArg(aArgs, iIndex);
      CString sRowSize = _GetArg(aArgs, iIndex);
      if( sByteSize == _T("4") ) sFormat.MakeUpper();
      sOut.Format(_T("examine %s / %ld %s;  ## dbx"), sAddress, _ttol(sRowSize) * _ttol(sByteSize), sFormat);
      return sOut;
   }
   if( sCommand == _T("-var-create") )
   {
      CString sName = _GetArg(aArgs, iIndex);
      _GetArg(aArgs, iIndex);
      CString sExpr = _GetArg(aArgs, iIndex);
      if( !m_aWatches.SetAt(sName, sExpr) ) m_aWatches.Add(sName, sExpr);
      sOut.Format(_T("display %s;  ## dbx"), sExpr);
      return sOut;
   }
   if( sCommand == _T("-var-delete") )
   {
      CString sExpr, sName = _GetArg(aArgs, iIndex);
      long lKeyIndex = m_aWatches.FindKey(sName);
      if( lKeyIndex < 0 ) return _T("");
      sExpr = m_aWatches.GetValueAt(lKeyIndex);
      sOut.Format(_T("undisplay %s;  ## dbx"), sExpr);
      return sOut;
   }
   if( sCommand == _T("-var-info-type") )
   {
      CString sExpr, sName = _GetArg(aArgs, iIndex);
      long lKeyIndex = m_aWatches.FindKey(sName);
      if( lKeyIndex < 0 ) return _T("");
      sExpr = m_aWatches.GetValueAt(lKeyIndex);
      sOut.Format(_T("whatis %s;  ## dbx"), sExpr);
      return sOut;
   }
   if( sCommand == _T("-var-evaluate-expression") )
   {
      CString sExpr, sName = _GetArg(aArgs, iIndex);
      long lKeyIndex = m_aWatches.FindKey(sName);
      if( lKeyIndex < 0 ) return _T("");
      sExpr = m_aWatches.GetValueAt(lKeyIndex);
      sOut.Format(_T("print %s;  ## dbx"), sExpr);
      return sOut;
   }
   if( sCommand == _T("-var-list-children") )
   {
      CString sExpr, sName = _GetArg(aArgs, iIndex);
      long lKeyIndex = m_aWatches.FindKey(sName);
      if( lKeyIndex >= 0 ) sExpr = m_aWatches.GetValueAt(lKeyIndex);
      sOut.Format(_T("print -r %s;  ## dbx"), sExpr);
      return sOut;
   }
   if( sCommand == _T("-var-update") )
   {
   }
   if( sCommand == _T("-stack-list-arguments") )
   {
      return _T("where 1;  ## dbx");
   }
   if( sCommand == _T("-stack-list-locals") )
   {
      return _T("dump;  ## dbx");
   }
   if( sCommand == _T("-stack-list-frames") )
   {
      return _T("where -l -v;  ## dbx");
   }
   if( sCommand == _T("-thread-select") )
   {
      sOut.Format(_T("%s%ld;  ## dbx"), m_bThreadSupport ? _T("thread t@") : _T("lwp l@"), _ttol(sFullArgs));
      return sOut;
   }
   if( sCommand == _T("-thread-list-ids") )
   {
      sOut.Format(_T("%s; ## dbx"), m_bThreadSupport ? _T("threads") : _T("lwps"));
      return sOut;
   }   
   if( sCommand == _T("-stack-select-frame") )
   {
      sOut.Format(_T("frame %ld;  ## dbx"), _ttol(_GetArg(aArgs, iIndex)) + 1);
      return sOut;
   }
   if( sCommand == _T("-data-disassemble") )
   {
      if( aArgs.GetSize() < 3 ) return _T("");
      sOut.Format(_T("dis %s;  ## dbx"), aArgs[2]);
      return sOut;
   }   
   if( sCommand == _T("-data-list-register-names") )
   {
      return _T("regs -F; ## dbx names");
   }
   if( sCommand == _T("-data-list-register-values") )
   {
      return _T("regs -F;  ## dbx");
   }
   if( sCommand == _T("-data-read-memory") )
   {
      if( aArgs.GetSize() < 5 ) return _T("");
      sOut.Format(_T("examine %s %s;  ## dbx"), aArgs[1], aArgs[3]);
      return sOut;
   }   
   if( sCommand == _T("-thread-list-ids") )
   {
      return _T("threads;  ## dbx");
   }
   if( sCommand == _T("-exec-arguments") )
   {
      sOut.Format(_T("runargs %s;  ## dbx"), sFullArgs);
      return sOut;
   }
   if( sCommand == _T("-break-insert") )
   {
      CString sMarkTemp;
      if( _SkipArg(aArgs, iIndex, _T("-t")) ) sMarkTemp = _T("-temp");
      CString sFile, sLineNum;
      _GetInputFileLineArgs(_GetArg(aArgs, iIndex), sFile, sLineNum);
      if( sLineNum.IsEmpty() ) sOut.Format(_T("stop in %s %s"), sFile, sMarkTemp);
      else sOut.Format(_T("stop at \"%s\"%s %s;  ## dbx"), sFile, sLineNum, sMarkTemp);
      return sOut;
   }
   if( sCommand == _T("-break-delete") )
   {
      sOut.Format(_T("delete %s;  ## dbx"), sFullArgs);
      return sOut;
   }
   if( sCommand == _T("-break-after") )
   {
      sOut.Format(_T("handler -count %s;  ## dbx"), sFullArgs);
      return sOut;
   }     
   if( sCommand == _T("-break-condition") )
   {
      sOut.Format(_T("when %s;  ## dbx"), sFullArgs);
      return sOut;
   }     
   if( sCommand == _T("-break-enable") )
   {
      sOut.Format(_T("handler -enable %s;  ## dbx"), sFullArgs);
      return sOut;
   }  
   if( sCommand == _T("-break-disable") )
   {
      sOut.Format(_T("handler -disable %s;  ## dbx"), sFullArgs);
      return sOut;
   }  
   if( sCommand == _T("-break-list") )
   {      
      return _T("status;  ## dbx");
   }  
   if( sCommand == _T("-gdb-set") )
   {
      if( _SkipArg(aArgs, iIndex, _T("confirm")) ) {
         return _T("set $page=0; set $prompt=\"(dbx) \"; PS1=\"(dbx) \";  ## dbx");
      }
      if( _SkipArg(aArgs, iIndex, _T("variable")) ) {
         sOut.Format(_T("assign %s;  ## dbx"), sFullArgs.Mid(9));  // "variable "=len9
         return sOut;
      }
   }
   if( sCommand == _T("-environment-pwd") )
   {
      return _T("pwd;  ## dbx");
   }
   if( sCommand == _T("-environment-path") )
   {
   }
   if( sCommand == _T("-environment-directory") )
   {
      sOut.Format(_T("use %s;  ## dbx"), sFullArgs);
      return sOut;
   }
   if( sCommand == _T("-interpreter-exec") ) {
      if( _SkipArg(aArgs, iIndex, _T("console")) ) {
         sOut = _GetArg(aArgs, iIndex);
         sOut.Replace(_T("\\\\"), _T("\\"));
         sOut.Replace(_T("\\\""), _T("\""));
         return sOut;
      }
   }
   if( sCommand == _T("-gdb-exit") )
   {
      return _T("quit;  ## dbx");
   }
   return _T("");   
}

/**
 * Transform DBX output to GDB MI format.
 */
CString CDbxAdaptor::TransformOutput(LPCTSTR pstrOutput)
{
   // Internally we only support the GDB MI format, so we need to transform
   // any output result from native DBX format back to GDB MI format.
   // We use a simple state-machine to keep track of when the output lines might
   // be related to since we also see the most recent command submitted.
   if( _tcsncmp(pstrOutput, _T("(dbx"), 4) == 0 ) 
   {
      // Confused by running state?
      if( m_State == DBX_RUNNING && m_sReturnValue == _T("(gdb)") ) m_sReturnValue = _T("232*stopped,");
      // Update state in state-machine
      struct
      {
         DBXSTATE State;
         LPCTSTR pstrCommand;
         LPCTSTR pstrEmptyResult;
      } cat[] = 
      {
         { DBX_RUNARGS,   _T("(dbx) runargs")                 _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) run"),                    _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) cont"),                   _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) next"),                   _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) step"),                   _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) stepi"),                  _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) until"),                  _T("(gdb)") },
         { DBX_RUNNING,   _T("(dbx) return"),                 _T("(gdb)") },
         { DBX_STACKARGS, _T("(dbx) where 1"),                _T("(gdb)") },
         { DBX_WHERE,     _T("(dbx) where"),                  _T("(gdb)") },
         { DBX_PRINTC,    _T("(dbx) print -r"),               _T("(gdb)") },
         { DBX_PRINT,     _T("(dbx) print"),                  _T("(gdb)") },
         { DBX_DUMP,      _T("(dbx) dump"),                   _T("232^done,locals=[]") },
         { DBX_DISPLAY,   _T("(dbx) display"),                _T("(gdb)") },
         { DBX_DIS,       _T("(dbx) dis"),                    _T("(gdb)") },
         { DBX_REGNAMES,  _T("(dbx) regs -F;  ## dbx names"), _T("(gdb)") },
         { DBX_REGS,      _T("(dbx) regs -F"),                _T("(gdb)") },
         { DBX_FRAME,     _T("(dbx) frame"),                  _T("(gdb)") },
         { DBX_STOP,      _T("(dbx) stop"),                   _T("(gdb)") },
         { DBX_STATUS,    _T("(dbx) status"),                 _T("232^done,BreakpointTable={nr_rows=\"0\",nr_cols=\"9\",hdr=[{}],body=[]") },
         { DBX_THREADS,   _T("(dbx) threads"),                _T("232^done,thread-ids={}") },
         { DBX_LWPS,      _T("(dbx) lwps"),                   _T("232^done,thread-ids={}") },
         { DBX_EXAMINE,   _T("(dbx) examine"),                _T("232^done,addr=\"\",memory=[]") },
         { DBX_PWD,       _T("(dbx) pwd"),                    _T("(gdb)") },
         { DBX_UNKNOWN,   _T("(dbx)"),                        _T("(gdb)") },
      };
      CString sNewReturnValue = _T("(gdb)");
      for( int i = 0; i < sizeof(cat) / sizeof(cat[0]); i++ ) {
         if( _tcsncmp(pstrOutput, cat[i].pstrCommand, _tcslen(cat[i].pstrCommand)) == 0 ) {
            m_State = cat[i].State;
            sNewReturnValue = cat[i].pstrEmptyResult;
            break;
         }
      }
      // By default we will just translate the inputted command line to the standard GDB prompt
      // or the empty MI answer for that action, but the state-machine may collect an elaborate 
      // answer and we should then return this on the next prompt.
      CString sOldReturnValue = m_sReturnValue;
      m_sReturnValue = sNewReturnValue;
      m_lReturnIndex = 0;
      m_bSeenPrompt = true;
      if( m_State == DBX_STOP && _tcsstr(pstrOutput, _T("; cont")) != NULL ) m_State = DBX_RUNNING;
      if( m_State == DBX_RUNNING && sOldReturnValue == _T("(gdb)") ) sOldReturnValue = _T("232^running,");
      return sOldReturnValue;
   }
   if( _tcsncmp(pstrOutput, _T("dbx:"), 4) == 0 ) 
   {
      CString sText = pstrOutput + 5;
      sText.Replace(_T("\""), _T("\\\""));
      CString sMi;
      if( sText == _T("program is not active") ) return _T("232^exit");
      if( sText == _T("can't continue execution -- no active process") ) return _T("232^exit");
      if( sText == _T("thread related commands not available") ) m_bThreadSupport = false;      
      sMi.Format(_T("232&\"%s\""), sText);
      if( _tcsstr(pstrOutput, _T("syntax error")) != NULL ) sMi.Format(_T("232^error,msg=\"%s\""), sText);
      return sMi;
   }
   if( *pstrOutput == '\0' || _tcsstr(pstrOutput, _T(" ## dbx")) != NULL ) 
   {
      // We're not seing our own or empty commands
   }
   else
   {
      CString sMi;
      CString sFullArgs;
      CSimpleArray<CString> aArgs;
      _SplitCommand(pstrOutput, aArgs, sFullArgs);
      int iIndex = 0;
      if( _SkipArg(aArgs, iIndex, _T("Process")) ) _GetArg(aArgs, iIndex);
      switch( m_State ) {
      case DBX_RUNNING:
         {
            CString sCommand;
            long lBreakpointNo = 0;
            _GetNumericArg(aArgs, iIndex, lBreakpointNo);
            while( true ) {
               sCommand = _GetArg(aArgs, iIndex);
               if( sCommand.Find('@') < 0 ) break;
            }
            CString sCommandCase = sCommand;
            sCommand.MakeLower();
            if( sCommand == _T("stopped") 
                || sCommand == _T("bus") 
                || sCommand == _T("signal") 
                || sCommand == _T("attached") 
                || sCommand == _T("floating") 
                || sCommand == _T("interrupt") 
                || sCommand == _T("segmentation") 
                || sCommandCase.Find(_T("SIG")) == 0 
                || sCommandCase.Find(_T(" in file ")) > 0 ) 
            {
               CString sReasonValue, sReason = _T("end-stepping-range");
               if( lBreakpointNo > 0 ) {
                  sReason = _T("breakpoint");
                  sReasonValue.Format(_T("bkptno=\"%ld\","), lBreakpointNo);
               }
               if( sCommand == _T("bus") ) {
                  _SkipArg(aArgs, iIndex, _T("error"));
                  sReason = _T("signal");
               }
               if( sCommand == _T("interrupt") ) {
                  sReason = _T("signal");
               }
               if( sCommand == _T("floating") ) {
                  _SkipArg(aArgs, iIndex, _T("execption"));
                  sReason = _T("signal");
               }
               if( sCommand == _T("segmentation") ) {
                  _SkipArg(aArgs, iIndex, _T("fault"));
                  _SkipArg(aArgs, iIndex, _T("violation"));
                  sReason = _T("signal");
               }
               if( sCommand == _T("signal") ) {
                  CString sName = _GetArg(aArgs, iIndex);
                  CString sMeaning = _GetArg(aArgs, iIndex);
                  sReason = _T("signal-received");
                  sReasonValue.Format(_T("signal-name=\"Signal %s\",signal-meaning=\"%s\","), sName, sMeaning);
               }
               if( sCommandCase.Find(_T("SIG")) == 0 ) {
                  CString sName = sCommandCase;
                  CString sMeaning = _GetArg(aArgs, iIndex);
                  sReason = _T("signal-received");
                  sReasonValue.Format(_T("signal-name=\"Signal %s\",signal-meaning=\"%s\","), sName, sMeaning);
               }
               DBXLOCATION Location;
               while( iIndex < aArgs.GetSize() ) _GetLocationArgs(aArgs, iIndex, Location);
               m_State = DBX_UNKNOWN;
               sMi.Format(_T("232*stopped,reason=\"%s\",%sframe={func=\"%s\",file=\"%s\",addr=\"%s\",line=\"%ld\"}"), 
                  sReason, sReasonValue, Location.sFunction, Location.sFile, Location.sAddress, Location.lLineNum);
               return sMi;
            }
            if( sCommand == _T("running:") 
                || sCommand == _T("starting") ) 
            {
               if( sCommand == _T("starting") && !_SkipArg(aArgs, iIndex, _T("program")) ) break;
               sMi.Format(_T("232^running,file=\"%s\""), _GetArg(aArgs, iIndex));
               return sMi;
            }
            if( sCommand == _T("(process") ) 
            {
               if( !_SkipArg(aArgs, iIndex, _T("id")) ) break;
               sMi.Format(_T("232~\"kernel event for pid=%ld\""), _ttol(_GetArg(aArgs, iIndex)));
               return sMi;
            }
            if( sCommand == _T("execution") ) {
               return _T("232^exit");
            }
         }
         break;
      case DBX_WHERE:
         {
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,stack=[");
            long lLevel = 0;
            DBXLOCATION Location;
            _SkipArg(aArgs, iIndex, _T(">"));
            _SkipArg(aArgs, iIndex, _T("=>"));
            _SkipArg(aArgs, iIndex, _T("Frame"));
            _GetNumericArg(aArgs, iIndex, lLevel);
            if( _SkipArg(aArgs, iIndex, _T("line")) ) Location.lLineNum = _ttol(_GetArg(aArgs, iIndex));
            _SkipArg(aArgs, iIndex, _T("routine"));
            Location.sFunction = _GetArg(aArgs, iIndex).SpanExcluding(_T("([,"));
            if( lLevel == 0 && m_lReturnIndex > 0 ) break;
            if( Location.sFunction.Find(_T("---")) >= 0 ) break;
            if( Location.sFunction == _T("current") ) break;
            while( iIndex < aArgs.GetSize() ) _GetLocationArgs(aArgs, iIndex, Location);
            CString sTemp;
            sTemp.Format(_T("frame={level=\"%ld\",addr=\"%s\",func=\"%s\",file=\"%s\",line=\"%ld\"}]"), 
               lLevel, Location.sAddress, Location.sFunction, Location.sFile, Location.lLineNum);
            _AdjustAnswerList(_T("]"), sTemp);
         }
         break;
      case DBX_STACKARGS:
         {
            long lLevel = 0;
            DBXLOCATION Location;
            _SkipArg(aArgs, iIndex, _T(">"));
            _SkipArg(aArgs, iIndex, _T("=>"));
            _SkipArg(aArgs, iIndex, _T("Frame"));
            _GetNumericArg(aArgs, iIndex, lLevel);
            if( _SkipArg(aArgs, iIndex, _T("line")) ) Location.lLineNum = _ttol(_GetArg(aArgs, iIndex));
            _SkipArg(aArgs, iIndex, _T("routine"));
            Location.sFunction = _GetArg(aArgs, iIndex).SpanExcluding(_T("([,"));
            if( lLevel == 0 && m_lReturnIndex > 0 ) break;
            if( Location.sFunction.Find(_T("---")) >= 0 ) break;
            if( Location.sFunction == _T("current") ) break;
            CString sArgs = sFullArgs.Mid(Location.sFunction.GetLength());
            sArgs.TrimLeft(_T(" (")); sArgs.TrimRight(_T(" )"));
            sMi = _T("232^done,stack-args=[");
            while( !sArgs.IsEmpty() ) {
               CString sName, sValue = sArgs.SpanExcluding(_T(",)"));
               sArgs = sArgs.Mid(sValue.GetLength() + 1);
               int iPos = sValue.Find('=');
               if( iPos > 0 ) { sName = sValue.Left(iPos); sValue = sValue.Mid(iPos + 1); }
               sName.TrimLeft(); sName.TrimRight();
               sValue.TrimLeft(); sValue.TrimRight();
               sValue.Replace(_T("\\"), _T("\\\\"));
               sValue.Replace(_T("\""), _T("\\\""));
               CString sTemp;
               sTemp.Format(_T("{name=\"%s\",type=\"\",value=\"%s\"}"), sName, sValue);
               sMi += sTemp;
               if( sArgs.Left(1) == _T(")") ) break;
            }
            sMi += _T("]");
            return sMi;
         }
         break;
      case DBX_PWD:
         {
            sMi.Format(_T("232^done,cwd=\"%s\""), sFullArgs);
            return sMi;
         }
         break;
      case DBX_DIS:
         {
            CString sCommand = pstrOutput;
            int iPos = sCommand.Find(_T(": "));
            if( iPos < 0 ) break;
            bool bHasFunctionName = false;
            if( sCommand.Find(_T(": "), iPos + 1) > 0 ) iPos = sCommand.Find(_T(": "), iPos + 1), bHasFunctionName = true;
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,asm_insns=[");
            CString sTemp, sAddress, sFunction, sOffset, sDisasm;
            sAddress = sCommand.SpanExcluding(_T(":"));
            if( bHasFunctionName )  {
               sTemp = sCommand.Mid(sAddress.GetLength() + 1);
               sFunction = sTemp.SpanExcluding(_T("+"));
               sOffset = sTemp.Mid(sFunction.GetLength()).SpanExcluding(_T(": "));
            }
            sDisasm = sCommand.Mid(iPos + 2);
            sFunction.TrimLeft(); sFunction.TrimRight();
            sOffset.TrimLeft(_T(" +")); sOffset.TrimRight();
            sDisasm.TrimLeft(); sDisasm.TrimRight();
            sTemp.Format(_T("{address=\"%s\",func-name=\"%s\",offset=\"%s\",inst=\"%s\"}]"),
               sAddress, sFunction, sOffset, sDisasm);
            _AdjustAnswerList(_T("]"), sTemp);
         }
         break;
      case DBX_PRINT:
         {
            if( m_lReturnIndex > 0 ) break;
            LPCTSTR pstr = _tcsstr(pstrOutput, _T(" = "));
            if( pstr == NULL ) pstr = pstrOutput; else pstr += 3, m_lReturnIndex++;
            CString sResult = pstr;
            sResult.Replace(_T("\\"), _T("\\\\"));
            sResult.Replace(_T("\""), _T("\\\""));
            if( sResult.Right(1) == _T("{") ) sResult += _T("...}");
            m_sReturnValue.Format(_T("232^done,value=\"%s\""), sResult);            
         }
         break;
      case DBX_PRINTC:
         {
            if( _tcsstr(pstrOutput, _T("   ")) != pstrOutput ) {
               m_sWatchName = _GetArg(aArgs, iIndex);
               break;
            }
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,numchild=\"0\",children=[");
            CString sName = _GetArg(aArgs, iIndex);
            if( sName.IsEmpty() ) break;
            CString sWatchName;
            sWatchName.Format(_T("quickwatch.%s.%s"), m_sWatchName, sName);
            CString sTemp;
            sTemp.Format(_T("{name=\"%s\",exp=\"%s\",type=\"\",numchild=\"0\"}]"), sWatchName, sName);
            _AdjustAnswerList(_T("]"), sTemp);
         }
         break;
      case DBX_DISPLAY:
         {
            if( _tcsstr(pstrOutput, _T(" = ")) == NULL ) break;
            CString sName = _GetArg(aArgs, iIndex);
            CString sValue = _GetArg(aArgs, iIndex);
            sValue.Replace(_T("\\"), _T("\\\\"));
            sValue.Replace(_T("\""), _T("\\\""));
            sMi.Format(_T("232^done,name=\"%s\",value=\"%s\""), sName, sValue);
            return sMi;
         }
         break;
      case DBX_EXAMINE:
         {
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,addr=\"\",memory=[");
            CString sAddress = _GetArg(aArgs, iIndex);
            CString sTemp;
            sTemp.Format(_T("{addr=\"%s\",data=["), sAddress);
            int iPos = 0;
            while( iIndex < aArgs.GetSize() ) {
               CString sValue = _GetArg(aArgs, iIndex);
               sValue = sValue.SpanIncluding(_T("x0123456789abcdefABCDEF"));
               if( sValue.IsEmpty() ) continue;
               if( !_istdigit(sValue[0]) ) continue;
               if( iPos++ > 0 ) sTemp += ",";
               CString sVal;
               sVal.Format(_T("\"%s\""), sValue);
               sTemp += sVal;
            }
            sTemp += _T("]}]");
            _AdjustAnswerList(_T("]"), sTemp);
         }
      case DBX_DUMP:
         {
            CString sCommand = pstrOutput;
            int iPos = sCommand.Find('=');
            if( iPos < 0 ) break;
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,locals=[");
            CString sName = sCommand.Left(iPos);
            CString sValue = sCommand.Mid(iPos + 1);
            if( sName.IsEmpty() ) break;
            if( sName.FindOneOf(_T("(<;[-)")) >= 0 ) break;
            sName.TrimLeft(); sName.TrimRight();
            sValue.TrimLeft(); sValue.TrimRight();
            sValue.Replace(_T("\\"), _T("\\\\"));
            sValue.Replace(_T("\""), _T("\\\""));
            CString sTemp;
            sTemp.Format(_T("{name=\"%s\",type=\"\",value=\"%s\"}]"), sName, sValue);
            _AdjustAnswerList(_T("]"), sTemp);
         }
         break;
      case DBX_REGNAMES:
         {
            CString sCommand = _GetArg(aArgs, iIndex);
            if( sCommand == _T("current") ) m_sReturnValue = _T("232^done,register-names=[");
            else {
               CString sTemp;
               sTemp.Format(_T("\"%s\"]"), sCommand);
               _AdjustAnswerList(_T("]"), sTemp);
            }
         }
         break;
      case DBX_REGS:
         {
            CString sCommand = _GetArg(aArgs, iIndex);
            if( sCommand == _T("current") ) m_sReturnValue = _T("232^done,register-values=["), m_lReturnIndex = 0;
            else {
               CString sValue = sFullArgs;
               sValue.Replace(_T("\\"), _T("\\\\"));
               sValue.Replace(_T("\""), _T("\\\""));
               sValue.TrimLeft();
               CString sTemp;
               sTemp.Format(_T("{number=\"%ld\",value=\"%s\"}]"), m_lReturnIndex, sValue);
               _AdjustAnswerList(_T("]"), sTemp);
            }
         }
         break;
      case DBX_STATUS:
         {
            long lBreakpointNo = 0;
            if( !_GetNumericArg(aArgs, iIndex, lBreakpointNo) ) break;
            if( !_SkipArg(aArgs, iIndex, _T("stop") )) break;
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,BreakpointTable={nr_rows=\"0\",nr_cols=\"9\",hdr=[{}],body=[");
            DBXLOCATION Location;
            _GetLocationArgs(aArgs, iIndex, Location);
            CString sEnabled = _T("y");
            if( _SkipArg(aArgs, iIndex, _T("-disable")) ) sEnabled = _T("n");
            CString sTemp;
            sTemp.Format(_T("bkpt={number=\"%ld\",type=\"breakpoint\",disp=\"keep\",enabled=\"%s\",addr=\"%s\",func=\"%s\",file=\"%s\",line=\"%ld\",times=\"0\"}]}"), 
               lBreakpointNo, sEnabled, Location.sAddress, Location.sFunction, Location.sFile, Location.lLineNum);
            _AdjustAnswerList(_T("]}"), sTemp);
         }
         break;
      case DBX_STOP:
         {
            long lBreakpointNo = 0;
            if( !_GetNumericArg(aArgs, iIndex, lBreakpointNo) ) break;
            if( !_SkipArg(aArgs, iIndex, _T("stop")) ) break;
            DBXLOCATION Location;
            _GetLocationArgs(aArgs, iIndex, Location);
            sMi.Format(_T("232^done,bkpt={number=\"%ld\",file=\"%s\",line=\"%ld\"}"), lBreakpointNo, Location.sFile, Location.lLineNum);
            return sMi;
         }
         break;
      case DBX_THREADS:
         {
            CString sCommand = _GetArg(aArgs, iIndex);
            if( sCommand.Find('@') < 0 ) break;
            sCommand.TrimLeft(_T("=>o*t@"));
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,thread-ids={");
            CString sTemp;
            sTemp.Format(_T("thread-id=\"%s\"}"), sCommand);
            _AdjustAnswerList(_T("}"), sTemp);
         }
         break;
      case DBX_LWPS:
         {
            CString sCommand = _GetArg(aArgs, iIndex);
            if( sCommand.Find('@') < 0 ) break;
            sCommand.TrimLeft(_T("=>o*l@"));
            if( m_lReturnIndex == 0 ) m_sReturnValue = _T("232^done,thread-ids={");
            CString sTemp;
            sTemp.Format(_T("thread-id=\"%s\"}"), sCommand);
            _AdjustAnswerList(_T("}"), sTemp);
         }
         break;
      case DBX_UNKNOWN:
         {
            if( !m_bSeenPrompt )
            {
               CString sCommand = _GetArg(aArgs, iIndex);
               sCommand.MakeLower();
               if( sCommand == _T("running:") || sCommand == _T("starting") ) 
               {
                  if( sCommand == _T("starting") && !_SkipArg(aArgs, iIndex, _T("program")) ) break;
                  sMi.Format(_T("232^running,file=\"%s\""), _GetArg(aArgs, iIndex));
                  m_State = DBX_RUNNING;
                  return sMi;
               }
               if( sCommand == _T("reading") ) 
               {
                  return _T("set $prompt=\"(dbx) \"");
               }
            }
         }
         break;
      }
   }
   return pstrOutput;
}

// Implementation

void CDbxAdaptor::_AdjustAnswerList(LPCTSTR pstrEnding, CString sNewItem)
{
   // Append an item to the GDB MI list result.
   // Since we already added the close-list marker to the result we need to remove
   // this first, and then append the new item.
   ATLASSERT(m_sReturnValue!=_T("(gdb)"));
   SIZE_T cchEnding = _tcslen(pstrEnding);
   if( m_sReturnValue.Right(cchEnding) == pstrEnding ) {
      m_sReturnValue.Delete(m_sReturnValue.GetLength() - cchEnding, cchEnding);
      m_sReturnValue += _T(",");
   }
   m_sReturnValue += sNewItem;
   m_lReturnIndex++;
}

CString CDbxAdaptor::_GetArg(const CSimpleArray<CString>& aArgs, int& iIndex) const
{
   // Return argument from list; advance argument index
   if( iIndex >= aArgs.GetSize() ) return _T("");
   return aArgs[++iIndex - 1];
}

void CDbxAdaptor::_GetLocationArgs(const CString sCommand, DBXLOCATION& Location) const
{
   // Extract dbx location for the following syntax:
   //   ["<file>":<line>, <address>]
   //   [<function>:<line>, <address>]
   //   <address>
   // Start with the address; we support 0x-hex syntax only
   if( sCommand.Find(_T("0x")) == 0 ) Location.sAddress = sCommand;
   if( sCommand.Find(_T(",0x")) > 0 ) Location.sAddress = sCommand.Mid(sCommand.Find(_T(",0x") + 1));
   if( sCommand.Find(_T(", 0x")) > 0 ) Location.sAddress = sCommand.Mid(sCommand.Find(_T(", 0x") + 2));
   Location.sAddress = Location.sAddress.SpanIncluding(_T("x0123456789abcdefABCDEF:"));
   // Attempt to find the function or file. A file has quotes.
   CString sTemp = sCommand.SpanExcluding(_T(":,]"));
   if( sTemp.IsEmpty() ) return;
   sTemp.TrimLeft(_T(">*["));
   bool bIsFile = (sTemp.Find('.') > 0);
   sTemp.TrimLeft(_T("\""));
   sTemp.TrimRight(_T("\""));
   if( !bIsFile && sCommand.Find(':') < 0 ) return;
   if( bIsFile ) Location.sFile = sTemp; else Location.sFunction = sTemp;
   // Finally, the linenumber may be found on the file/function
   Location.lLineNum = _ttol(sCommand.Mid(sTemp.GetLength() + 1));
}

void CDbxAdaptor::_GetLocationArgs(const CSimpleArray<CString>& aArgs, int& iIndex, DBXLOCATION& Location) const
{
   // Parse dbx location at least with the following syntax:
   //   in <function> at line <line> in <file>
   //   at ["<file>":<line>, <address>]
   //   at pc <address>
   // and all variations thereof.
   if( _SkipArg(aArgs, iIndex, _T("in")) && Location.sFunction.IsEmpty() ) Location.sFunction = _GetArg(aArgs, iIndex);
   _SkipArg(aArgs, iIndex, _T("at"));
   if( _SkipArg(aArgs, iIndex, _T("pc")) ) Location.sAddress = _GetArg(aArgs, iIndex);
   else if( _SkipArg(aArgs, iIndex, _T("line")) ) Location.lLineNum = _ttol(_GetArg(aArgs, iIndex));
   else _GetLocationArgs(_GetArg(aArgs, iIndex), Location);
   bool bInFile = _SkipArg(aArgs, iIndex, _T("in"));
   bInFile |= _SkipArg(aArgs, iIndex, _T("file")); 
   if( bInFile && Location.sFile.IsEmpty() ) Location.sFile = _GetArg(aArgs, iIndex);
}

void CDbxAdaptor::_GetInputFileLineArgs(const CString sArg, CString& sFile, CString& sLineNum) const
{
   sFile = sArg.SpanExcluding(_T(":"));
   sLineNum = sArg.Mid(sFile.GetLength()).SpanIncluding(_T(":0123456789"));
}

bool CDbxAdaptor::_GetNumericArg(const CSimpleArray<CString>& aArgs, int& iIndex, long& lBreakpointNo) const
{
   // Parse numeric values often prefixing dbx lines, in syntax:
   //   [42]
   //   (42)
   if( iIndex >= aArgs.GetSize() ) return false;
   CString sCommand = aArgs[iIndex];
   sCommand.TrimLeft(_T("=>([*"));
   sCommand = sCommand.SpanIncluding(_T("0123456789"));
   if( sCommand.IsEmpty() ) return false;
   lBreakpointNo = _ttol(sCommand);
   iIndex++;
   return true;
}

bool CDbxAdaptor::_SkipArg(const CSimpleArray<CString>& aArgs, int& iIndex, LPCTSTR pstrMatch) const
{
   if( iIndex >= aArgs.GetSize() ) return false;
   if( _tcscmp(aArgs[iIndex], pstrMatch) != 0 ) return false;
   iIndex++;
   return true;
}

/**
 * Split text into a number of arguments.
 * The parser treats the characters '"[]() as quotes and captures all content
 * between these as one argument.
 */
void CDbxAdaptor::_SplitCommand(LPCTSTR pstrInput, CSimpleArray<CString>& aArgs, CString& sFullArgs) const
{
   CString sTerm;
   bool bIncludeQuotes = false;
   TCHAR iQuote = '\0';
   while( *pstrInput != '\0' ) 
   {
      switch( *pstrInput ) {
      case '\'':
      case '\"':
      case '(':
      case ')':
      case '}':
      case '{':
      case '[':
      case ']':
         if( iQuote == '\0' && !sTerm.IsEmpty() ) bIncludeQuotes = true;
         if( iQuote == '\0' ) iQuote = *pstrInput;
         else if( iQuote == *pstrInput ) iQuote = '\0';
         else sTerm += *pstrInput;
         if( iQuote == '(' ) iQuote = ')';
         else if( iQuote == '{' ) iQuote = '}';
         else if( iQuote == '[' ) iQuote = ']';
         if( bIncludeQuotes ) sTerm += *pstrInput;
      	break;
      case '\r':
      case '\n':
         // Ignore newlines...
      	break;
      case ' ':
      case '\t':
         if( iQuote == '\0' ) {
            if( aArgs.GetSize() == 0 ) sFullArgs = pstrInput + 1;
            if( !sTerm.IsEmpty() ) aArgs.Add(sTerm);
            sTerm = _T("");
            bIncludeQuotes = false;
            break;
         }
         // FALL THROUGH...
      default:
         if( iQuote == '\"' && *pstrInput == '\\' ) sTerm += *pstrInput++;
         sTerm += *pstrInput;
      }
      pstrInput++;
   }
   if( !sTerm.IsEmpty() ) aArgs.Add(sTerm);
}
