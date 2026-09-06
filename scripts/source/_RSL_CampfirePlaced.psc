Scriptname _RSL_CampfirePlaced extends ObjectReference
{On _RSL_CampfireLit (the ACTI the player lights with the power). Activate it to
 put the whole set (fire + spit + pot) out. Firewood is NOT refunded.}

Event OnActivate(ObjectReference akActionRef)
    If akActionRef != Game.GetPlayer()
        return
    EndIf
    ; The cook pot sits right on top of the fire - it is easy to hit the fire by
    ; mistake, so confirm before putting it out. Button 0 = "Да", 1 = "Нет".
    Message ask = _RSL_Forms.MsgCampConfirm()
    If ask && ask.Show() != 0
        return
    EndIf
    Message m = _RSL_Forms.MsgCampOut()
    If m
        m.Show()
    EndIf
    ; RemovePrevCampfire disables+deletes _RSL_CampRef / _RSL_CampSpitRef /
    ; _RSL_CampCookRef (this ref is _RSL_CampRef) and clears the tracking keys.
    _RSL_Controller.RemovePrevCampfire(Game.GetPlayer())
EndEvent
