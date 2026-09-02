Scriptname _RSL_Disease Hidden
{Progressive-stage disease engine. Stateless helpers; the controller owns
 per-disease policy (spells, gates, timers) and calls these once per id per tick.
 Shared by the from-scratch diseases, food poisoning, hypothermia and the RFAB
 wrapper diseases.

 Player StorageUtil state, per id (key prefix "_RSL_Dz_<id>_"):
   Stage  int    0..3 (0 = not sick)
   Time   float  game days, last stage change
   Roll   float  game days, last contract roll
   Prog   float  hidden -100..100 progression accumulator (P)
   Cures  int    pending counted Cure-Disease hits
   S3     float  seconds spent in hypothermia stage 3
   Clean  int    guard against instant re-contract after a heal}

import StorageUtil

int Function GetStage(Actor p, string id) global
    return StorageUtil.GetIntValue(p, "_RSL_Dz_" + id + "_Stage", 0)
EndFunction

; Contract roll gate: true at most once per `perHours` game-hours, and stamps on
; success so the caller does `If RollDue(...) && RandomFloat(0,100) < chance`.
bool Function RollDue(Actor p, string id, float perHours) global
    float now  = Utility.GetCurrentGameTime()
    float last = StorageUtil.GetFloatValue(p, "_RSL_Dz_" + id + "_Roll", 0.0)
    If now - last >= perHours / 24.0
        StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Roll", now)
        return true
    EndIf
    return false
EndFunction

; Move to stage s. prev/next are our stage spells (None where we add nothing,
; e.g. an RFAB wrapper's stage 1). magPct/baseMag are unused (kept for the
; call-site signature) - stage magnitudes are baked into the SPEL by the generator.
Function SetStage(Actor p, string id, int s, Spell prev, Spell next, float magPct, float baseMag) global
    If prev
        p.RemoveSpell(prev)
    EndIf
    If next
        p.AddSpell(next, false)
    EndIf
    StorageUtil.SetIntValue(p, "_RSL_Dz_" + id + "_Stage", s)
    StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Time", Utility.GetCurrentGameTime())
    If s <= 0
        StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Prog", 0.0)
    EndIf
    _RSL_Log.W("Dz " + id + ": stage -> " + s)
EndFunction

; Full reset: strip our stage spells, zero the counters. Pass None for unused slots.
Function ClearStages(Actor p, string id, Spell s1, Spell s2, Spell s3) global
    If s1
        p.RemoveSpell(s1)
    EndIf
    If s2
        p.RemoveSpell(s2)
    EndIf
    If s3
        p.RemoveSpell(s3)
    EndIf
    float now = Utility.GetCurrentGameTime()
    StorageUtil.SetIntValue(p, "_RSL_Dz_" + id + "_Stage", 0)
    StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Time", now)
    StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Roll", now)
    StorageUtil.UnsetIntValue(p, "_RSL_Dz_" + id + "_Cures")
    StorageUtil.UnsetFloatValue(p, "_RSL_Dz_" + id + "_Prog")
    StorageUtil.UnsetFloatValue(p, "_RSL_Dz_" + id + "_S3")
EndFunction

; One counted Cure-Disease hit = one stage back. Returns the pending count and
; clears it; the caller applies `stage - n` (floored at 0).
int Function TakeCures(Actor p, string id) global
    int n = StorageUtil.GetIntValue(p, "_RSL_Dz_" + id + "_Cures", 0)
    If n > 0
        StorageUtil.SetIntValue(p, "_RSL_Dz_" + id + "_Cures", 0)
    EndIf
    return n
EndFunction

Function AddCure(Actor p, string id) global
    StorageUtil.SetIntValue(p, "_RSL_Dz_" + id + "_Cures", \
        StorageUtil.GetIntValue(p, "_RSL_Dz_" + id + "_Cures", 0) + 1)
EndFunction

Function ResetP(Actor p, string id) global
    StorageUtil.SetFloatValue(p, "_RSL_Dz_" + id + "_Prog", 0.0)
EndFunction

Function HalveP(Actor p, string id) global
    string k = "_RSL_Dz_" + id + "_Prog"
    StorageUtil.SetFloatValue(p, k, StorageUtil.GetFloatValue(p, k, 0.0) * 0.5)
EndFunction

; Drive the hidden accumulator P over one tick and roll for stage steps. Shared
; by every disease except hypothermia.
;   bad = true  -> a survival axis is past its line -> P falls (disease worsens)
;   bad = false -> every axis is fine               -> P rises (disease heals)
; DiseaseResist tilts both drift rates. Continuous roll: RandomFloat(0,100) <
; |P|*dt. A big dt is split into <=0.25h sub-steps (one roll each). On a proc P
; resets to 0. Returns the net stage delta CLAMPED to +/-1 so one call never
; skips a stage - the frequent tick catches up step by step.
int Function StepP(Actor p, string id, bool bad, float dtHours, float diseaseResist, float worsenHours, float recoverHours) global
    string k = "_RSL_Dz_" + id + "_Prog"
    float prog = StorageUtil.GetFloatValue(p, k, 0.0)

    float drift = (100.0 / recoverHours) * (1.0 + diseaseResist * 0.01)
    If bad
        drift = -(100.0 / worsenHours) * (1.0 - diseaseResist * 0.01)
        If drift > 0.0
            drift = 0.0
        EndIf
    EndIf

    int iters = 1
    If dtHours > 0.25
        iters = (dtHours / 0.25) as int
        If iters > 120
            iters = 120
        EndIf
    EndIf
    float sub = dtHours / iters

    int net = 0
    int i = 0
    While i < iters
        prog += drift * sub
        If prog > 100.0
            prog = 100.0
        ElseIf prog < -100.0
            prog = -100.0
        EndIf
        float absP = prog
        If absP < 0.0
            absP = -absP
        EndIf
        If Utility.RandomFloat(0.0, 100.0) < absP * sub
            If prog > 0.0
                net += 1
            Else
                net -= 1
            EndIf
            prog = 0.0
        EndIf
        i += 1
    EndWhile

    StorageUtil.SetFloatValue(p, k, prog)
    If net > 1
        net = 1
    ElseIf net < -1
        net = -1
    EndIf
    return net
EndFunction

; Deterministic threshold-crossing accumulator (hypothermia, not a roll):
; P += drift*dt; a full +100 -> +1 stage, a full -100 -> -1 stage, P resets to 0.
; `drift` is signed %/game-hour, set by the caller from the cold thresholds.
int Function StepLinear(Actor p, string id, float drift, float dtHours) global
    string k = "_RSL_Dz_" + id + "_Prog"
    float prog = StorageUtil.GetFloatValue(p, k, 0.0) + drift * dtHours
    int step = 0
    If prog >= 100.0
        step = 1
        prog = 0.0
    ElseIf prog <= -100.0
        step = -1
        prog = 0.0
    EndIf
    StorageUtil.SetFloatValue(p, k, prog)
    return step
EndFunction
