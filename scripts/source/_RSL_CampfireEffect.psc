Scriptname _RSL_CampfireEffect extends ActiveMagicEffect
{Feature 5/6: the "Развести костёр" lesser power (_RSL_PowerCampfire).

 The work is in the global LightCampfire() so it can be driven two ways:
   - this MGEF's OnEffectStart (the clean path), and
   - _RSL_Controller.OnMagicEffectApply as a fallback (that hook is known to
     fire; a scripted VMAD on a cast power's MGEF is not always reliable).
 The real-time cooldown stamp de-dupes the two.

 Perk-gated (RFAB "Основы выживания"), consumes _RSL_CampfireFuel x Firewood01.
 Places _RSL_CampfireLit (an ACTI wearing the Campfire01Burning model, carrying
 _RSL_CampfirePlaced for activate-to-extinguish) in front of the player - plus a
 cook spit + pot with RFAB "Кулинар". Refs and the burn deadline live in
 StorageUtil; cleanup / expiry are in _RSL_Controller.}

Event OnEffectStart(Actor akTarget, Actor akCaster)
    LightCampfire(Game.GetPlayer())
    Dispel()          ; Constant Effect - remove ourselves so this is a one-shot
EndEvent

Function LightCampfire(Actor p) global
    If !p
        return
    EndIf
    GlobalVariable ge = _RSL_Forms.CampfireEnabled()
    If ge && ge.GetValue() < 0.5
        return
    EndIf

    ; cooldown - also de-dupes OnEffectStart vs the controller fallback, and
    ; rate-limits the "no fuel/perk" notifications. GetCurrentRealTime() resets
    ; to ~0 each launch; a stamp from a prior session reads larger than now.
    float nowRT = Utility.GetCurrentRealTime()
    float lastRT = StorageUtil.GetFloatValue(p, "_RSL_CampCastRT", -999.0)
    If lastRT > nowRT
        lastRT = -999.0
    EndIf
    If nowRT - lastRT < Cooldown()
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

    Form base = _RSL_Forms.CampfireLit()
    If !base
        base = _RSL_Forms.BaseCampfire()   ; fallback if the plugin lacks _RSL_CampfireLit
    EndIf
    If !base
        _RSL_Log.W("LightCampfire: no campfire base - _RSL_Forms stale")
        return
    EndIf

    p.RemoveItem(wood, need)

    ; grab the previous set BEFORE placing the new one - it is cleaned up only
    ; AFTER the new campfire is up (see _RSL_Controller.CampfireGC).
    ObjectReference oldFire = StorageUtil.GetFormValue(p, "_RSL_CampRef") as ObjectReference
    ObjectReference oldSpit = StorageUtil.GetFormValue(p, "_RSL_CampSpitRef") as ObjectReference
    ObjectReference oldPot  = StorageUtil.GetFormValue(p, "_RSL_CampCookRef") as ObjectReference

    float a = p.GetAngleZ()
    ObjectReference r = PlaceRel(p, base, p, Math.Sin(a) * 110.0, Math.Cos(a) * 110.0, 0.0, a)

    ObjectReference rs = None
    ObjectReference rp = None
    Perk chef = _RSL_Forms.PerkCook()
    Form spitBase = _RSL_Forms.BaseCookSpit()
    Form potBase  = _RSL_Forms.BaseCookPot()
    If chef && p.HasPerk(chef) && spitBase && potBase
        rs = PlaceRel(p, spitBase, r, 0.0, 0.0, 0.0, a)
        float fwd  = -47.0
        float side = 0.0
        float up   = -13.3
        rp = PlaceRel(p, potBase, r, \
            Math.Sin(a) * fwd + Math.Cos(a) * side, \
            Math.Cos(a) * fwd - Math.Sin(a) * side, up, a)
    EndIf

    StorageUtil.SetFormValue(p, "_RSL_CampRef", r)
    StorageUtil.SetFormValue(p, "_RSL_CampSpitRef", rs)
    StorageUtil.SetFormValue(p, "_RSL_CampCookRef", rp)
    StorageUtil.SetFloatValue(p, "_RSL_CampUntil", \
        Utility.GetCurrentGameTime() + BurnHours() / 24.0)

    ShowMsg(_RSL_Forms.MsgCampLit())
    _RSL_Log.W("LightCampfire: lit " + r + " until day " \
        + StorageUtil.GetFloatValue(p, "_RSL_CampUntil", 0.0))

    If oldFire
        StorageUtil.FormListAdd(p, "_RSL_CampGC", oldFire, false)
    EndIf
    If oldSpit
        StorageUtil.FormListAdd(p, "_RSL_CampGC", oldSpit, false)
    EndIf
    If oldPot
        StorageUtil.FormListAdd(p, "_RSL_CampGC", oldPot, false)
    EndIf
EndFunction

; Place (disabled), MoveTo anchor + world offset, set heading, player-own, enable.
ObjectReference Function PlaceRel(Actor p, Form base, ObjectReference anchor, float ox, float oy, float oz, float angZ) global
    ObjectReference o = p.PlaceAtMe(base, 1, false, true)
    o.SetAngle(0.0, 0.0, angZ)
    o.MoveTo(anchor, ox, oy, oz, false)
    o.SetActorOwner(p.GetActorBase())
    o.Enable()
    return o
EndFunction

float Function Cooldown() global
    GlobalVariable g = _RSL_Forms.CampfireCooldown()
    If g && g.GetValue() >= 0.0
        return g.GetValue()
    EndIf
    return 5.0
EndFunction

int Function FuelNeeded() global
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

float Function BurnHours() global
    GlobalVariable g = _RSL_Forms.CampfireBurnHours()
    If g && g.GetValue() > 0.0
        return g.GetValue()
    EndIf
    return 4.0
EndFunction

Function ShowMsg(Message m) global
    If m
        m.Show()
    EndIf
EndFunction
