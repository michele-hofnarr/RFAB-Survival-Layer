Scriptname _RSL_BedrollFurn extends ObjectReference
{The placed portable bedroll. Activate to sleep (vanilla furniture behaviour).
 Grab it (grab key) to pick it back up as _RSL_BedrollItem.}

bool grabbed = false

Event OnGrab()
    If grabbed
        return
    EndIf
    grabbed = true
    Form item = _RSL_Forms.BedrollItem()
    If item
        Game.GetPlayer().AddItem(item, 1)
    EndIf
    Disable()
    Delete()
EndEvent
