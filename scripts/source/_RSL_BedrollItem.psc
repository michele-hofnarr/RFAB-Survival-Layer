Scriptname _RSL_BedrollItem extends ObjectReference
{Portable bedroll. Craft _RSL_BedrollItem at a tanning rack; drop it and it
 becomes _RSL_BedrollFurn placed in front of the player (activate to sleep).
 Grab the placed bedroll to pick it back up. Standard drop-to-place idiom
 (OnContainerChanged -> PlaceAtMe + MoveTo); vanilla assets only.

 With BOTH RFAB survival perks a tent goes up over it - see PitchTent.}

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
    PitchTent(p, nr, a)
    Delete()
EndEvent

; "Основы выживания" + "Акклиматизация" -> the bedroll comes with a tent, and
; sleeping under it keeps the cold bar off the hypothermia range
; (_RSL_Controller.SleepingUnderOwnTent / SleptColdCap).
;
; The tent ref is remembered on the bedroll so _RSL_BedrollFurn.OnGrab can take
; it away again, and is marked "_RSL_OwnTent" so the controller can tell it from
; the identical NorTentSmall standing in every bandit camp in Skyrim.
;
; fwd/side/up are placement tuning, same as the cook pot in _RSL_CampfireEffect:
; the mesh pivot is not where the bedroll lies. Adjust here if it sits off.
Function PitchTent(Actor p, ObjectReference bedroll, float angZ)
    Perk basics = _RSL_Forms.PerkSurvivalBasics()
    Perk acclim = _RSL_Forms.PerkAcclimatization()
    If !basics || !acclim || !p.HasPerk(basics) || !p.HasPerk(acclim)
        return
    EndIf

    Form tentBase = _RSL_Forms.BaseTent()
    If !tentBase
        return
    EndIf

    float fwd  = 0.0
    float side = 0.0
    float up   = 0.0

    ObjectReference t = p.PlaceAtMe(tentBase, 1, false, true)
    t.SetAngle(0.0, 0.0, angZ)
    t.MoveTo(bedroll, \
        Math.Sin(angZ) * fwd + Math.Cos(angZ) * side, \
        Math.Cos(angZ) * fwd - Math.Sin(angZ) * side, up, false)
    t.SetActorOwner(p.GetActorBase())
    t.Enable()

    StorageUtil.SetIntValue(t, "_RSL_OwnTent", 1)
    StorageUtil.SetFormValue(bedroll, "_RSL_TentRef", t)
    _RSL_Log.W("bedroll: tent pitched " + t)
EndFunction
