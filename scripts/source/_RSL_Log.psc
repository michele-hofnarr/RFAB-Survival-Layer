Scriptname _RSL_Log Hidden
{Mod's own debug log, written via PapyrusUtil (the engine's Papyrus log stays
 off in this pack). Path "Data/RSL_debug.log" is caught by MO2's VFS and lands
 in MO2\overwrite\RSL_debug.log.

 Gated by the int flag "_RSL_DbgLog" (StorageUtil, None target) which the
 controller mirrors from the "Отладочный лог" MCM toggle each tick. Off = no
 file writes at all.}

Function W(string msg) global
    If StorageUtil.GetIntValue(None, "_RSL_DbgLog", 0) > 0
        MiscUtil.WriteToFile("Data/RSL_debug.log", msg + "\n", true, true)
    EndIf
EndFunction
