Scriptname _RSL_Boot extends Quest
{Loader. Hands the player the monitor ability (_RSL_AbMonitor) whose constant
 effect carries all mod logic (_RSL_Controller). Start Game Enabled, invisible.

 The re-check tick is a safety net: if the ability were ever lost (foreign
 RemoveAllSpells, a corrupt save) it is re-added. Quest has no OnPlayerLoadGame,
 hence the game-time tick rather than a load hook.}

; Interval to re-check the monitor ability is still on the player, in game hours.
float Property RecheckHours = 24.0 Auto Hidden

Event OnInit()
    Attach()
    RegisterForSingleUpdateGameTime(RecheckHours)
EndEvent

Event OnUpdateGameTime()
    Attach()
    RegisterForSingleUpdateGameTime(RecheckHours)
EndEvent

Function Attach()
    Spell monitor = _RSL_Forms.AbMonitor()
    If !monitor
        _RSL_Log.W("Boot: AbMonitor is None - plugin inactive or _RSL_Forms stale")
        return
    EndIf
    Actor pl = Game.GetPlayer()
    If !pl.HasSpell(monitor)
        pl.AddSpell(monitor, false)
    EndIf
EndFunction
