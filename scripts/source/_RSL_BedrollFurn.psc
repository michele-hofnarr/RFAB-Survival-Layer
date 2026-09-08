Scriptname _RSL_BedrollFurn extends ObjectReference
{The placed portable bedroll. Activate to sleep (vanilla furniture behaviour).
 Grab it (grab key) to pick it back up as _RSL_BedrollItem. The tent that came
 with it, if any, goes with it.}

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

    ; _RSL_BedrollItem.PitchTent stored it here. Nothing else refers to the
    ; tent, so it has to go now or it stands there forever.
    ObjectReference tent = StorageUtil.GetFormValue(self, "_RSL_TentRef") as ObjectReference
    If tent
        StorageUtil.UnsetIntValue(tent, "_RSL_OwnTent")
        tent.Disable()
        tent.Delete()
    EndIf
    StorageUtil.UnsetFormValue(self, "_RSL_TentRef")

    Disable()
    Delete()
EndEvent
