Scriptname _RSL_BedrollItem extends ObjectReference
{Portable bedroll. Craft _RSL_BedrollItem at a tanning rack; drop it and it
 becomes _RSL_BedrollFurn placed in front of the player (activate to sleep).
 Grab the placed bedroll to pick it back up. Standard drop-to-place idiom
 (OnContainerChanged -> PlaceAtMe + MoveTo); vanilla assets only.}

Event OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)
    Actor p = Game.GetPlayer()
    If akNewContainer || akOldContainer != p
        return          ; only act when dropped from the player into the world
    EndIf

    Furniture fn = _RSL_Forms.BedrollFurn()
    If !fn
        return
    EndIf

    Disable()
    float a = p.GetAngleZ()
    ObjectReference nr = p.PlaceAtMe(fn, 1, false, true)
    nr.SetAngle(0.0, 0.0, a)
    nr.MoveTo(p, Math.Sin(a) * 90.0, Math.Cos(a) * 90.0, 0.0, false)
    nr.SetActorOwner(p.GetActorBase())
    nr.Enable()
    Delete()
EndEvent
