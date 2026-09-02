Scriptname Debug Hidden
{Stub. Extends the stripped Nemesis version (Trace() only). The full set is
 needed because MiscUtil.psc from PapyrusUtil calls Debug.TraceStack().
 See README.txt.}

Function Trace(string asTextToPrint, int aiSeverity = 0) native global
Function TraceStack(string asTextToPrint = "", int aiSeverity = 0) native global
Function TraceConditional(string asTextToPrint, bool abCondition) native global
Function TraceAndBox(string asTextToPrint, int aiSeverity = 0) native global
Function TraceUser(string asUserLogName, string asTextToPrint, int aiSeverity = 0) native global
bool Function OpenUserLog(string asLogName) native global

Function Notification(string asNotificationText) native global
Function MessageBox(string asMessageBoxText) native global

Function SendAnimationEvent(ObjectReference arRef, string asEventName) native global
Function SetGodMode(bool abGodMode) native global
Function QuitGame() native global
