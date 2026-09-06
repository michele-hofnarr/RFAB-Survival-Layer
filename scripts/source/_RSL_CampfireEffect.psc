Scriptname _RSL_CampfireEffect extends ActiveMagicEffect
{Feature 5/6: the "Развести костёр" lesser power (_RSL_PowerCampfire).

 Perk-gated (RFAB "Основы выживания"), real-time cooldown, consumes
 _RSL_CampfireFuel x Firewood01. Places a burning campfire in front of the
 player - plus a cooking spit + pot when the player also has RFAB "Кулинар".
 Every placed ref is snapped to the nearest navmesh so it sits on the ground.

 Refs and the burn deadline are tracked in StorageUtil (_RSL_CampRef /
 _RSL_CampSpitRef / _RSL_CampCookRef / _RSL_CampUntil); the deadline and all
 cleanup live in _RSL_Controller (RemovePrevCampfire + the tick / teardown) so
 re-light, expiry, load and teardown share one path.

 The campfire is a vanilla Campfire01Burning whose EditorID matches the
 fire-source scan, so Severity() already treats it as a heat source.}

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Actor p = Game.GetPlayer()

    GlobalVariable ge = _RSL_Forms.CampfireEnabled()
    If ge && ge.GetValue() < 0.5
        return
    EndIf

    ; cooldown - also rate-limits the "no fuel/perk" notifications.
    ; GetCurrentRealTime() resets to ~0 each launch; a persisted stamp from a
    ; prior session reads larger than now -> ignore it (else stuck "on cooldown").
    float nowRT = Utility.GetCurrentRealTime()
    float lastRT = StorageUtil.GetFloatValue(p, "_RSL_CampCastRT", -999.0)
    If lastRT > nowRT
        lastRT = -999.0
    EndIf
    _RSL_Log.W("CampfireEffect OnEffectStart: nowRT=" + nowRT + " lastRT=" + lastRT + " cd=" + Cooldown())
    If nowRT - lastRT < Cooldown()
        _RSL_Log.W("CampfireEffect: on cooldown, ignored")
        return
    EndIf
    StorageUtil.SetFloatValue(p, "_RSL_CampCastRT", nowRT)

    Perk basics = _RSL_Forms.PerkSurvivalBasics()
    If !basics || !p.HasPerk(basics)
        ShowMsg(_RSL_Forms.MsgCampNoPerk())
        return
    EndIf

    Form wood = _RSL_Forms.Firewood()
    int need = FuelNeeded()
    If !wood || p.GetItemCount(wood) < need
        ShowMsg(_RSL_Forms.MsgCampNoFuel())
        return
    EndIf

    Form base = _RSL_Forms.BaseCampfire()
    If !base
        _RSL_Log.W("CampfireEffect: BaseCampfire is None - _RSL_Forms stale")
        return
    EndIf

    p.RemoveItem(wood, need)

    ; grab the previous set BEFORE placing the new one - it is cleaned up only
    ; AFTER the new campfire is up, so a cleanup stall can never leave the player
    ; with no campfire and spent firewood.
    ObjectReference oldFire = StorageUtil.GetFormValue(p, "_RSL_CampRef") as ObjectReference
    ObjectReference oldSpit = StorageUtil.GetFormValue(p, "_RSL_CampSpitRef") as ObjectReference
    ObjectReference oldPot  = StorageUtil.GetFormValue(p, "_RSL_CampCookRef") as ObjectReference

    float a = p.GetAngleZ()
    float dist = 110.0
    float px = p.GetPositionX() + Math.Sin(a) * dist
    float py = p.GetPositionY() + Math.Cos(a) * dist
    float pz = p.GetPositionZ()

    ObjectReference r = PlaceGrounded(p, base, px, py, pz, a)
    float fx = r.GetPositionX()
    float fy = r.GetPositionY()
    float fz = r.GetPositionZ()

    ObjectReference rs = None
    ObjectReference rp = None
    Perk chef = _RSL_Forms.PerkCook()
    Form spitBase = _RSL_Forms.BaseCookSpit()
    Form potBase  = _RSL_Forms.BaseCookPot()
    If chef && p.HasPerk(chef) && spitBase && potBase
        rs = PlaceGrounded(p, spitBase, fx, fy, fz, a)
        ; pot hangs on the spit - NOT grounded. Hardcoded offset compensates the
        ; off-centre CraftingCookingPotSm mesh pivot. fwd/side in the facing frame.
        float fwd  = -47.0
        float side = 0.0
        float up   = -13.3
        rp = p.PlaceAtMe(potBase, 1, false, true)
        rp.SetPosition(fx + Math.Sin(a) * fwd + Math.Cos(a) * side, \
                       fy + Math.Cos(a) * fwd - Math.Sin(a) * side, \
                       fz + up)
        rp.SetAngle(0.0, 0.0, a)
        rp.Enable()
    EndIf

    StorageUtil.SetFormValue(p, "_RSL_CampRef", r)
    StorageUtil.SetFormValue(p, "_RSL_CampSpitRef", rs)
    StorageUtil.SetFormValue(p, "_RSL_CampCookRef", rp)
    StorageUtil.SetFloatValue(p, "_RSL_CampUntil", \
        Utility.GetCurrentGameTime() + BurnHours() / 24.0)

    ShowMsg(_RSL_Forms.MsgCampLit())
    _RSL_Log.W("CampfireEffect: lit " + r + " until day " \
        + StorageUtil.GetFloatValue(p, "_RSL_CampUntil", 0.0))

    ; new set is up and tracked - now retire the old set
    KillRef(oldFire)
    KillRef(oldSpit)
    KillRef(oldPot)
EndEvent

Function KillRef(ObjectReference r)
    If r
        r.DisableNoWait()
        r.Delete()
    EndIf
EndFunction

; Place disabled at (x,y,z), snap to the nearest navmesh (drops it onto the
; ground), then enable. Returns the ref.
ObjectReference Function PlaceGrounded(Actor p, Form base, float x, float y, float z, float angZ)
    ObjectReference o = p.PlaceAtMe(base, 1, false, true)
    o.SetPosition(x, y, z)
    o.SetAngle(0.0, 0.0, angZ)
    PO3_SKSEFunctions.MoveToNearestNavmeshLocation(o)
    o.Enable()
    return o
EndFunction

float Function Cooldown()
    GlobalVariable g = _RSL_Forms.CampfireCooldown()
    If g && g.GetValue() >= 0.0
        return g.GetValue()
    EndIf
    return 5.0
EndFunction

int Function FuelNeeded()
    GlobalVariable g = _RSL_Forms.CampfireFuel()
    int n = 1
    If g
        n = g.GetValue() as int
    EndIf
    If n < 1
        n = 1
    EndIf
    return n
EndFunction

float Function BurnHours()
    GlobalVariable g = _RSL_Forms.CampfireBurnHours()
    If g && g.GetValue() > 0.0
        return g.GetValue()
    EndIf
    return 4.0
EndFunction

Function ShowMsg(Message m)
    If m
        m.Show()
    EndIf
EndFunction
