Scriptname _RSL_Controller extends ActiveMagicEffect
{RFAB Survival Layer controller. Three axes: SLEEP, HUNGER, COLD.

 Lives on the constant effect of ability _RSL_AbMonitor, handed out by the
 loader quest _RSL_Boot. ActiveMagicEffect (not ReferenceAlias): it gives the
 events needed (OnObjectEquipped, OnSleepStart/Stop, OnPlayerLoadGame,
 OnVampirismStateChanged) without a nested VMAD\Aliases.

 Forms come from the generated _RSL_Forms.psc, not VMAD properties, so VMAD
 stays trivial and formIDs cannot drift from the plugin. State is in
 StorageUtil, settings in GLOB (written by MCM Helper). See README.md.}

; StorageUtil keys. Shared prefix so they wipe cleanly on mod removal.

string  K_SLEEP     = "_RSL_SleepCounter"      ; hours without sleep
string  K_HUNGER    = "_RSL_HungerCounter"     ; hours without food
string  K_COLD      = "_RSL_ColdExposure"      ; 0..100
string  K_LASTTIME  = "_RSL_LastGameTime"      ; game days, for the delta
string  K_WETUNTIL  = "_RSL_WetUntil"          ; game days, wet until
string  K_SLEEPING  = "_RSL_Sleeping"          ; 1 while sleeping
string  K_SLEEPFROM = "_RSL_SleepStartedAt"    ; game days
string  K_WASUNDEAD = "_RSL_WasUndead"         ; to catch the transition
string  K_TIER_SL   = "_RSL_TierSleep"
string  K_TIER_HU   = "_RSL_TierHunger"
string  K_TIER_CO   = "_RSL_TierCold"
string  K_HYWAIT    = "_RSL_HypoWaitBlocked"   ; 1 while SetInChargen blocks rest
string  K_ELEMACC   = "_RSL_ElemAccum"         ; signed cold nudge from frost/fire hits, pending fold
string  K_ELEMT     = "_RSL_ElemLastT"         ; real seconds, last accepted frost/fire hit
string  K_ELPACC    = "_RSL_Dz_EL_PAcc"        ; signed elemental-lesion P delta (hits -, bandage +), pending fold

; Form cache. Filled once so the tick does not call GetFormFromFile.

Actor    pl
Spell    abSleep
Spell    abHunger
Spell    abCold
Spell    abBonusWarm        ; full-bar bonus: cold safe  -> +HealRateMult
Spell    abBonusRest        ; full-bar bonus: sleep safe -> +MagickaRateMult
Spell    abBonusFed         ; full-bar bonus: hunger safe -> +StaminaRateMult
FormList fireSources
FormList coldInteriors
Keyword  kwUndead
Keyword  kwMagicDamageFrost
Keyword  kwMagicDamageFire
Keyword  kwMagicDamageShock

Spell    sCC1
Spell    sCC2
Spell    sCC3
Message  msgCC1
Message  msgCC2
Message  msgCC3
Message  msgCCCured

; From-scratch diseases, parallel arrays. 0=BrownRot 1=Gutworm 2=Greenspore
; (contract via OnHit), 3=FoodPoison (contract via OnObjectEquipped). All four
; progress/heal on the same _RSL_Disease engine.
string[]  hdId
Spell[]   hdS1
Spell[]   hdS2
Spell[]   hdS3
Message[] hdM1
Message[] hdM2
Message[] hdM3
Message[] hdMC
Race     raceDraugr
Race     raceSlaughterfish
Keyword  kwTroll
Keyword  kwRawFood
Keyword  kwStrongStomach
Keyword  kwSpecialFood
Keyword  kwSpecialDrink

; Wrappers over RFAB's own diseases (id AT/RJ/WB/RA/BF/BRR + DR). Stage 1 =
; rdBase[i] (RFAB's own RFAB_Disease_X, untouched); stages 2/3 = rd2/rd3 (our
; 1:1 copies, only renamed). Same _RSL_Disease P engine as the others.
string[]      rdId
Spell[]       rdBase
MagicEffect[] rdMark
Spell[]       rd2
Spell[]       rd3
Message[]     rdM2
Message[]     rdM3
Message[]     rdMC
GlobalVariable gRfabDzEnabled

; hypothermia (id "HY") - an Ability, not a disease
Spell    sHY1
Spell    sHY2
Spell    sHY3
Message  msgHY1
Message  msgHY2
Message  msgHY3
Message  msgHYCured
Message  msgHYNoRest

; elemental lesions (id "EL") - a Disease-type SPEL with a bespoke P model
; (AdvanceElemLesion). Worsens from cold >= threshold and from frost/fire/shock
; hits; heals only when every axis is clear; RFAB_Bandage nudges P up.
Spell    sEL1
Spell    sEL2
Spell    sEL3
Message  msgEL1
Message  msgEL2
Message  msgEL3
Message  msgELCured

GlobalVariable gModEnabled
GlobalVariable gSleepGrace
GlobalVariable gSleepMax
GlobalVariable gSleepRestorePerHour
GlobalVariable gSleepMinHours
GlobalVariable gCombatFatigueMult
GlobalVariable gHungerGrace
GlobalVariable gHungerMax
GlobalVariable gHungerFoodPct
GlobalVariable gHungerSpecialFoodPct
; Cold: multiplicative model (see README.md)
GlobalVariable gRegionWinterhold
GlobalVariable gRegionPale
GlobalVariable gRegionEastmarch
GlobalVariable gRegionReach
GlobalVariable gRegionHjaalmarch
GlobalVariable gRegionHaafingar
GlobalVariable gRegionWhiterun
GlobalVariable gRegionFalkreath
GlobalVariable gRegionRift
GlobalVariable gRegionDefault
GlobalVariable gRegionSnowFloor
GlobalVariable gWeatherClear
GlobalVariable gWeatherCloudy
GlobalVariable gWeatherRain
GlobalVariable gWeatherSnow
GlobalVariable gNightMult
GlobalVariable gSwimMult
GlobalVariable gRegionAltitude
GlobalVariable gFireMult
GlobalVariable gSevInterior
GlobalVariable gSevColdInterior
GlobalVariable gAltitudeLow
GlobalVariable gAltitudeHigh
GlobalVariable gFireRadius
GlobalVariable gColdRate
GlobalVariable gWarmthPerSlot
GlobalVariable gResistWeight
GlobalVariable gDryMinutes
GlobalVariable gFrostHitCold
GlobalVariable gFireHitWarm
GlobalVariable gElemLesionEnabled
GlobalVariable gElemLesionColdThr
GlobalVariable gElemLesionHypoChance
GlobalVariable gElemLesionHitP
GlobalVariable gElemLesionContractP
GlobalVariable gElemLesionBandageP
GlobalVariable gPenaltyPrimary
GlobalVariable gPenaltyCross
GlobalVariable gPenaltySpeed
GlobalVariable gSpeedCap            ; hard total cap on the slow
GlobalVariable gPenaltyCap
GlobalVariable gTierStep
GlobalVariable gPollInterval
GlobalVariable gDebugLog

GlobalVariable gColdGrace           ; cold below this = no penalty
GlobalVariable gWarmupMult          ; warm-up speed multiplier

; diseases
GlobalVariable gDiseaseEnabled
GlobalVariable gDiseaseProgressHours
GlobalVariable gDiseaseDecayHours
GlobalVariable gDiseaseHitChance
GlobalVariable gFoodPoisonChance
GlobalVariable gHypEnabled
GlobalVariable gHypThreshold
GlobalVariable gHypRecoverThr
GlobalVariable gHypWorsenHours
GlobalVariable gHypRecoverHours
GlobalVariable gHypDrainPerSec
GlobalVariable gHypDrainRamp
GlobalVariable gColdColdThreshold
GlobalVariable gColdColdChanceMin
GlobalVariable gColdColdChanceMax
GlobalVariable gColdColdChanceMaxAt

; HUD widget
GlobalVariable gHudWidget
GlobalVariable gHudColor
GlobalVariable gHudWidgetAutoHide
GlobalVariable gHudWidgetX
GlobalVariable gHudWidgetY
GlobalVariable gHudWidgetScale
GlobalVariable gHudWidgetAlpha
GlobalVariable gHudWidgetHAnchor
GlobalVariable gHudWidgetVAnchor

; full-bar regen bonuses
GlobalVariable gBonusEnabled
GlobalVariable gBonusRegenPct
GlobalVariable gBonusThresholdPct

bool ready = false

; --- lifecycle -------------------------------------------------------------

Event OnEffectStart(Actor akTarget, Actor akCaster)
    _RSL_Log.W("Controller OnEffectStart")
    Bind()
    MigrateSettings()
    SanitizeAbilities()
    RegisterForSleep()
    Touch()
    PokeMCM()
    KickWidget()
    Schedule()
EndEvent

Event OnPlayerLoadGame()
    Bind()
    MigrateSettings()
    SanitizeAbilities()
    RegisterForSleep()
    Touch()
    PokeMCM()
    KickWidget()        ; re-register the widget after a load
    ClearColdVisual()   ; drop stuck visual; the tick restores it if needed
    Schedule()
EndEvent

; A save keeps its own GLOB values. When its recorded version lags
; SETTINGS_VERSION, MigrateSettings re-applies all defaults once, then stamps
; the new version. Bump this whenever a default changes.
int SETTINGS_VERSION = 38   ; v38: DryMinutes is in-game minutes, default 15

Function MigrateSettings()
    If !ready
        return
    EndIf
    int have = StorageUtil.GetIntValue(pl, "_RSL_SettingsVer", 0)
    If have < SETTINGS_VERSION
        ResetDefaults()
        StorageUtil.SetIntValue(pl, "_RSL_SettingsVer", SETTINGS_VERSION)
        _RSL_Log.W("MigrateSettings: " + have + " -> " + SETTINGS_VERSION + ", defaults applied")
    EndIf
    If have < 29 && have > 0
        ; v29: common cold moved to the _RSL_Disease engine. Drop every trace of
        ; the pre-v29 cold disease so the load is silent, not a fake "cured".
        If sCC1
            pl.RemoveSpell(sCC1)
        EndIf
        If sCC2
            pl.RemoveSpell(sCC2)
        EndIf
        If sCC3
            pl.RemoveSpell(sCC3)
        EndIf
        StorageUtil.UnsetIntValue(pl, "_RSL_ColdColdStage")
        StorageUtil.UnsetFloatValue(pl, "_RSL_ColdColdChanged")
        StorageUtil.UnsetFloatValue(pl, "_RSL_ColdColdRoll")
        StorageUtil.UnsetIntValue(pl, "_RSL_Dz_CC_Stage")
        StorageUtil.UnsetFloatValue(pl, "_RSL_Dz_CC_Time")
        StorageUtil.UnsetFloatValue(pl, "_RSL_Dz_CC_Roll")
        StorageUtil.UnsetIntValue(pl, "_RSL_Dz_CC_Cures")
        StorageUtil.UnsetFloatValue(pl, "_RSL_Dz_CC_Prog")
        _RSL_Log.W("MigrateSettings: cleared pre-v29 cold-disease state")
    EndIf
    If have < 33 && have > 0
        ; v33: balance pass reworked every disease's effect list - the stage
        ; spell formIDs changed, so an in-progress disease from an older save
        ; holds an orphaned spell. Clear all disease state; the tick re-detects
        ; anything still genuinely active (RFAB wrappers via HasMagicEffect).
        CureColdDisease()
        _RSL_Log.W("MigrateSettings: v33 balance pass - cleared all disease state")
    EndIf
EndFunction

; Unconditionally clears all 3 abilities and the tier keys. Needed at
; start/load: a sub-step penalty could get stuck in an old save. The tick
; restores the current penalty immediately if there is one.
Function SanitizeAbilities()
    If !ready
        return
    EndIf
    ClearAxis(abSleep,  K_TIER_SL)
    ClearAxis(abHunger, K_TIER_HU)
    ClearAxis(abCold,   K_TIER_CO)
    ClearBonus()
    ; re-sync hypothermia's lockdown + rest-block after a load
    int hySt = _RSL_Disease.GetStage(pl, "HY")
    HypoSetLock(hySt >= 3, false)
    ; Game.SetInChargen does not persist a load - engine now allows rest; match
    ; K_HYWAIT to that, then let SyncHypoWait re-block if needed.
    StorageUtil.SetIntValue(pl, K_HYWAIT, 0)
    SyncHypoWait()
    _RSL_Log.W("SanitizeAbilities: all axes cleared")
EndFunction

; Re-arms the MCM quest's subscription to SKICP_configManagerReady.
; SKI_ConfigBase subscribes only inside OnGameReload, called once by OnInit,
; and mod-event subscriptions do not survive a save load. SkyUI mods re-arm it
; from a player-alias script; we have no alias, so call OnGameReload by hand.
; Without this the menu never registers (OnConfigInit but no OnConfigRegister).
Function PokeMCM()
    Quest q = _RSL_Forms.QstMCM()
    If q
        (q as _RSL_MCM).OnGameReload()
        _RSL_Log.W("PokeMCM: OnGameReload sent")
    Else
        _RSL_Log.W("PokeMCM: QstMCM not found")
    EndIf
EndFunction

; The widget loses its SkyUI-manager subscription on save load, same as MCM.
; Call OnGameReload by hand.
Function KickWidget()
    Quest q = _RSL_Forms.QstWidget()
    If q
        (q as _RSL_HUDWidget).Kick()
        (q as _RSL_HUDWidget).SetMenuHidden(false)   ; clear any stuck menu-hide
        _RSL_Log.W("KickWidget: sent")
    Else
        _RSL_Log.W("KickWidget: QstWidget not found")
    EndIf
EndFunction

string Function AnchorName(float idx, bool horiz)
    If horiz
        If idx >= 1.5
            return "right"
        ElseIf idx >= 0.5
            return "center"
        EndIf
        return "left"
    EndIf
    If idx >= 1.5
        return "bottom"
    ElseIf idx >= 0.5
        return "center"
    EndIf
    return "top"
EndFunction

float Function Frac01(float x)
    If x < 0.0
        return 0.0
    ElseIf x > 1.0
        return 1.0
    EndIf
    return x
EndFunction

; 0..4 "temperature feel" for the widget icon, from Severity - Mitigation:
;   0 much colder (cold rising fast) .. 2 balanced .. 4 much warmer (falling fast)
int Function TempFeel()
    float d = Severity() - Mitigation()
    If d >= 25.0
        return 0
    ElseIf d >= 8.0
        return 1
    ElseIf d > -8.0
        return 2
    ElseIf d > -25.0
        return 3
    EndIf
    return 4
EndFunction

; Once per tick: push layout + bar values to the widget.
Function PushWidget()
    Quest q = _RSL_Forms.QstWidget()
    If !q
        return
    EndIf
    _RSL_HUDWidget w = q as _RSL_HUDWidget
    If !w
        return
    EndIf

    bool off = gModEnabled.GetValue() < 0.5 || GV(gHudWidget, 1.0) < 0.5
    bool undead = pl.HasKeyword(kwUndead)

    float slMax = gSleepMax.GetValue()
    float slFill = 100.0 - Frac01(StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0) / slMax) * 100.0
    float slSafe = 100.0 - Frac01(gSleepGrace.GetValue() / slMax) * 100.0
    bool slShown = !off && !undead

    float huMax = gHungerMax.GetValue()
    float huFill = 100.0 - Frac01(StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0) / huMax) * 100.0
    float huSafe = 100.0 - Frac01(gHungerGrace.GetValue() / huMax) * 100.0
    bool huShown = !off && !undead

    float coldV = StorageUtil.GetFloatValue(pl, K_COLD, 0.0)
    float coFill = 100.0 - coldV
    float coSafe = 100.0 - GV(gColdGrace, 25.0)
    bool coShown = !off

    w.X = GV(gHudWidgetX, 220.0)
    w.Y = GV(gHudWidgetY, 655.0)
    w.HAnchor = AnchorName(GV(gHudWidgetHAnchor, 0.0), true)
    w.VAnchor = AnchorName(GV(gHudWidgetVAnchor, 0.0), false)
    w.SetScale(GV(gHudWidgetScale, 100.0))

    w.PushData(slShown, slFill, slSafe, huShown, huFill, huSafe, coShown, coFill, coSafe,         GV(gHudWidgetAutoHide, 1.0) > 0.5, GV(gHudWidgetAlpha, 100.0), TempFeel(), GV(gHudColor, 1.0) >= 0.5)
EndFunction

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    UnregisterForUpdate()
    UnregisterForSleep()
    ClearAllPenalties()
    ClearColdVisual()
EndEvent

Function Bind()
    pl = Game.GetPlayer()

    abSleep     = _RSL_Forms.AbSleep()
    abHunger    = _RSL_Forms.AbHunger()
    abCold      = _RSL_Forms.AbCold()
    abBonusWarm = _RSL_Forms.AbBonusWarm()
    abBonusRest = _RSL_Forms.AbBonusRest()
    abBonusFed  = _RSL_Forms.AbBonusFed()
    fireSources   = _RSL_Forms.FireSources()
    coldInteriors = _RSL_Forms.ColdInteriors()
    kwUndead    = _RSL_Forms.ActorTypeUndead()
    kwMagicDamageFrost = _RSL_Forms.KwMagicDamageFrost()
    kwMagicDamageFire  = _RSL_Forms.KwMagicDamageFire()
    kwMagicDamageShock = _RSL_Forms.KwMagicDamageShock()

    gModEnabled          = _RSL_Forms.ModEnabled()
    gSleepGrace          = _RSL_Forms.SleepGrace()
    gSleepMax            = _RSL_Forms.SleepMax()
    gSleepRestorePerHour = _RSL_Forms.SleepRestorePerHour()
    gSleepMinHours       = _RSL_Forms.SleepMinHours()
    gCombatFatigueMult   = _RSL_Forms.CombatFatigueMult()
    gHungerGrace         = _RSL_Forms.HungerGrace()
    gHungerMax           = _RSL_Forms.HungerMax()
    gHungerFoodPct       = _RSL_Forms.HungerFoodPct()
    gHungerSpecialFoodPct = _RSL_Forms.HungerSpecialFoodPct()
    gRegionWinterhold    = _RSL_Forms.RegionWinterhold()
    gRegionPale          = _RSL_Forms.RegionPale()
    gRegionEastmarch     = _RSL_Forms.RegionEastmarch()
    gRegionReach         = _RSL_Forms.RegionReach()
    gRegionHjaalmarch    = _RSL_Forms.RegionHjaalmarch()
    gRegionHaafingar     = _RSL_Forms.RegionHaafingar()
    gRegionWhiterun      = _RSL_Forms.RegionWhiterun()
    gRegionFalkreath     = _RSL_Forms.RegionFalkreath()
    gRegionRift          = _RSL_Forms.RegionRift()
    gRegionDefault       = _RSL_Forms.RegionDefault()
    gRegionSnowFloor     = _RSL_Forms.RegionSnowFloor()
    gWeatherClear        = _RSL_Forms.WeatherClear()
    gWeatherCloudy       = _RSL_Forms.WeatherCloudy()
    gWeatherRain         = _RSL_Forms.WeatherRain()
    gWeatherSnow         = _RSL_Forms.WeatherSnow()
    gNightMult           = _RSL_Forms.NightMult()
    gSwimMult            = _RSL_Forms.SwimMult()
    gRegionAltitude      = _RSL_Forms.RegionAltitude()
    gFireMult            = _RSL_Forms.FireMult()
    gSevInterior         = _RSL_Forms.SevInterior()
    gSevColdInterior     = _RSL_Forms.SevColdInterior()
    gAltitudeLow         = _RSL_Forms.AltitudeLow()
    gAltitudeHigh        = _RSL_Forms.AltitudeHigh()
    gFireRadius          = _RSL_Forms.FireRadius()
    gColdRate            = _RSL_Forms.ColdRate()
    gWarmthPerSlot       = _RSL_Forms.WarmthPerSlot()
    gResistWeight        = _RSL_Forms.ResistWeight()
    gDryMinutes          = _RSL_Forms.DryMinutes()
    gFrostHitCold        = _RSL_Forms.FrostHitCold()
    gFireHitWarm         = _RSL_Forms.FireHitWarm()
    gElemLesionEnabled   = _RSL_Forms.ElemLesionEnabled()
    gElemLesionColdThr   = _RSL_Forms.ElemLesionColdThr()
    gElemLesionHypoChance = _RSL_Forms.ElemLesionHypoChance()
    gElemLesionHitP      = _RSL_Forms.ElemLesionHitP()
    gElemLesionContractP = _RSL_Forms.ElemLesionContractP()
    gElemLesionBandageP  = _RSL_Forms.ElemLesionBandageP()
    gPenaltyPrimary      = _RSL_Forms.PenaltyPrimary()
    gPenaltyCross        = _RSL_Forms.PenaltyCross()
    gPenaltyCap          = _RSL_Forms.PenaltyCap()
    gTierStep            = _RSL_Forms.TierStep()
    gPollInterval        = _RSL_Forms.PollInterval()
    gDebugLog            = _RSL_Forms.DebugLog()

    gPenaltySpeed        = _RSL_Forms.PenaltySpeed()
    gSpeedCap            = _RSL_Forms.SpeedCap()
    gColdGrace           = _RSL_Forms.ColdGrace()
    gWarmupMult          = _RSL_Forms.WarmupMult()
    gHudWidget           = _RSL_Forms.HudWidget()
    gHudColor            = _RSL_Forms.HudColor()
    gHudWidgetAutoHide   = _RSL_Forms.HudWidgetAutoHide()
    gHudWidgetX          = _RSL_Forms.HudWidgetX()
    gHudWidgetY          = _RSL_Forms.HudWidgetY()
    gHudWidgetScale      = _RSL_Forms.HudWidgetScale()
    gHudWidgetAlpha      = _RSL_Forms.HudWidgetAlpha()
    gHudWidgetHAnchor    = _RSL_Forms.HudWidgetHAnchor()
    gHudWidgetVAnchor    = _RSL_Forms.HudWidgetVAnchor()
    gBonusEnabled        = _RSL_Forms.BonusEnabled()
    gBonusRegenPct       = _RSL_Forms.BonusRegenPct()
    gBonusThresholdPct   = _RSL_Forms.BonusThresholdPct()

    gDiseaseEnabled      = _RSL_Forms.DiseaseEnabled()
    gDiseaseProgressHours = _RSL_Forms.DiseaseProgressHours()
    gDiseaseDecayHours   = _RSL_Forms.DiseaseDecayHours()
    gDiseaseHitChance    = _RSL_Forms.DiseaseHitChance()
    gFoodPoisonChance    = _RSL_Forms.FoodPoisonChance()
    gRfabDzEnabled       = _RSL_Forms.RfabDzEnabled()
    gHypEnabled          = _RSL_Forms.HypothermiaEnabled()
    gHypThreshold        = _RSL_Forms.HypothermiaThreshold()
    gHypRecoverThr       = _RSL_Forms.HypothermiaRecoverThr()
    gHypWorsenHours      = _RSL_Forms.HypothermiaWorsenHours()
    gHypRecoverHours     = _RSL_Forms.HypothermiaRecoverHours()
    gHypDrainPerSec      = _RSL_Forms.HypothermiaDrainPerSec()
    gHypDrainRamp        = _RSL_Forms.HypothermiaDrainRamp()
    sHY1      = _RSL_Forms.AbHypo1()
    sHY2      = _RSL_Forms.AbHypo2()
    sHY3      = _RSL_Forms.AbHypo3()
    msgHY1    = _RSL_Forms.MsgHypo1()
    msgHY2    = _RSL_Forms.MsgHypo2()
    msgHY3    = _RSL_Forms.MsgHypo3()
    msgHYCured  = _RSL_Forms.MsgHypoCured()
    msgHYNoRest = _RSL_Forms.MsgHypoNoRest()
    gColdColdThreshold   = _RSL_Forms.ColdColdThreshold()
    gColdColdChanceMin   = _RSL_Forms.ColdColdChanceMin()
    gColdColdChanceMax   = _RSL_Forms.ColdColdChanceMax()
    gColdColdChanceMaxAt = _RSL_Forms.ColdColdChanceMaxAt()
    sCC1      = _RSL_Forms.DiseaseColdCommon1()
    sCC2      = _RSL_Forms.DiseaseColdCommon2()
    sCC3      = _RSL_Forms.DiseaseColdCommon3()
    msgCC1    = _RSL_Forms.MsgColdCommon1()
    msgCC2    = _RSL_Forms.MsgColdCommon2()
    msgCC3    = _RSL_Forms.MsgColdCommon3()
    msgCCCured = _RSL_Forms.MsgColdCommonCured()
    sEL1      = _RSL_Forms.DiseaseElemLesion1()
    sEL2      = _RSL_Forms.DiseaseElemLesion2()
    sEL3      = _RSL_Forms.DiseaseElemLesion3()
    msgEL1    = _RSL_Forms.MsgElemLesion1()
    msgEL2    = _RSL_Forms.MsgElemLesion2()
    msgEL3    = _RSL_Forms.MsgElemLesion3()
    msgELCured = _RSL_Forms.MsgElemLesionCured()

    raceDraugr        = _RSL_Forms.RaceDraugr()
    raceSlaughterfish = _RSL_Forms.RaceSlaughterfish()
    kwTroll           = _RSL_Forms.KwActorTypeTroll()
    kwRawFood         = _RSL_Forms.KwRawFood()
    kwStrongStomach   = _RSL_Forms.KwStrongStomach()
    kwSpecialFood     = _RSL_Forms.KwSpecialFood()
    kwSpecialDrink    = _RSL_Forms.KwSpecialDrink()
    hdId = new string[4]
    hdId[0] = "BR"
    hdId[1] = "GW"
    hdId[2] = "GS"
    hdId[3] = "FP"
    hdS1 = new Spell[4]
    hdS2 = new Spell[4]
    hdS3 = new Spell[4]
    hdM1 = new Message[4]
    hdM2 = new Message[4]
    hdM3 = new Message[4]
    hdMC = new Message[4]
    hdS1[0] = _RSL_Forms.DiseaseBrownRot1()
    hdS2[0] = _RSL_Forms.DiseaseBrownRot2()
    hdS3[0] = _RSL_Forms.DiseaseBrownRot3()
    hdM1[0] = _RSL_Forms.MsgBrownRot1()
    hdM2[0] = _RSL_Forms.MsgBrownRot2()
    hdM3[0] = _RSL_Forms.MsgBrownRot3()
    hdMC[0] = _RSL_Forms.MsgBrownRotCured()
    hdS1[1] = _RSL_Forms.DiseaseGutworm1()
    hdS2[1] = _RSL_Forms.DiseaseGutworm2()
    hdS3[1] = _RSL_Forms.DiseaseGutworm3()
    hdM1[1] = _RSL_Forms.MsgGutworm1()
    hdM2[1] = _RSL_Forms.MsgGutworm2()
    hdM3[1] = _RSL_Forms.MsgGutworm3()
    hdMC[1] = _RSL_Forms.MsgGutwormCured()
    hdS1[2] = _RSL_Forms.DiseaseGreenspore1()
    hdS2[2] = _RSL_Forms.DiseaseGreenspore2()
    hdS3[2] = _RSL_Forms.DiseaseGreenspore3()
    hdM1[2] = _RSL_Forms.MsgGreenspore1()
    hdM2[2] = _RSL_Forms.MsgGreenspore2()
    hdM3[2] = _RSL_Forms.MsgGreenspore3()
    hdMC[2] = _RSL_Forms.MsgGreensporeCured()
    hdS1[3] = _RSL_Forms.DiseaseFoodPoison1()
    hdS2[3] = _RSL_Forms.DiseaseFoodPoison2()
    hdS3[3] = _RSL_Forms.DiseaseFoodPoison3()
    hdM1[3] = _RSL_Forms.MsgFoodPoison1()
    hdM2[3] = _RSL_Forms.MsgFoodPoison2()
    hdM3[3] = _RSL_Forms.MsgFoodPoison3()
    hdMC[3] = _RSL_Forms.MsgFoodPoisonCured()

    rdId = new string[7]
    rdId[0] = "AT"
    rdId[1] = "RJ"
    rdId[2] = "WB"
    rdId[3] = "RA"
    rdId[4] = "BF"
    rdId[5] = "BRR"
    rdId[6] = "DR"
    rdBase = new Spell[7]
    rdMark = new MagicEffect[7]
    rd2  = new Spell[7]
    rd3  = new Spell[7]
    rdM2 = new Message[7]
    rdM3 = new Message[7]
    rdMC = new Message[7]
    rdBase[0] = _RSL_Forms.RfabDzAT()
    rdBase[1] = _RSL_Forms.RfabDzRJ()
    rdBase[2] = _RSL_Forms.RfabDzWB()
    rdBase[3] = _RSL_Forms.RfabDzRA()
    rdBase[4] = _RSL_Forms.RfabDzBF()
    rdBase[5] = _RSL_Forms.RfabDzBRR()
    rdMark[0] = _RSL_Forms.RfabDzMarkAT()
    rdMark[1] = _RSL_Forms.RfabDzMarkRJ()
    rdMark[2] = _RSL_Forms.RfabDzMarkWB()
    rdMark[3] = _RSL_Forms.RfabDzMarkRA()
    rdMark[4] = _RSL_Forms.RfabDzMarkBF()
    rdMark[5] = _RSL_Forms.RfabDzMarkBRR()
    rdMark[6] = _RSL_Forms.RfabDzMarkDR()
    rdBase[6] = _RSL_Forms.RfabDzDR()
    rd2[0] = _RSL_Forms.DzAT2()
    rd3[0] = _RSL_Forms.DzAT3()
    rdM2[0] = _RSL_Forms.MsgDzAT2()
    rdM3[0] = _RSL_Forms.MsgDzAT3()
    rdMC[0] = _RSL_Forms.MsgDzATCured()
    rd2[1] = _RSL_Forms.DzRJ2()
    rd3[1] = _RSL_Forms.DzRJ3()
    rdM2[1] = _RSL_Forms.MsgDzRJ2()
    rdM3[1] = _RSL_Forms.MsgDzRJ3()
    rdMC[1] = _RSL_Forms.MsgDzRJCured()
    rd2[2] = _RSL_Forms.DzWB2()
    rd3[2] = _RSL_Forms.DzWB3()
    rdM2[2] = _RSL_Forms.MsgDzWB2()
    rdM3[2] = _RSL_Forms.MsgDzWB3()
    rdMC[2] = _RSL_Forms.MsgDzWBCured()
    rd2[3] = _RSL_Forms.DzRA2()
    rd3[3] = _RSL_Forms.DzRA3()
    rdM2[3] = _RSL_Forms.MsgDzRA2()
    rdM3[3] = _RSL_Forms.MsgDzRA3()
    rdMC[3] = _RSL_Forms.MsgDzRACured()
    rd2[4] = _RSL_Forms.DzBF2()
    rd3[4] = _RSL_Forms.DzBF3()
    rdM2[4] = _RSL_Forms.MsgDzBF2()
    rdM3[4] = _RSL_Forms.MsgDzBF3()
    rdMC[4] = _RSL_Forms.MsgDzBFCured()
    rd2[5] = _RSL_Forms.DzBRR2()
    rd3[5] = _RSL_Forms.DzBRR3()
    rdM2[5] = _RSL_Forms.MsgDzBRR2()
    rdM3[5] = _RSL_Forms.MsgDzBRR3()
    rdMC[5] = _RSL_Forms.MsgDzBRRCured()
    rd2[6] = _RSL_Forms.DzDR2()
    rd3[6] = _RSL_Forms.DzDR3()
    rdM2[6] = _RSL_Forms.MsgDzDR2()
    rdM3[6] = _RSL_Forms.MsgDzDR3()
    rdMC[6] = _RSL_Forms.MsgDzDRCured()

    ready = (pl != None) && (abCold != None) && (gModEnabled != None)
    SyncDebugLog()

    If ready
        RegisterForMenu("Sleep/Wait Menu")
        ; SkyUI's item menus never drive the widget .swf's own menu-hide path.
        RegisterForMenu("InventoryMenu")
        RegisterForMenu("ContainerMenu")
        RegisterForMenu("BarterMenu")
        RegisterForMenu("GiftMenu")
        _RSL_Log.W("Bind OK: forms resolved, ModEnabled=" + gModEnabled.GetValue())
    Else
        _RSL_Log.W("Bind FAILED: pl=" + pl + " abCold=" + abCold + " gModEnabled=" + gModEnabled)
    EndIf
EndFunction

; SkyUI inventory/container/barter/gift share one "ItemMenu" .swf that does not
; push a HUD mode the widget filters on, so it stays drawn over them (Magic /
; Map / Journal hide it fine). Hide it by hand on open, restore in OnMenuClose.
bool Function MenuHidesWidget(string m)
    return m == "InventoryMenu" || m == "ContainerMenu" \
        || m == "BarterMenu" || m == "GiftMenu"
EndFunction

Function SetWidgetMenuHidden(bool hidden)
    _RSL_HUDWidget w = _RSL_Forms.QstWidget() as _RSL_HUDWidget
    If w
        w.SetMenuHidden(hidden)
    EndIf
EndFunction

; Hypothermia blocks resting - EXCEPT when the player is indoors and actively
; warming (Severity < Mitigation): a warm shelter is exactly where you sleep it
; off. Ice caves keep a high Severity so they stay blocked.
Event OnMenuOpen(string menuName)
    If !ready
        return
    EndIf
    If MenuHidesWidget(menuName)
        SetWidgetMenuHidden(true)
        return
    EndIf
    If menuName != "Sleep/Wait Menu"
        return
    EndIf
    int hyStage = _RSL_Disease.GetStage(pl, "HY")
    If hyStage <= 0
        return
    EndIf
    Cell c = pl.GetParentCell()
    bool inside = c && c.IsInterior()
    float sev = Severity()
    float mit = Mitigation()
    bool warmingIndoors = inside && (sev < mit)
    _RSL_Log.W("Hypothermia OnMenuOpen: stage=" + hyStage + " inside=" + inside \
        + " sev=" + sev + " mit=" + mit + " -> " + warmingIndoors)
    If warmingIndoors
        return
    EndIf
    UI.Invoke("Sleep/Wait Menu", "_root.WaitMenu_mc.CloseMenu")
    If msgHYNoRest
        msgHYNoRest.Show()
    EndIf
    _RSL_Log.W("Hypothermia: blocked Sleep/Wait menu")
EndEvent

Event OnMenuClose(string menuName)
    If ready && MenuHidesWidget(menuName)
        SetWidgetMenuHidden(false)
    EndIf
EndEvent

Function Schedule()
    float iv = 1.0
    If gPollInterval
        iv = gPollInterval.GetValue()
    EndIf
    If iv < 1.0
        iv = 1.0
    EndIf
    RegisterForSingleUpdate(iv)
EndFunction

; Reset the reference point so no delta is charged for time that did not pass
; for us (first run, save load).
Function Touch()
    StorageUtil.SetFloatValue(pl, K_LASTTIME, Utility.GetCurrentGameTime())
    ; real-time clock restarts each launch - drop a stale future elem-hit stamp
    StorageUtil.SetFloatValue(pl, K_ELEMT, 0.0)
EndFunction

; GLOB value with a fallback default. Needed on upgrade runs: until the
; generator recreates the plugin, a new _RSL_Forms getter returns None.
float Function GV(GlobalVariable g, float dflt)
    If g
        return g.GetValue()
    EndIf
    return dflt
EndFunction

; Mirror the "Отладочный лог" toggle into the fast flag _RSL_Log.W reads.
Function SyncDebugLog()
    StorageUtil.SetIntValue(None, "_RSL_DbgLog", ((gDebugLog != None) && (gDebugLog.GetValue() >= 0.5)) as int)
EndFunction

; --- main tick ------------------------------------------------------------

Event OnUpdate()
    If !ready
        Bind()
        Schedule()
        return
    EndIf

    SyncDebugLog()

    If gModEnabled.GetValue() < 0.5
        ; Run the full teardown once on the on->off edge; the monitor keeps
        ; ticking so re-enabling re-applies everything from a clean slate.
        If StorageUtil.GetIntValue(pl, "_RSL_ModOff", 0) == 0
            _RSL_Log.W("tick: ModEnabled=0 - running teardown")
            TeardownAll()
            StorageUtil.SetIntValue(pl, "_RSL_ModOff", 1)
        EndIf
        Touch()
        Schedule()
        return
    EndIf
    StorageUtil.SetIntValue(pl, "_RSL_ModOff", 0)

    float now = Utility.GetCurrentGameTime()
    float last = StorageUtil.GetFloatValue(pl, K_LASTTIME, now)
    float dtHours = (now - last) * 24.0

    ; Negative delta means time went backwards (console, loading an earlier
    ; save). Nothing to charge.
    If dtHours < 0.0
        dtHours = 0.0
    EndIf
    StorageUtil.SetFloatValue(pl, K_LASTTIME, now)

    bool undead = pl.HasKeyword(kwUndead)
    HandleUndeadTransition(undead)

    AdvanceSleep(dtHours, undead)
    AdvanceHunger(dtHours, undead)
    ApplyElemHits()
    AdvanceCold(dtHours)
    AdvanceColdDisease(undead, dtHours)
    AdvanceElemLesion(undead, dtHours)
    int hd = 0
    While hd < 4
        AdvanceHitDz(hd, undead, dtHours)
        hd += 1
    EndWhile
    int rd = 0
    While rd < 7
        AdvanceRfabDz(rd, undead, dtHours)
        rd += 1
    EndWhile
    AdvanceHypothermia(dtHours, undead)
    SyncHypoWait()   ; follows the player in/out of warm shelter

    ApplyPenalties(undead)
    ApplyBonus(undead)
    PushWidget()

    ; per-tick diagnostics - skip the string building entirely unless logging is on
    If StorageUtil.GetIntValue(None, "_RSL_DbgLog", 0) > 0
        _RSL_Log.W("tick dt=" + dtHours + "h  sleep=" + StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0) + "  hunger=" + StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0) + "  cold=" + StorageUtil.GetFloatValue(pl, K_COLD, 0.0) + "  sev=" + Severity() + "  mit=" + Mitigation() + "  undead=" + undead)
        LogStats()
        LogCold()
    EndIf
    Schedule()
EndEvent

; Cold breakdown: how severity/mitigation add up and the COLD axis state.
Function LogCold()
    Cell c = pl.GetParentCell()
    bool interior = c && c.IsInterior()
    Weather w = Weather.GetCurrentWeather()
    int wcls = -1
    If w
        wcls = w.GetClassification()
    EndIf
    ObjectReference fr400 = None
    ObjectReference fr2000 = None
    If fireSources
        fr400  = Game.FindClosestReferenceOfAnyTypeInListFromRef(fireSources, pl, gFireRadius.GetValue())
        fr2000 = Game.FindClosestReferenceOfAnyTypeInListFromRef(fireSources, pl, 2000.0)
    EndIf
    string fbase = ""
    If fr2000
        Form b = fr2000.GetBaseObject()
        fbase = PO3_SKSEFunctions.GetFormEditorID(b) + " " + b + " d=" + (pl.GetDistance(fr2000) as int)
    EndIf
    bool torch = pl.GetEquippedItemType(0) == 11 || pl.GetEquippedItemType(1) == 11
    float hour = Utility.GetCurrentGameTime()
    hour = (hour - Math.Floor(hour)) * 24.0

    _RSL_Log.W("cold: interior=" + interior + " coldInterior=" + ColdInteriorHere() + " snowClim=" + SnowClimateHere() + " wcls=" + wcls + " hour=" + hour + " altZ=" + (pl.GetPositionZ() as int) + " fire400=" + (fr400 != None) + " fire2000=" + (fr2000 != None) + " nearestFireBase=" + fbase + " torch=" + torch + " swim=" + pl.IsSwimming() + " regionBase=" + RegionBase())
    _RSL_Log.W("cold: warmthPts=" + WarmthPoints() + " wet=" + WetnessFactor() + " frostAV=" + pl.GetActorValue("FrostResist") + " sev=" + Severity() + " mit=" + Mitigation())
    _RSL_Log.W("axes: abSleep has=" + pl.HasSpell(abSleep) + " H=" + abSleep.GetNthEffectMagnitude(0) + " M=" + abSleep.GetNthEffectMagnitude(1) + " | abHunger has=" + pl.HasSpell(abHunger) + " S=" + abHunger.GetNthEffectMagnitude(2) + " | abCold has=" + pl.HasSpell(abCold) + " H=" + abCold.GetNthEffectMagnitude(0) + " Spd=" + abCold.GetNthEffectMagnitude(3))
    _RSL_Log.W("coldDis: stage=" + _RSL_Disease.GetStage(pl, "CC") + " P=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_CC_Prog", 0.0) + " mitMult=" + ColdDiseaseMitMult() + " contractChance/h=" + CCContractChance(StorageUtil.GetFloatValue(pl, K_COLD, 0.0)) + " diseaseResist=" + pl.GetActorValue("DiseaseResist") + " has1=" + (sCC1 && pl.HasSpell(sCC1)) + " has2=" + (sCC2 && pl.HasSpell(sCC2)) + " has3=" + (sCC3 && pl.HasSpell(sCC3)))
    int hyStage = _RSL_Disease.GetStage(pl, "HY")
    If hyStage > 0 || StorageUtil.GetFloatValue(pl, "_RSL_Dz_HY_Prog", 0.0) != 0.0
        _RSL_Log.W("hypo: stage=" + hyStage + " P=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_HY_Prog", 0.0) + " s3=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_HY_S3", 0.0) + " has3=" + (sHY3 && pl.HasSpell(sHY3)) + " hp=" + pl.GetActorValue("Health"))
    EndIf
    If _RSL_Disease.GetStage(pl, "EL") > 0 || StorageUtil.GetFloatValue(pl, "_RSL_Dz_EL_Prog", 0.0) != 0.0 || StorageUtil.GetFloatValue(pl, K_ELPACC, 0.0) != 0.0
        _RSL_Log.W("elemLes: stage=" + _RSL_Disease.GetStage(pl, "EL") + " P=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_EL_Prog", 0.0) + " pAcc=" + StorageUtil.GetFloatValue(pl, K_ELPACC, 0.0) + " coldDeep=" + (StorageUtil.GetFloatValue(pl, K_COLD, 0.0) >= GV(gElemLesionColdThr, 90.0)) + " hyStage=" + _RSL_Disease.GetStage(pl, "HY") + " allClear=" + !DzAnyAxisBad(pl.HasKeyword(kwUndead)))
    EndIf
    If hdId
        int hi = 0
        While hi < 4
            int st = _RSL_Disease.GetStage(pl, hdId[hi])
            If st > 0
                _RSL_Log.W("hitDz " + hdId[hi] + ": stage=" + st + " P=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_" + hdId[hi] + "_Prog", 0.0) + " bad=" + DzAnyAxisBad(pl.HasKeyword(kwUndead)) + " hasStageSpell=" + (HitStageSpell(hi, st) && pl.HasSpell(HitStageSpell(hi, st))))
            EndIf
            hi += 1
        EndWhile
    EndIf
    If rdId
        int ri = 0
        While ri < 7
            int rst = _RSL_Disease.GetStage(pl, rdId[ri])
            bool rmark = rdMark[ri] && pl.HasMagicEffect(rdMark[ri])
            If rst > 0 || rmark || (rdBase[ri] && pl.HasSpell(rdBase[ri]))
                _RSL_Log.W("rfabDz " + rdId[ri] + ": stage=" + rst + " P=" + StorageUtil.GetFloatValue(pl, "_RSL_Dz_" + rdId[ri] + "_Prog", 0.0) + " bad=" + DzAnyAxisBad(pl.HasKeyword(kwUndead)) + " mark=" + rmark + " hasBase=" + (rdBase[ri] && pl.HasSpell(rdBase[ri])) + " has2=" + (rd2[ri] && pl.HasSpell(rd2[ri])) + " has3=" + (rd3[ri] && pl.HasSpell(rd3[ri])))
            EndIf
            ri += 1
        EndWhile
    EndIf
EndFunction

Function LogStats()
    _RSL_Log.W("resist frost=" + pl.GetActorValue("FrostResist") + " magic=" + pl.GetActorValue("MagicResist") + " fire=" + pl.GetActorValue("FireResist"))

    _RSL_Log.W("stats H base=" + pl.GetBaseActorValue("Health")         + " av=" + pl.GetActorValue("Health")         + " pct=" + pl.GetActorValuePercentage("Health")         + " | M base=" + pl.GetBaseActorValue("Magicka")         + " av=" + pl.GetActorValue("Magicka")         + " pct=" + pl.GetActorValuePercentage("Magicka")         + " | S base=" + pl.GetBaseActorValue("Stamina")         + " av=" + pl.GetActorValue("Stamina")         + " pct=" + pl.GetActorValuePercentage("Stamina"))
    ; SpeedMult incl. RFAB burden. <= 0 -> RFAB locks the player in place.
    _RSL_Log.W("speedmult=" + pl.GetActorValue("SpeedMult"))
EndFunction

; --- undead ---------------------------------------------------------------

Function HandleUndeadTransition(bool undead)
    bool was = StorageUtil.GetFloatValue(pl, K_WASUNDEAD, 0.0) > 0.5

    If undead == was
        return
    EndIf

    ; Counters restart from zero on both turning and curing, else a vampire
    ; cured after a game-month would instantly take the max penalty.
    StorageUtil.SetFloatValue(pl, K_SLEEP, 0.0)
    StorageUtil.SetFloatValue(pl, K_HUNGER, 0.0)
    StorageUtil.SetFloatValue(pl, K_WASUNDEAD, undead as float)

    If undead
        pl.RemoveSpell(abSleep)
        pl.RemoveSpell(abHunger)
        ClearColdDisease()
        If _RSL_Disease.GetStage(pl, "HY") > 0
            _RSL_Disease.ClearStages(pl, "HY", sHY1, sHY2, sHY3)
            HypoSetLock(false, false)
            SyncHypoWait()
        EndIf
    EndIf
EndFunction

Event OnVampirismStateChanged(bool abIsVampire)
    ; The poll would catch this via the keyword anyway; this reacts at once.
    If ready
        HandleUndeadTransition(pl.HasKeyword(kwUndead))
    EndIf
EndEvent

; --- SLEEP axis ----------------------------------------------------------

; Combat multiplies the sleep AND hunger accrual rate. 1.0 out of combat.
; GetCombatState 1 = in combat (2 = "searching", not counted).
float Function CombatMult()
    If pl.GetCombatState() == 1
        return GV(gCombatFatigueMult, 5.0)
    EndIf
    return 1.0
EndFunction

Function AdvanceSleep(float dtHours, bool undead)
    If undead
        return
    EndIf

    ; Axis is paused during sleep, else the tick also charges the slept hours
    ; and the effective restore rate drops.
    If StorageUtil.GetFloatValue(pl, K_SLEEPING, 0.0) > 0.5
        return
    EndIf

    float v = StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0) + dtHours * CombatMult()
    ; Clamp to max, else the counter runs to hundreds of hours and a night's
    ; sleep shows no visible progress (penalty is on the ramp cap regardless).
    float mx = gSleepMax.GetValue()
    If v > mx
        v = mx
    EndIf
    StorageUtil.SetFloatValue(pl, K_SLEEP, v)
EndFunction

Event OnSleepStart(float afSleepStartTime, float afDesiredSleepEndTime)
    If !ready
        return
    EndIf
    StorageUtil.SetFloatValue(pl, K_SLEEPING, 1.0)
    StorageUtil.SetFloatValue(pl, K_SLEEPFROM, afSleepStartTime)
EndEvent

Event OnSleepStop(bool abInterrupted)
    If !ready
        return
    EndIf

    float now    = Utility.GetCurrentGameTime()
    float from   = StorageUtil.GetFloatValue(pl, K_SLEEPFROM, now)
    float slept  = (now - from) * 24.0

    StorageUtil.SetFloatValue(pl, K_SLEEPING, 0.0)

    ; Sleep pauses only the SLEEP axis. Hunger and cold advance as during a
    ; wait: charge the slept hours explicitly here (the tick will not see
    ; them - the reference point moves below). AdvanceHunger/AdvanceCold are
    ; guarded by K_SLEEPING, so call after clearing the flag.
    If slept > 0.0
        AdvanceHunger(slept, pl.HasKeyword(kwUndead))
        AdvanceCold(slept)
    EndIf

    ; Move the reference point by hand: slept time must not enter the next
    ; tick's delta, as a charge or as a double count.
    StorageUtil.SetFloatValue(pl, K_LASTTIME, now)

    If slept >= gSleepMinHours.GetValue()
        float v = StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0)
        v -= slept * gSleepRestorePerHour.GetValue() * BrSleepMult()
        If v < 0.0
            v = 0.0
        EndIf
        StorageUtil.SetFloatValue(pl, K_SLEEP, v)
    EndIf

    ; Do NOT recompute magnitudes here. Requiem's REQ_HealingWhileSleeping
    ; does DamageActorValue("health", missingHealth) on OnSleepStop, where
    ; missingHealth was computed BEFORE sleep. Changing max health now applies
    ; that damage on a stale scale - up to death on waking. Leave it to the
    ; tick, which recomputes on the next update.

    ; Diseases advance over the slept hours too - the tick will not see them
    ; (K_LASTTIME moved above). Run AFTER the SLEEP counter is reduced so
    ; DzAnyAxisBad reads the post-sleep value (you slept well -> axis is fine).
    If slept > 0.0
        bool undead = pl.HasKeyword(kwUndead)
        AdvanceColdDisease(undead, slept)
        AdvanceElemLesion(undead, slept)
        int hd = 0
        While hd < 4
            AdvanceHitDz(hd, undead, slept)
            hd += 1
        EndWhile
        int rd = 0
        While rd < 7
            AdvanceRfabDz(rd, undead, slept)
            rd += 1
        EndWhile
        AdvanceHypothermia(slept, undead)
    EndIf

    Schedule()
EndEvent

; --- HUNGER axis ---------------------------------------------------------

Function AdvanceHunger(float dtHours, bool undead)
    ; Vampire hunger is fully handled by Req_FeedPenaltyScript; do not double.
    If undead
        return
    EndIf

    ; No charge during sleep: OnSleepStop adds the slept hours in one chunk
    ; (else double count).
    If StorageUtil.GetFloatValue(pl, K_SLEEPING, 0.0) > 0.5
        return
    EndIf

    float v = StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0) + dtHours * GwHungerMult() * CombatMult()
    float mx = gHungerMax.GetValue()
    If v > mx
        v = mx
    EndIf
    StorageUtil.SetFloatValue(pl, K_HUNGER, v)
EndFunction

; Consumed food / potions arrive here as equipping an ALCH.
Event OnObjectEquipped(Form akBaseObject, ObjectReference akReference)
    If !ready
        return
    EndIf

    Potion food = akBaseObject as Potion
    If !food
        return
    EndIf

    string eid = PO3_SKSEFunctions.GetFormEditorID(akBaseObject)

    ; "Чистая льняная ткань" [RFAB] - patches elemental-lesion wounds. Not a
    ; Food, so it must be caught before the IsFood gate.
    If eid == "RFAB_Bandage"
        NoteBandage()
        return
    EndIf

    If !food.IsFood()
        return
    EndIf

    ; Drinks (wine / mead / ale / water bottle) are all RFAB_Drink_* and carry
    ; only VendorItemFood - no single keyword separates them from an apple.
    ; EditorID prefix catches all of them (incl. water); RFAB_SpecialDrink is a
    ; fallback for alcohol if GetFormEditorID comes back empty in this setup.
    If StringUtil.Find(eid, "RFAB_Drink_") == 0 \
       || (kwSpecialDrink && akBaseObject.HasKeyword(kwSpecialDrink))
        return   ; drinks carry no hunger effect
    EndIf

    float mx = gHungerMax.GetValue()
    bool raw = kwRawFood && akBaseObject.HasKeyword(kwRawFood)

    ; Raw food on a weak stomach (no REQ_KW_StrongStomach race, not undead):
    ; a poison-roll, and it gives NO nourishment - it comes back up, so the
    ; hunger counter goes to max instead of dropping.
    bool rawWeak = raw \
                   && !(kwStrongStomach && pl.HasKeyword(kwStrongStomach)) \
                   && !pl.HasKeyword(kwUndead)

    If rawWeak && _RSL_Disease.GetStage(pl, "FP") <= 0 && hdS1[3] \
       && gModEnabled.GetValue() >= 0.5 && GV(gDiseaseEnabled, 1.0) >= 0.5
        If Utility.RandomFloat(0.0, 100.0) < GV(gFoodPoisonChance, 50.0)
            _RSL_Disease.SetStage(pl, "FP", 1, None, hdS1[3], 0.0, 0.0)
            _RSL_Disease.ResetP(pl, "FP")
            If hdM1[3]
                hdM1[3].Show()
            EndIf
            _RSL_Log.W("Dz FP: contracted from raw food (" + akBaseObject + ")")
        EndIf
    EndIf

    If rawWeak
        StorageUtil.SetFloatValue(pl, K_HUNGER, mx)
        Touch()
        return
    EndIf

    ; Restore scales with the item's WEIGHT (DATA - Weight), per kg:
    ;   plain Food item          -> HungerFoodPct % of the bar per kg (def. 50)
    ;   RFAB_SpecialFood keyword  -> HungerSpecialFoodPct % per kg     (def. 100)
    ;   RFAB_RawFood keyword      -> same as SpecialFood (dense, uncooked) -
    ;     only reachable with a strong stomach (rawWeak already returned above).
    ; The 2+ effects check stays as a fallback for non-RFAB dishes.
    float w = akBaseObject.GetWeight()
    If w <= 0.0
        w = 0.1          ; weightless food (some modded produce) still counts a little
    EndIf

    float pct = GV(gHungerFoodPct, 50.0)
    If raw || (kwSpecialFood && akBaseObject.HasKeyword(kwSpecialFood)) || food.GetNumEffects() >= 2
        pct = GV(gHungerSpecialFoodPct, 100.0)
    EndIf

    ; gutworm steals nutrition: a meal helps -25 / -50 / -80 % less by stage.
    float gwMult = 1.0 - GwFoodPenalty()
    float v = StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0) - mx * 0.01 * pct * w * gwMult
    If v < 0.0
        v = 0.0
    EndIf
    StorageUtil.SetFloatValue(pl, K_HUNGER, v)
    _RSL_Log.W("ate " + eid + " w=" + w + " pct/kg=" + pct + " -> hunger " + v)
    Touch()
EndEvent

; Gutworm penalty on hunger restore, by GW stage.
float Function GwFoodPenalty()
    int s = _RSL_Disease.GetStage(pl, "GW")
    If s >= 3
        return 0.8
    ElseIf s == 2
        return 0.5
    ElseIf s == 1
        return 0.25
    EndIf
    return 0.0
EndFunction

; --- COLD axis -----------------------------------------------------------

; Slot mask = 1 << (slot - 30). Head, body, hands, feet are checked; jewelry
; slots are not checked at all, so no exclusion list is needed.
float Function WarmthPoints()
    float per = gWarmthPerSlot.GetValue()
    float w = 0.0

    If pl.GetWornForm(0x00000001)      ; 30 head
        w += per
    EndIf
    If pl.GetWornForm(0x00000004)      ; 32 body
        w += per
    EndIf
    If pl.GetWornForm(0x00000008)      ; 33 hands
        w += per
    EndIf
    If pl.GetWornForm(0x00000080)      ; 37 feet
        w += per
    EndIf

    return w
EndFunction

; Wet clothing does not warm: slots are zeroed (not capped), so only the
; actor's own FrostResist counts. Drying is linear over DryMinutes IN-GAME
; minutes - K_WETUNTIL is the game-day the player is fully dry, so a Wait /
; sleep dries you off too.
float Function WetnessFactor()
    float now = Utility.GetCurrentGameTime()
    float dryDays = gDryMinutes.GetValue() / 1440.0    ; in-game minutes -> game days

    If pl.IsSwimming()
        StorageUtil.SetFloatValue(pl, K_WETUNTIL, now + dryDays)
        return 0.0
    EndIf

    If dryDays <= 0.0
        return 1.0
    EndIf

    float until = StorageUtil.GetFloatValue(pl, K_WETUNTIL, 0.0)
    If now >= until
        return 1.0
    EndIf

    ; the closer to `until`, the drier
    float remaining = (until - now) / dryDays
    If remaining > 1.0
        remaining = 1.0        ; stale deadline / time ran backwards -> just-wet
    EndIf
    return 1.0 - remaining
EndFunction

; sev = RegionBase x Weather x Night x Swim x Fire (interior: fire -> 0, else
; SevInterior). Current weather classification is a free region proxy -
; vanilla REGN already keeps snow out of the Rift and rain out of Winterhold.
float Function Severity()
    Cell c = pl.GetParentCell()
    bool interior = c && c.IsInterior()
    bool swim = pl.IsSwimming()

    ; Heat source: a world object from the list (campfire/forge/smelter/oven)
    ; OR a torch in hand.
    bool nearFire = false
    If fireSources
        If Game.FindClosestReferenceOfAnyTypeInListFromRef(fireSources, pl, gFireRadius.GetValue())
            nearFire = true
        EndIf
    EndIf
    If !nearFire && (pl.GetEquippedItemType(0) == 11 || pl.GetEquippedItemType(1) == 11)
        nearFire = true
    EndIf

    ; Shared multipliers, stored as percent, divided by 100.
    float nightM = 1.0
    float hour = Utility.GetCurrentGameTime()
    hour = (hour - Math.Floor(hour)) * 24.0
    If hour >= 22.0 || hour < 6.0
        nightM = GV(gNightMult, 170.0) / 100.0
    EndIf
    float swimM = 1.0
    If swim
        swimM = GV(gSwimMult, 220.0) / 100.0
    EndIf

    ; interior. Ordinary house/cave: fire + not swimming -> 0, else SevInterior.
    ; Ice cave / glacial ruin (ColdInteriors list): cold still bites - use
    ; SevColdInterior and a campfire only helps by FireMult (like outdoors).
    If interior
        If ColdInteriorHere()
            float ci = GV(gSevColdInterior, 45.0) * nightM * swimM
            If nearFire && !swim
                ci *= GV(gFireMult, 40.0) / 100.0
            EndIf
            return ci
        EndIf
        If nearFire && !swim
            return 0.0
        EndIf
        return GV(gSevInterior, 25.0) * nightM * swimM
    EndIf

    ; outdoors: sev = RegionBase x Weather x Night x Swim x Fire
    int wcls = -1
    Weather w = Weather.GetCurrentWeather()
    If w
        wcls = w.GetClassification()
    EndIf

    ; Cold climate = snow can fall at this spot. Two ways in:
    ;  - it is snowing right now (wcls 3) - definitive, and FindWeather can miss
    ;    it: that call reads only the local region's weather list, which is
    ;    empty of snow in mild regions even when snow blows in from a neighbour.
    ;  - SnowClimateHere: the region lists a snow weather even under a clear sky.
    ; Both the snow floor and the altitude ramp are gated on this: absolute
    ; worldspace Z alone cannot tell a cold height from a warm one (Riften's
    ; valley floor sits at Z ~11000 yet is temperate). The weather multiplier
    ; below still scales for the current sky on top, regardless.
    bool coldClimate = (wcls == 3) || SnowClimateHere()

    float sev = RegionBase()
    If coldClimate && sev < GV(gRegionSnowFloor, 40.0)
        sev = GV(gRegionSnowFloor, 40.0)
    EndIf

    ; Altitude: in a cold climate, between AltitudeLow and AltitudeHigh the base
    ; interpolates hold_base -> RegionAltitude by Z height; above High = full.
    ; A mountain pass is colder than the valley, a summit is arctic.
    float mtn = GV(gRegionAltitude, 100.0)
    If coldClimate && mtn > sev
        float zLo = GV(gAltitudeLow, 8000.0)
        float zHi = GV(gAltitudeHigh, 14000.0)
        float z = pl.GetPositionZ()
        If z > zLo && zHi > zLo
            float t = (z - zLo) / (zHi - zLo)
            If t > 1.0
                t = 1.0
            EndIf
            sev = sev + (mtn - sev) * t
        EndIf
    EndIf

    float weatherM = GV(gWeatherClear, 100.0) / 100.0
    If wcls == 3
        weatherM = GV(gWeatherSnow, 200.0) / 100.0
    ElseIf wcls == 2
        weatherM = GV(gWeatherRain, 200.0) / 100.0
    ElseIf wcls == 1
        weatherM = GV(gWeatherCloudy, 150.0) / 100.0
    EndIf
    sev *= weatherM
    sev *= nightM
    sev *= swimM

    If nearFire
        sev *= GV(gFireMult, 40.0) / 100.0
    EndIf

    If sev < 0.0
        sev = 0.0
    EndIf
    return sev
EndFunction

; Flat severity base of the current hold. Cached by location FormID; walks up
; the parent chain via PO3.
float Function RegionBase()
    Location here = pl.GetCurrentLocation()
    string cacheKey = "_RSL_RegionCache"
    string locKey   = "_RSL_HoldLocId"

    If !here
        return StorageUtil.GetFloatValue(pl, cacheKey, GV(gRegionDefault, 20.0))
    EndIf
    int hereId = here.GetFormID()
    If hereId == StorageUtil.GetIntValue(pl, locKey, -1)
        return StorageUtil.GetFloatValue(pl, cacheKey, GV(gRegionDefault, 20.0))
    EndIf
    StorageUtil.SetIntValue(pl, locKey, hereId)

    int idWin = LocId(_RSL_Forms.LocWinterhold())
    int idPal = LocId(_RSL_Forms.LocPale())
    int idEas = LocId(_RSL_Forms.LocEastmarch())
    int idHja = LocId(_RSL_Forms.LocHjaalmarch())
    int idHaa = LocId(_RSL_Forms.LocHaafingar())
    int idRea = LocId(_RSL_Forms.LocReach())
    int idWhi = LocId(_RSL_Forms.LocWhiterun())
    int idFal = LocId(_RSL_Forms.LocFalkreath())
    int idRif = LocId(_RSL_Forms.LocRift())

    float base = GV(gRegionDefault, 20.0)
    Location loc = here
    int guard = 0
    string matched = "default"
    string chain = ""
    While loc && guard < 15
        int lid = loc.GetFormID()
        chain += " " + loc
        If lid == idWin
            base = GV(gRegionWinterhold, 40.0)
            matched = "Winterhold"
        ElseIf lid == idPal
            base = GV(gRegionPale, 40.0)
            matched = "Pale"
        ElseIf lid == idEas
            base = GV(gRegionEastmarch, 40.0)
            matched = "Eastmarch"
        ElseIf lid == idHja
            base = GV(gRegionHjaalmarch, 20.0)
            matched = "Hjaalmarch"
        ElseIf lid == idHaa
            base = GV(gRegionHaafingar, 20.0)
            matched = "Haafingar"
        ElseIf lid == idRea
            base = GV(gRegionReach, 20.0)
            matched = "Reach"
        ElseIf lid == idWhi
            base = GV(gRegionWhiterun, 10.0)
            matched = "Whiterun"
        ElseIf lid == idFal
            base = GV(gRegionFalkreath, 10.0)
            matched = "Falkreath"
        ElseIf lid == idRif
            base = GV(gRegionRift, 10.0)
            matched = "Rift"
        EndIf

        If matched != "default"
            loc = None
        Else
            loc = PO3_SKSEFunctions.GetParentLocation(loc)
            guard += 1
        EndIf
    EndWhile

    StorageUtil.SetFloatValue(pl, cacheKey, base)
    _RSL_Log.W("RegionBase: here=" + here + " chain=[" + chain + " ] -> " + matched + " " + base)
    return base
EndFunction

int Function LocId(Location l)
    If l
        return l.GetFormID()
    EndIf
    return -1
EndFunction

; True in an ice cave / glacial ruin (the ColdInteriors FLST - same set CC
; Survival Mode uses). Walks the location parent chain. Cached by current
; location id so the walk runs only on a location change, not every tick.
bool Function ColdInteriorHere()
    If !coldInteriors
        return false
    EndIf
    Location here = pl.GetCurrentLocation()
    int hereId = -1
    If here
        hereId = here.GetFormID()
    EndIf
    If hereId == StorageUtil.GetIntValue(pl, "_RSL_ColdIntLoc", -2)
        return StorageUtil.GetIntValue(pl, "_RSL_ColdIntVal", 0) > 0
    EndIf
    StorageUtil.SetIntValue(pl, "_RSL_ColdIntLoc", hereId)

    bool cold = false
    Location loc = here
    int guard = 0
    While loc && guard < 15 && !cold
        If coldInteriors.HasForm(loc)
            cold = true
        EndIf
        loc = PO3_SKSEFunctions.GetParentLocation(loc)
        guard += 1
    EndWhile
    StorageUtil.SetIntValue(pl, "_RSL_ColdIntVal", cold as int)
    _RSL_Log.W("ColdInteriorHere: loc=" + here + " -> " + cold)
    return cold
EndFunction

; True if the current region has a snow-class weather in its table, i.e. it
; CAN snow here (whether or not the sky is clear right now) -> cold climate.
; Weather.FindWeather(3) is exactly what RFAB's Apocalypse "Control Weather"
; spell checks to tell region-appropriate weather from forced weather. Cached
; by parent cell (region is position-based; the cell is a stable-enough key).
bool Function SnowClimateHere()
    Cell c = pl.GetParentCell()
    If c && c.IsInterior()
        return false          ; FindWeather is region-based; meaningless indoors
    EndIf
    int cid = 0
    If c
        cid = c.GetFormID()
    EndIf
    If cid == StorageUtil.GetIntValue(pl, "_RSL_SnowClimCell", -1)
        return StorageUtil.GetIntValue(pl, "_RSL_SnowClimVal", 0) > 0
    EndIf
    StorageUtil.SetIntValue(pl, "_RSL_SnowClimCell", cid)
    bool snow = (Weather.FindWeather(3) != None)
    StorageUtil.SetIntValue(pl, "_RSL_SnowClimVal", snow as int)
    _RSL_Log.W("SnowClimateHere: cell=" + c + " -> " + snow)
    return snow
EndFunction

; Mitigation = clothing + resist, capped at 200.
;   clothing = slots(0..4) * WarmthPerSlot * Wetness
;   resist   = FrostResist * ResistWeight/100
; AV is "FrostResist" (NOT "ResistFrost" - that silently returns 0). Lord
; Stone = 25, Acclimatization perk = +25. Target: 4*7 + 25*0.5 = 40.5.
; A common cold multiplies the whole result down (0.85 / 0.75 / 0.60).
float Function Mitigation()
    float m = WarmthPoints() * WetnessFactor()
    m += pl.GetActorValue("FrostResist") * (GV(gResistWeight, 50.0) / 100.0)
    m *= ColdDiseaseMitMult()
    If m > 200.0
        m = 200.0
    EndIf
    return m
EndFunction

; Accumulator, not a grace timer. When Severity is below Mitigation the delta
; goes negative - the character warms up.
Function AdvanceCold(float dtHours)
    ; No charge during sleep - OnSleepStop adds the slept hours in one go.
    If StorageUtil.GetFloatValue(pl, K_SLEEPING, 0.0) > 0.5
        return
    EndIf

    ; Cooling/warming rate is proportional to the DIFFERENCE of environment
    ; severity and mitigation. A negative resist causes heat loss even at
    ; sev=0 (a fire cancels severity but gives no surplus heat) - intentional:
    ; the cursed need clothing, a fire is not enough.
    float delta = (Severity() - Mitigation()) * dtHours * gColdRate.GetValue()

    ; Warm-up faster than cooling (game pace): delta < 0 -> multiply.
    If delta < 0.0
        delta *= GV(gWarmupMult, 3.0)
    EndIf

    float v = StorageUtil.GetFloatValue(pl, K_COLD, 0.0) + delta
    If v < 0.0
        v = 0.0
    ElseIf v > 100.0
        v = 100.0
    EndIf
    StorageUtil.SetFloatValue(pl, K_COLD, v)

    UpdateColdVisual(v)
EndFunction

; Character ice shader above the threshold. MCM toggle. State in StorageUtil
; (survives save load); force-cleared in OnEffectFinish/TeardownAll. Screen
; ISM was dropped: no vanilla IMAD holds visually as "frost".
Function UpdateColdVisual(float v)
    float thr = GV(_RSL_Forms.ColdVisualThreshold(), 50.0)
    bool want = v > thr

    bool shOn = StorageUtil.GetIntValue(pl, "_RSL_SHon", 0) > 0
    bool shWant = want && GV(_RSL_Forms.ColdVisualShader(), 0.0) > 0.5
    If shWant && !shOn
        EffectShader sh = _RSL_Forms.FxColdShader()
        If sh
            sh.Play(pl)
            StorageUtil.SetIntValue(pl, "_RSL_SHon", 1)
        EndIf
    ElseIf !shWant && shOn
        EffectShader sh2 = _RSL_Forms.FxColdShader()
        If sh2
            sh2.Stop(pl)
        EndIf
        StorageUtil.SetIntValue(pl, "_RSL_SHon", 0)
    EndIf
EndFunction

Function ClearColdVisual()
    If !pl
        pl = Game.GetPlayer()
    EndIf
    EffectShader sh = _RSL_Forms.FxColdShader()
    If sh
        sh.Stop(pl)
    EndIf
    StorageUtil.SetIntValue(pl, "_RSL_SHon", 0)
EndFunction

; 1 - AV/100, clamped [0, 2]: full resist -> 0 (no effect), no resist -> 1,
; weakness -> amplified up to 2x. Same idea Mitigation() already uses for cold.
float Function ResistFactor(string av)
    float f = 1.0 - pl.GetActorValue(av) * 0.01
    If f < 0.0
        f = 0.0
    ElseIf f > 2.0
        f = 2.0
    EndIf
    return f
EndFunction

; An elemental damage effect landed on the player. Two systems, one rate-limit
; gate (ELEM_MIN_GAP real seconds - a frost cloak / channelled stream fires this
; event many times a second):
;   - cold bar: frost raises, fire lowers, scaled by Frost/FireResist. Queued
;     (K_ELEMACC), folded on the tick by ApplyElemHits so K_COLD has one writer.
;     Fire-warming is intentionally usable - burning costs HP, a real trade.
;   - elemental lesions: frost/fire/shock all damage P, scaled by the matching
;     resist. Queued (K_ELPACC), folded by AdvanceElemLesion.
float ELEM_MIN_GAP = 0.5

; 0 none / 1 frost / 2 fire / 3 shock. Vanilla keyword first; then, for a
; Detrimental effect, the resistance AV on the MGEF - RFAB's destruction
; effects can drop the MagicDamage* keyword but still resist to the element.
int Function ElemKind(MagicEffect e)
    If !e
        return 0
    EndIf
    If kwMagicDamageFrost && e.HasKeyword(kwMagicDamageFrost)
        return 1
    ElseIf kwMagicDamageFire && e.HasKeyword(kwMagicDamageFire)
        return 2
    ElseIf kwMagicDamageShock && e.HasKeyword(kwMagicDamageShock)
        return 3
    EndIf
    If e.IsEffectFlagSet(0x00000004)       ; Detrimental
        string r = e.GetResistance()
        If r == "ResistFrost" || r == "FrostResist"
            return 1
        ElseIf r == "ResistFire" || r == "FireResist"
            return 2
        ElseIf r == "ResistShock" || r == "ShockResist" || r == "ElectricResist"
            return 3
        EndIf
    EndIf
    return 0
EndFunction

; First elemental (frost/fire/shock) effect carried by a hit source - a Spell
; (projectile / rune / cloak), an Enchantment, or a Weapon's enchantment.
; Used by OnHit as a fallback when OnMagicEffectApply does not deliver the hit.
MagicEffect Function ElemSourceEffect(Form src)
    If !src
        return None
    EndIf
    Spell sp = src as Spell
    Enchantment en = src as Enchantment
    If !en
        Weapon wp = src as Weapon
        If wp
            en = wp.GetEnchantment()
        EndIf
    EndIf

    int n = 0
    If sp
        n = sp.GetNumEffects()
    ElseIf en
        n = en.GetNumEffects()
    EndIf
    int i = 0
    While i < n
        MagicEffect e = None
        If sp
            e = sp.GetNthEffectMagicEffect(i)
        Else
            e = en.GetNthEffectMagicEffect(i)
        EndIf
        If ElemKind(e) != 0
            return e
        EndIf
        i += 1
    EndWhile
    return None
EndFunction

Function NoteElemHit(MagicEffect eff)
    If gModEnabled.GetValue() < 0.5
        return
    EndIf

    float rf = 0.0          ; ResistFactor for the matching element
    float coldD = 0.0       ; cold-bar nudge (frost +, fire -); 0 for shock
    int kind = ElemKind(eff)
    If kind == 1
        rf = ResistFactor("FrostResist")
        coldD = GV(gFrostHitCold, 2.0) * rf
    ElseIf kind == 2
        rf = ResistFactor("FireResist")
        coldD = -GV(gFireHitWarm, 2.0) * rf
    ElseIf kind == 3
        rf = ResistFactor("ElectricResist")   ; RFAB's shock resist AV; shock feeds lesions only, not the cold bar
    Else
        return
    EndIf

    ; Rate limit: one accepted hit per ELEM_MIN_GAP. Utility.GetCurrentRealTime
    ; resets to ~0 each game launch but K_ELEMT persists in StorageUtil - a stale
    ; value from a prior session sits in the future, so only gate when the last
    ; stamp is actually in the past (else every hit was silently dropped forever).
    float now = Utility.GetCurrentRealTime()
    float lastT = StorageUtil.GetFloatValue(pl, K_ELEMT, 0.0)
    If lastT <= now && now - lastT < ELEM_MIN_GAP
        return
    EndIf
    StorageUtil.SetFloatValue(pl, K_ELEMT, now)

    If coldD != 0.0
        float acc = StorageUtil.GetFloatValue(pl, K_ELEMACC, 0.0) + coldD
        If acc > 20.0
            acc = 20.0
        ElseIf acc < -20.0
            acc = -20.0
        EndIf
        StorageUtil.SetFloatValue(pl, K_ELEMACC, acc)
    EndIf

    If GV(gElemLesionEnabled, 1.0) >= 0.5
        float pd = StorageUtil.GetFloatValue(pl, K_ELPACC, 0.0) - GV(gElemLesionHitP, 4.0) * rf
        If pd < -40.0
            pd = -40.0
        ElseIf pd > 40.0
            pd = 40.0
        EndIf
        StorageUtil.SetFloatValue(pl, K_ELPACC, pd)
    EndIf

    _RSL_Log.W("elemHit: " + eff.GetName() + " coldD=" + coldD + " rf=" + rf)
EndFunction

; "Чистая льняная ткань" [RFAB] (RFAB_Bandage) consumed - nudge elemental-lesion
; P up. Queued into K_ELPACC (shared with the hit damage), folded on the tick.
Function NoteBandage()
    If GV(gElemLesionEnabled, 1.0) < 0.5
        return
    EndIf
    If _RSL_Disease.GetStage(pl, "EL") <= 0 && StorageUtil.GetFloatValue(pl, "_RSL_Dz_EL_Prog", 0.0) >= 0.0
        return          ; nothing to patch
    EndIf
    float pd = StorageUtil.GetFloatValue(pl, K_ELPACC, 0.0) + GV(gElemLesionBandageP, 10.0)
    If pd > 40.0
        pd = 40.0
    ElseIf pd < -40.0
        pd = -40.0
    EndIf
    StorageUtil.SetFloatValue(pl, K_ELPACC, pd)
    _RSL_Log.W("bandage: EL P acc -> " + pd)
EndFunction

; Fold the queued frost/fire nudge into the cold bar. The rate limit + accum
; clamp in NoteElemHit already bound it (at most ~4/tick), so fold it whole.
Function ApplyElemHits()
    float acc = StorageUtil.GetFloatValue(pl, K_ELEMACC, 0.0)
    If acc == 0.0
        return
    EndIf
    float v = StorageUtil.GetFloatValue(pl, K_COLD, 0.0) + acc
    If v < 0.0
        v = 0.0
    ElseIf v > 100.0
        v = 100.0
    EndIf
    StorageUtil.SetFloatValue(pl, K_COLD, v)
    StorageUtil.SetFloatValue(pl, K_ELEMACC, 0.0)
EndFunction

; --- disease: common cold ----------------------------------------------
; Stages: Простуда -> Тяжёлая простуда -> Грипп (Disease-type SPEL).
; Effect: flat -10% max Magicka for every stage (placeholder until the RFAB
; stat pass), plus the escalating Mitigation multiplier (ColdDiseaseMitMult)
; that makes cold worse. NEVER touches Health.
; Contract: rolls once per game-hour; chance rises linearly from
; ColdColdChanceMin at the threshold to ColdColdChanceMax at ColdColdChanceMaxAt
; cold, then x (1 - DiseaseResist/100).
; Progression / decay: game-time, DiseaseProgressHours / DiseaseDecayHours (24
; each). Progression only while cold > threshold; decay only while cold <= it.

; Sleep-efficiency multiplier for brown rot (draugr disease): rotting flesh
; rests poorly. Applied to the SLEEP-counter reduction in OnSleepStop.
float Function BrSleepMult()
    int s = _RSL_Disease.GetStage(pl, "BR")
    If s >= 3
        return 0.7
    ElseIf s == 2
        return 0.8
    ElseIf s == 1
        return 0.9
    EndIf
    return 1.0
EndFunction

; Hunger-accrual multiplier for gutworm: parasites burn through you faster.
; Applied to the HUNGER delta in AdvanceHunger.
float Function GwHungerMult()
    int s = _RSL_Disease.GetStage(pl, "GW")
    If s >= 3
        return 2.5
    ElseIf s == 2
        return 1.7
    ElseIf s == 1
        return 1.3
    EndIf
    return 1.0
EndFunction

; %/game-hour contract chance at the given cold level.
float Function CCContractChance(float cold)
    float thr = GV(gColdColdThreshold, 50.0)
    If cold < thr
        return 0.0
    EndIf
    float lo   = GV(gColdColdChanceMin, 10.0)
    float hi   = GV(gColdColdChanceMax, 90.0)
    float full = GV(gColdColdChanceMaxAt, 90.0)
    float c = hi
    If cold < full && full > thr
        c = lo + (cold - thr) / (full - thr) * (hi - lo)
    EndIf
    c *= (1.0 - pl.GetActorValue("DiseaseResist") * 0.01)
    If c < 0.0
        c = 0.0
    EndIf
    return c
EndFunction

Spell Function CCStageSpell(int s)
    If s == 1
        return sCC1
    ElseIf s == 2
        return sCC2
    ElseIf s == 3
        return sCC3
    EndIf
    return None
EndFunction

; Multiplicative cold-tolerance penalty; checked via HasSpell so it self-clears
; the moment a cure removes the disease.
float Function ColdDiseaseMitMult()
    If sCC3 && pl.HasSpell(sCC3)
        return 0.5
    ElseIf sCC2 && pl.HasSpell(sCC2)
        return 0.7
    ElseIf sCC1 && pl.HasSpell(sCC1)
        return 0.85
    EndIf
    return 1.0
EndFunction

Function CCNotify(Message m)
    If m
        m.Show()
    EndIf
EndFunction

Function CCSetStage(int s)
    int old = _RSL_Disease.GetStage(pl, "CC")
    _RSL_Disease.SetStage(pl, "CC", s, CCStageSpell(old), CCStageSpell(s), \
                          0.0, 0.0)
EndFunction

Function ClearColdDisease()
    _RSL_Disease.ClearStages(pl, "CC", sCC1, sCC2, sCC3)
EndFunction

; MCM "Вылечить болезни" button - full clear of every disease (debug).
Function CureColdDisease() global
    Actor p = Game.GetPlayer()
    _RSL_Disease.ClearStages(p, "CC", _RSL_Forms.DiseaseColdCommon1(), \
        _RSL_Forms.DiseaseColdCommon2(), _RSL_Forms.DiseaseColdCommon3())
    _RSL_Disease.ClearStages(p, "BR", _RSL_Forms.DiseaseBrownRot1(), \
        _RSL_Forms.DiseaseBrownRot2(), _RSL_Forms.DiseaseBrownRot3())
    _RSL_Disease.ClearStages(p, "GW", _RSL_Forms.DiseaseGutworm1(), \
        _RSL_Forms.DiseaseGutworm2(), _RSL_Forms.DiseaseGutworm3())
    _RSL_Disease.ClearStages(p, "GS", _RSL_Forms.DiseaseGreenspore1(), \
        _RSL_Forms.DiseaseGreenspore2(), _RSL_Forms.DiseaseGreenspore3())
    _RSL_Disease.ClearStages(p, "FP", _RSL_Forms.DiseaseFoodPoison1(), \
        _RSL_Forms.DiseaseFoodPoison2(), _RSL_Forms.DiseaseFoodPoison3())
    _RSL_Disease.ClearStages(p, "EL", _RSL_Forms.DiseaseElemLesion1(), \
        _RSL_Forms.DiseaseElemLesion2(), _RSL_Forms.DiseaseElemLesion3())
    _RSL_Disease.ClearStages(p, "HY", _RSL_Forms.AbHypo1(), \
        _RSL_Forms.AbHypo2(), _RSL_Forms.AbHypo3())
    ClearAllRfabWraps(p)

    ; queued elemental-hit accumulators + hypothermia lockdown / rest-block
    StorageUtil.SetFloatValue(p, "_RSL_ElemAccum", 0.0)     ; K_ELEMACC
    StorageUtil.SetFloatValue(p, "_RSL_Dz_EL_PAcc", 0.0)    ; K_ELPACC
    StorageUtil.SetFloatValue(p, "_RSL_ElemLastT", 0.0)     ; K_ELEMT
    p.SetActorValue("Paralysis", 0.0)
    Game.SetInChargen(false, false, false)
    StorageUtil.SetIntValue(p, "_RSL_HypoWaitBlocked", 0)   ; K_HYWAIT
    Game.EnablePlayerControls()

    _RSL_Log.W("CureColdDisease: all diseases cleared (MCM button)")
EndFunction

; Clear every RFAB-wrapper stage, base spell included. Used by the MCM "cure
; all" button and TeardownAll - both want a full wipe, not a one-stage step.
Function ClearAllRfabWraps(Actor p) global
    _RSL_Disease.ClearStages(p, "AT",  _RSL_Forms.RfabDzAT(),  _RSL_Forms.DzAT2(),  _RSL_Forms.DzAT3())
    _RSL_Disease.ClearStages(p, "RJ",  _RSL_Forms.RfabDzRJ(),  _RSL_Forms.DzRJ2(),  _RSL_Forms.DzRJ3())
    _RSL_Disease.ClearStages(p, "WB",  _RSL_Forms.RfabDzWB(),  _RSL_Forms.DzWB2(),  _RSL_Forms.DzWB3())
    _RSL_Disease.ClearStages(p, "RA",  _RSL_Forms.RfabDzRA(),  _RSL_Forms.DzRA2(),  _RSL_Forms.DzRA3())
    _RSL_Disease.ClearStages(p, "BF",  _RSL_Forms.RfabDzBF(),  _RSL_Forms.DzBF2(),  _RSL_Forms.DzBF3())
    _RSL_Disease.ClearStages(p, "BRR", _RSL_Forms.RfabDzBRR(), _RSL_Forms.DzBRR2(), _RSL_Forms.DzBRR3())
    _RSL_Disease.ClearStages(p, "DR",  _RSL_Forms.RfabDzDR(),  _RSL_Forms.DzDR2(),  _RSL_Forms.DzDR3())
EndFunction

; A Cure-Disease effect (potion / shrine / Restoration spell) knocks EVERY
; active disease back one stage, not straight to healthy. Counted here,
; consumed by each disease's tick (avoids racing the engine's "strip all
; Disease spells" with our re-add in the same frame).
Event OnMagicEffectApply(ObjectReference akCaster, MagicEffect akEffect)
    If !ready || !akEffect
        return
    EndIf

    If StorageUtil.GetIntValue(None, "_RSL_DbgLog", 0) > 0
        _RSL_Log.W("MGEFApply: " + akEffect.GetName() + " (" + akEffect.GetFormID() \
            + ") kind=" + ElemKind(akEffect) + " resAV=" + akEffect.GetResistance() \
            + " caster=" + akCaster)
    EndIf

    NoteElemHit(akEffect)

    If !_RSL_Forms.IsCureEffect(akEffect as Form)
        return
    EndIf
    _RSL_Log.W("Dz: cure counted (" + akEffect.GetName() + ")")
    If _RSL_Disease.GetStage(pl, "CC") > 0
        _RSL_Disease.AddCure(pl, "CC")
    EndIf
    If _RSL_Disease.GetStage(pl, "EL") > 0
        _RSL_Disease.AddCure(pl, "EL")
    EndIf
    int i = 0
    While i < 4
        If _RSL_Disease.GetStage(pl, hdId[i]) > 0
            _RSL_Disease.AddCure(pl, hdId[i])
        EndIf
        i += 1
    EndWhile
    int j = 0
    While j < 7
        If _RSL_Disease.GetStage(pl, rdId[j]) > 0
            _RSL_Disease.AddCure(pl, rdId[j])
        EndIf
        j += 1
    EndWhile
EndEvent

; --- OnHit diseases: brown rot / gutworm / greenspore -------------------

Event OnHit(ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked)
    If !ready
        return
    EndIf
    Actor agg = akAggressor as Actor
    _RSL_Log.W("OnHit: agg=" + akAggressor + " actor=" + agg + " src=" + akSource + " proj=" + akProjectile)

    ; Elemental damage from a spell / rune / enchanted weapon - a fallback for
    ; when OnMagicEffectApply does not deliver the hit (RFAB scripted damage,
    ; keyword-stripped MGEFs). NoteElemHit's own 0.5s gate de-dupes.
    MagicEffect em = ElemSourceEffect(akSource)
    If em
        NoteElemHit(em)
    EndIf

    If !agg || akProjectile
        return
    EndIf
    Race rc = agg.GetRace()
    If rc == raceDraugr
        HitContract(0, rc)
    ElseIf kwTroll && rc && rc.HasKeyword(kwTroll)
        HitContract(1, rc)
    ElseIf rc == raceSlaughterfish
        HitContract(2, rc)
    EndIf
EndEvent

Function HitContract(int i, Race rc)
    If !hdS1[i] || _RSL_Disease.GetStage(pl, hdId[i]) > 0
        return
    EndIf
    If gModEnabled.GetValue() < 0.5 || GV(gDiseaseEnabled, 1.0) < 0.5 || pl.HasKeyword(kwUndead)
        return
    EndIf
    float chance = GV(gDiseaseHitChance, 100.0) * (1.0 - pl.GetActorValue("DiseaseResist") * 0.01)
    If chance > 0.0 && Utility.RandomFloat(0.0, 100.0) < chance
        _RSL_Disease.SetStage(pl, hdId[i], 1, None, hdS1[i], 0.0, 0.0)
        _RSL_Disease.ResetP(pl, hdId[i])
        Message mm = hdM1[i]
        If mm
            mm.Show()
        EndIf
        _RSL_Log.W("Dz " + hdId[i] + ": contracted on hit (chance " + chance + ", race " + rc + ")")
    EndIf
EndFunction

Spell Function HitStageSpell(int i, int s)
    If s == 1
        return hdS1[i]
    ElseIf s == 2
        return hdS2[i]
    ElseIf s == 3
        return hdS3[i]
    EndIf
    return None
EndFunction

Function AdvanceHitDz(int i, bool undead, float dtHours)
    Spell s1 = hdS1[i]
    If !ready || !s1
        return
    EndIf
    string id = hdId[i]
    int stage = _RSL_Disease.GetStage(pl, id)

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gDiseaseEnabled, 1.0) >= 0.5
    If !on || undead
        If stage > 0
            _RSL_Disease.ClearStages(pl, id, s1, hdS2[i], hdS3[i])
        EndIf
        return
    EndIf
    If stage <= 0
        return          ; contract is OnHit-driven
    EndIf

    Spell cur = HitStageSpell(i, stage)

    int cures = _RSL_Disease.TakeCures(pl, id)
    If cures == 0 && !(cur && pl.HasSpell(cur))
        cures = 1
    EndIf
    If cures > 0
        int t = stage - cures
        If t < 0
            t = 0
        EndIf
        _RSL_Disease.SetStage(pl, id, t, cur, HitStageSpell(i, t), 0.0, 0.0)
        _RSL_Disease.HalveP(pl, id)
        If t == 0 && hdMC[i]
            hdMC[i].Show()
        EndIf
        _RSL_Log.W("Dz " + id + ": " + cures + " cure(s), stage " + stage + " -> " + t)
        return
    EndIf

    int net = _RSL_Disease.StepP(pl, id, DzAnyAxisBad(undead), dtHours, \
        pl.GetActorValue("DiseaseResist"), GV(gDiseaseProgressHours, 24.0), \
        GV(gDiseaseDecayHours, 24.0))
    If net != 0
        int t2 = stage - net
        If t2 < 0
            t2 = 0
        ElseIf t2 > 3
            t2 = 3
        EndIf
        If t2 != stage
            _RSL_Disease.SetStage(pl, id, t2, cur, HitStageSpell(i, t2), 0.0, 0.0)
            If t2 == 0 && hdMC[i]
                hdMC[i].Show()
            ElseIf t2 > stage && t2 == 3 && hdM3[i]
                hdM3[i].Show()
            ElseIf t2 > stage && hdM2[i]
                hdM2[i].Show()
            EndIf
        EndIf
    EndIf
EndFunction

; --- RFAB disease wrappers (id AT/RJ/WB/RA/BF/BRR/DR) -------------------
; Stage 1 = rdBase[i] (RFAB_Disease_X, contracted via its RACE ATKD attack
; spell - we don't gate that, DiseaseResist already does). Stages 2/3 = rd2/rd3
; (1:1 copies). Progression swaps base<->rd2<->rd3; a cure walks one stage back.
; All three are Type=Disease so the engine strips the current one on a cure -
; OnMagicEffectApply counts it, this re-adds the stage below (like the others).

Spell Function RdStageSpell(int i, int s)
    If s == 1
        return rdBase[i]
    ElseIf s == 2
        return rd2[i]
    ElseIf s == 3
        return rd3[i]
    EndIf
    return None
EndFunction

Function AdvanceRfabDz(int i, bool undead, float dtHours)
    Spell base = rdBase[i]
    MagicEffect mark = rdMark[i]
    If !ready || !base
        return
    EndIf
    string id = rdId[i]
    int stage = _RSL_Disease.GetStage(pl, id)

    ; A creature bite applies the disease's EFFECTS without the SPEL entering the
    ; spell list, so HasSpell misses it - detect via HasMagicEffect on the marker
    ; (the first, unconditional debuff). On contract we adopt it with a silent
    ; AddSpell(base, false); from then on it behaves like our own diseases.
    bool afflicted = (mark && pl.HasMagicEffect(mark)) || pl.HasSpell(base)

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gRfabDzEnabled, 1.0) >= 0.5
    If !on || undead
        If stage > 0
            _RSL_Disease.ClearStages(pl, id, base, rd2[i], rd3[i])
            pl.DispelSpell(base)
        EndIf
        return
    EndIf

    ; contract: adopt the engine-applied disease into the spell list. After a
    ; cure the marker effect can linger a tick or two - require it to clear once
    ; (the "seen clean" flag) before a fresh bite counts, so a cure can't loop
    ; straight back into stage 1.
    If stage == 0
        string seenKey = "_RSL_Dz_" + id + "_Clean"
        If !afflicted
            StorageUtil.SetIntValue(pl, seenKey, 1)
        ElseIf StorageUtil.GetIntValue(pl, seenKey, 1) == 1
            pl.AddSpell(base, false)
            StorageUtil.SetIntValue(pl, seenKey, 0)
            _RSL_Disease.SetStage(pl, id, 1, None, None, 0.0, 0.0)
            _RSL_Disease.ResetP(pl, id)
            _RSL_Log.W("rfabDz " + id + ": contracted (mark), base adopted")
        EndIf
        return
    EndIf

    ; at stage 2/3 the base spell was swapped out for our rd2/rd3 copy. A fresh
    ; bite re-applies RFAB's own effects (source = base) on top - dispel those;
    ; our copy (source = rd2/rd3) is untouched.
    If stage >= 2
        pl.DispelSpell(base)
    EndIf

    Spell cur = RdStageSpell(i, stage)

    ; cure: engine strips our current Type=Disease spell; counted in
    ; OnMagicEffectApply. Fallbacks for an uncounted external cure.
    int cures = _RSL_Disease.TakeCures(pl, id)
    If cures == 0 && stage == 1 && !afflicted
        cures = 1
    ElseIf cures == 0 && stage >= 2 && !(cur && pl.HasSpell(cur))
        cures = 1
    EndIf
    If cures > 0
        int t = stage - cures
        If t < 0
            t = 0
        EndIf
        _RSL_Disease.SetStage(pl, id, t, cur, RdStageSpell(i, t), 0.0, 0.0)
        _RSL_Disease.HalveP(pl, id)
        If t == 0
            pl.DispelSpell(base)
            If rdMC[i]
                rdMC[i].Show()
            EndIf
        EndIf
        _RSL_Log.W("rfabDz " + id + ": " + cures + " cure(s), stage " + stage + " -> " + t)
        return
    EndIf

    ; progression / regression via P. Good living heals it all the way out
    ; (stage 1 -> 0 removes the base RFAB disease), same as our own diseases.
    int net = _RSL_Disease.StepP(pl, id, DzAnyAxisBad(undead), dtHours, \
        pl.GetActorValue("DiseaseResist"), GV(gDiseaseProgressHours, 24.0), \
        GV(gDiseaseDecayHours, 24.0))
    If net != 0
        int t2 = stage - net
        If t2 < 0
            t2 = 0
        ElseIf t2 > 3
            t2 = 3
        EndIf
        If t2 != stage
            _RSL_Disease.SetStage(pl, id, t2, cur, RdStageSpell(i, t2), 0.0, 0.0)
            If t2 == 0
                pl.DispelSpell(base)
                If rdMC[i]
                    rdMC[i].Show()
                EndIf
            ElseIf t2 > stage && t2 == 3 && rdM3[i]
                rdM3[i].Show()
            ElseIf t2 > stage && rdM2[i]
                rdM2[i].Show()
            EndIf
        EndIf
    EndIf
EndFunction

; --- hypothermia (id "HY"): an Ability, not a disease --------------------
; P is a deterministic 0..100 bar (per _RSL_Disease.StepLinear): cold >= the
; threshold fills it (+100 over HypWorsenHours) -> stage+1; cold <= RecoverThr
; drains it (-100 over HypRecoverHours) -> stage-1; between, frozen. No roll,
; no DiseaseResist, cures do NOT touch it - only warmth. Stage 3 = paralysis +
; REQ_Ability_DisableHealthRegen + HypDrainPerSec HP/sec until death.

Spell Function HypoStageSpell(int s)
    If s == 1
        return sHY1
    ElseIf s == 2
        return sHY2
    ElseIf s == 3
        return sHY3
    EndIf
    return None
EndFunction

Message Function HypoMsg(int s)
    If s == 1
        return msgHY1
    ElseIf s == 2
        return msgHY2
    ElseIf s == 3
        return msgHY3
    EndIf
    return None
EndFunction

; Stage-3 lockdown. The "Paralysis" actor value gates get-up (a paralyzed actor
; cannot stand) but does NOT knock a standing/running actor down by itself - so
; pair it with ONE PushActorAway to start the ragdoll, then the AV holds the
; body on the ground. No repeated pulse (that jerked the body every tick, the
; "convulsions"). DisablePlayerControls kills movement/fighting/menu/activate on
; top - camera stays free, the player just watches themselves freeze. `nudge`
; fires one GetUpBegin on release, or the player can stay stuck down.
Function HypoSetLock(bool on, bool nudge)
    If on
        pl.SetActorValue("Paralysis", 1.0)
        pl.PushActorAway(pl, 3.0)
        Game.DisablePlayerControls(true, true, false, false, true, true, true, false)
    Else
        pl.SetActorValue("Paralysis", 0.0)
        Game.EnablePlayerControls()
        If nudge
            Debug.SendAnimationEvent(pl, "GetUpBegin")
        EndIf
    EndIf
EndFunction

; Wait/sleep is blocked at HY stage >= 1 via Game.SetInChargen(_, true, _) - the
; same lever the engine uses in combat/chargen. EXCEPTION: indoors and warming
; (Severity < Mitigation) - a warm shelter is where you sleep it off. Re-checked
; every tick so it follows the player in and out of shelter. K_HYWAIT tracks the
; current state so SetInChargen is only called on a change.
Function SyncHypoWait()
    If !ready
        return
    EndIf
    bool wantBlock = false
    If _RSL_Disease.GetStage(pl, "HY") >= 1
        Cell c = pl.GetParentCell()
        bool warmingIndoors = c && c.IsInterior() && (Severity() < Mitigation())
        wantBlock = !warmingIndoors
    EndIf
    bool isBlocked = StorageUtil.GetIntValue(pl, K_HYWAIT, 0) > 0
    If wantBlock != isBlocked
        Game.SetInChargen(false, wantBlock, false)
        StorageUtil.SetIntValue(pl, K_HYWAIT, wantBlock as int)
        _RSL_Log.W("Hypothermia: rest blocked = " + wantBlock)
    EndIf
EndFunction

Function HypoSetStage(int s)
    int old = _RSL_Disease.GetStage(pl, "HY")
    _RSL_Disease.SetStage(pl, "HY", s, HypoStageSpell(old), HypoStageSpell(s), \
                          0.0, 0.0)
    ; stage 3: lock the player down and start the accelerating drain timer.
    If s >= 3 && old < 3
        StorageUtil.SetFloatValue(pl, "_RSL_Dz_HY_S3", 0.0)
        HypoSetLock(true, false)
    ElseIf s < 3 && old >= 3
        StorageUtil.UnsetFloatValue(pl, "_RSL_Dz_HY_S3")
        HypoSetLock(false, true)
    ElseIf s < 3
        StorageUtil.UnsetFloatValue(pl, "_RSL_Dz_HY_S3")
    EndIf

    SyncHypoWait()   ; wait/sleep block, unless warming indoors

    If s == 0
        CCNotify(msgHYCured)
    ElseIf s > old
        CCNotify(HypoMsg(s))
    EndIf
    _RSL_Log.W("Hypothermia: stage " + old + " -> " + s)
EndFunction

Function AdvanceHypothermia(float dtHours, bool undead)
    If !ready || !sHY1
        return          ; records not in the plugin yet (pre-regen)
    EndIf
    int stage = _RSL_Disease.GetStage(pl, "HY")

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gHypEnabled, 1.0) >= 0.5
    If !on || undead
        If stage > 0
            _RSL_Disease.ClearStages(pl, "HY", sHY1, sHY2, sHY3)
            HypoSetLock(false, stage >= 3)
            SyncHypoWait()
        EndIf
        return
    EndIf

    float cold = StorageUtil.GetFloatValue(pl, K_COLD, 0.0)
    float thr  = GV(gHypThreshold, 90.0)

    ; instant onset: cold >= threshold at stage 0 -> straight to stage 1
    If stage == 0
        If cold >= thr
            HypoSetStage(1)
            _RSL_Disease.ResetP(pl, "HY")
        EndIf
        return
    EndIf

    ; stages 1..3: P accumulator - fill toward the next stage while still cold,
    ; drain toward recovery once warm, frozen in between.
    float drift = 0.0
    If cold >= thr
        drift = 100.0 / GV(gHypWorsenHours, 2.0)
    ElseIf cold <= GV(gHypRecoverThr, 25.0)
        drift = -(100.0 / GV(gHypRecoverHours, 1.0))
    EndIf

    int step = _RSL_Disease.StepLinear(pl, "HY", drift, dtHours)
    If step != 0
        int t = stage + step        ; step +1 worsens, -1 recovers
        If t < 0
            t = 0
        ElseIf t > 3
            t = 3
        EndIf
        If t != stage
            HypoSetStage(t)
        EndIf
    EndIf

    ; stage 3: bleed CURRENT HP. Drain is quadratic in time-in-stage-3 so high
    ; regen / big HP pool only buy time - it always overtakes. (Player lockdown
    ; is applied once on stage entry, see HypoSetStage.)
    If _RSL_Disease.GetStage(pl, "HY") >= 3
        float poll = GV(gPollInterval, 1.0)
        float s3 = StorageUtil.GetFloatValue(pl, "_RSL_Dz_HY_S3", 0.0) + poll
        StorageUtil.SetFloatValue(pl, "_RSL_Dz_HY_S3", s3)
        float k = 1.0 + s3 / GV(gHypDrainRamp, 30.0)
        pl.DamageActorValue("Health", GV(gHypDrainPerSec, 1.0) * poll * k * k)
    EndIf
EndFunction

; Worsen while cold >= ColdColdThreshold; recover while cold <= the (lower)
; ColdColdRecoverThreshold; drift between the two thresholds holds the stage.
; True while any survival axis (sleep / hunger / cold) is at least halfway
; through its penalty ramp. This is the shared "conditions are bad" test that
; drives every disease's accumulator: bad -> P falls, all-clear -> P rises.
float DZ_AXIS_BAD = 0.5

bool Function DzAnyAxisBad(bool undead)
    If !undead
        If Ramp(StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0), gSleepGrace.GetValue(), gSleepMax.GetValue()) >= DZ_AXIS_BAD
            return true
        EndIf
        If Ramp(StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0), gHungerGrace.GetValue(), gHungerMax.GetValue()) >= DZ_AXIS_BAD
            return true
        EndIf
    EndIf
    If Ramp(StorageUtil.GetFloatValue(pl, K_COLD, 0.0), GV(gColdGrace, 25.0), 100.0) >= DZ_AXIS_BAD
        return true
    EndIf
    return false
EndFunction

; Common cold. Progression/regression run on the shared _RSL_Disease P engine:
; while sick, any bad axis pushes the hidden P negative, an all-clear pushes it
; positive; a continuous roll (~|P|%/game-hour) steps one stage. Needs
; SUSTAINED conditions - can't bank time and cash it out. Only the contract
; (stage 0 -> 1) is cold-specific: rolls off ColdColdThreshold + cold level.
Function AdvanceColdDisease(bool undead, float dtHours)
    If !ready || !sCC1
        return          ; disease records not in the plugin yet (pre-regen)
    EndIf
    int stage = _RSL_Disease.GetStage(pl, "CC")

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gDiseaseEnabled, 1.0) >= 0.5
    If !on || undead
        If stage > 0
            ClearColdDisease()
        EndIf
        return
    EndIf

    float cold     = StorageUtil.GetFloatValue(pl, K_COLD, 0.0)
    float worsenAt = GV(gColdColdThreshold, 50.0)

    ; cures: each counted Cure-Disease effect = one stage back. Fallback: our
    ; stage spell vanished with no counted cure (console removespell, a cure
    ; another mod fires that IsCureEffect missed) -> treat as one.
    If stage > 0
        int cures = _RSL_Disease.TakeCures(pl, "CC")
        If cures == 0 && !(CCStageSpell(stage) && pl.HasSpell(CCStageSpell(stage)))
            cures = 1
        EndIf
        If cures > 0
            int target = stage - cures
            If target < 0
                target = 0
            EndIf
            CCSetStage(target)
            _RSL_Disease.HalveP(pl, "CC")
            If target == 0
                CCNotify(msgCCCured)
            EndIf
            _RSL_Log.W("ColdDisease: " + cures + " cure(s), stage " + stage + " -> " + target)
            return
        EndIf
    EndIf

    If stage == 0
        If cold >= worsenAt && _RSL_Disease.RollDue(pl, "CC", 1.0)
            float chance = CCContractChance(cold)
            If chance > 0.0 && Utility.RandomFloat(0.0, 100.0) < chance
                CCSetStage(1)
                _RSL_Disease.ResetP(pl, "CC")
                CCNotify(msgCC1)
            EndIf
        EndIf
        return
    EndIf

    ; --- stage >= 1: shared accumulator (net = signed stage delta) ---
    int net = _RSL_Disease.StepP(pl, "CC", DzAnyAxisBad(undead), dtHours, \
        pl.GetActorValue("DiseaseResist"), GV(gDiseaseProgressHours, 24.0), \
        GV(gDiseaseDecayHours, 24.0))
    If net != 0
        int target = stage - net
        If target < 0
            target = 0
        ElseIf target > 3
            target = 3
        EndIf
        If target != stage
            CCSetStage(target)
            If target == 0
                CCNotify(msgCCCured)
            ElseIf target > stage && target == 3
                CCNotify(msgCC3)
            ElseIf target > stage
                CCNotify(msgCC2)
            EndIf
        EndIf
    EndIf
EndFunction

Spell Function ELStageSpell(int s)
    If s == 1
        return sEL1
    ElseIf s == 2
        return sEL2
    ElseIf s == 3
        return sEL3
    EndIf
    return None
EndFunction

; Elemental lesions (frostbite/burns). Bespoke P model - NOT DzAnyAxisBad:
;   worsen : cold >= ElemLesionColdThr, or a frost/fire/shock hit (P damage
;            folded from K_ELPACC; a RFAB_Bandage folds in positive)
;   frozen : any other bad axis (hungry / tired / mild cold) - no change
;   heal   : every axis clear (!DzAnyAxisBad), same drift as a normal disease
; Contract: P <= -ElemLesionContractP (sustained hits), or a roll at deep cold.
; Disease-type SPEL - an engine Cure-Disease still walks it back one stage.
Function AdvanceElemLesion(bool undead, float dtHours)
    If !ready || !sEL1
        return          ; records not in the plugin yet (pre-regen)
    EndIf
    int stage = _RSL_Disease.GetStage(pl, "EL")

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gElemLesionEnabled, 1.0) >= 0.5
    If !on || undead
        If stage > 0
            _RSL_Disease.ClearStages(pl, "EL", sEL1, sEL2, sEL3)
        EndIf
        StorageUtil.SetFloatValue(pl, K_ELPACC, 0.0)
        return
    EndIf

    ; fold pending elemental-hit / bandage P delta
    float pd = StorageUtil.GetFloatValue(pl, K_ELPACC, 0.0)
    If pd != 0.0
        _RSL_Disease.AddP(pl, "EL", pd)
        StorageUtil.SetFloatValue(pl, K_ELPACC, 0.0)
    EndIf

    float cold = StorageUtil.GetFloatValue(pl, K_COLD, 0.0)
    bool coldDeep = cold >= GV(gElemLesionColdThr, 90.0)

    float drift = 0.0
    If coldDeep
        drift = -(100.0 / GV(gDiseaseProgressHours, 24.0))
    ElseIf !DzAnyAxisBad(undead)
        drift = (100.0 / GV(gDiseaseDecayHours, 24.0)) * (1.0 + pl.GetActorValue("DiseaseResist") * 0.01)
        ; an established lesion heals ~3x faster than a fresh scratch settles
        If stage >= 1
            drift *= 3.0
        EndIf
    EndIf

    If stage > 0
        int cures = _RSL_Disease.TakeCures(pl, "EL")
        If cures == 0 && !(ELStageSpell(stage) && pl.HasSpell(ELStageSpell(stage)))
            cures = 1
        EndIf
        If cures > 0
            int t = stage - cures
            If t < 0
                t = 0
            EndIf
            _RSL_Disease.SetStage(pl, "EL", t, ELStageSpell(stage), ELStageSpell(t), 0.0, 0.0)
            _RSL_Disease.HalveP(pl, "EL")
            If t == 0
                CCNotify(msgELCured)
            EndIf
            _RSL_Log.W("ElemLesion: " + cures + " cure(s), stage " + stage + " -> " + t)
            return
        EndIf
    EndIf

    If stage == 0
        float prog = StorageUtil.GetFloatValue(pl, "_RSL_Dz_EL_Prog", 0.0) + drift * dtHours
        If prog > 0.0
            prog = 0.0
        ElseIf prog < -100.0
            prog = -100.0
        EndIf
        If prog <= -GV(gElemLesionContractP, 70.0)
            _RSL_Disease.SetStage(pl, "EL", 1, None, sEL1, 0.0, 0.0)
            _RSL_Disease.ResetP(pl, "EL")
            CCNotify(msgEL1)
            _RSL_Log.W("ElemLesion: contracted (P " + prog + ")")
            return
        EndIf
        ; route B: a lasting after-effect of serious hypothermia (stage >= 2).
        ; cold >= 90 by itself is too brief a window - hypothermia ends it fast.
        If _RSL_Disease.GetStage(pl, "HY") >= 2 && _RSL_Disease.RollDue(pl, "EL", 1.0)
            If Utility.RandomFloat(0.0, 100.0) < GV(gElemLesionHypoChance, 50.0)
                _RSL_Disease.SetStage(pl, "EL", 1, None, sEL1, 0.0, 0.0)
                _RSL_Disease.ResetP(pl, "EL")
                CCNotify(msgEL1)
                _RSL_Log.W("ElemLesion: contracted (hypothermia st." + _RSL_Disease.GetStage(pl, "HY") + ")")
                return
            EndIf
        EndIf
        StorageUtil.SetFloatValue(pl, "_RSL_Dz_EL_Prog", prog)
        return
    EndIf

    ; stage >= 1: deterministic. Folded hits (K_ELPACC -> AddP above) drive P
    ; down; drift heals (all clear) or worsens (deep cold). Crossing +/- the
    ; same threshold as the initial contract steps a stage and resets P - no
    ; -100 clamp pinning, no stochastic roll.
    float lim = GV(gElemLesionContractP, 70.0)
    float prog = StorageUtil.GetFloatValue(pl, "_RSL_Dz_EL_Prog", 0.0)

    ; worsen: test the post-hit value BEFORE drift, so a maxed-out barrage (P
    ; slammed to the -100 clamp) still trips it instead of drift nudging it back.
    If prog <= -lim
        int tw = stage + 1
        If tw > 3
            tw = 3
        EndIf
        If tw != stage
            _RSL_Disease.SetStage(pl, "EL", tw, ELStageSpell(stage), ELStageSpell(tw), 0.0, 0.0)
            If tw == 3
                CCNotify(msgEL3)
            Else
                CCNotify(msgEL2)
            EndIf
            _RSL_Log.W("ElemLesion: worsened " + stage + " -> " + tw + " (P " + prog + ")")
        EndIf
        _RSL_Disease.ResetP(pl, "EL")
        return
    EndIf

    prog += drift * dtHours
    If prog < -100.0
        prog = -100.0
    ElseIf prog > 100.0
        prog = 100.0
    EndIf

    ; heal: sustained recovery lifts P to +lim -> step one stage back
    If prog >= lim
        int th = stage - 1
        _RSL_Disease.SetStage(pl, "EL", th, ELStageSpell(stage), ELStageSpell(th), 0.0, 0.0)
        _RSL_Disease.ResetP(pl, "EL")
        If th == 0
            CCNotify(msgELCured)
        EndIf
        _RSL_Log.W("ElemLesion: healed " + stage + " -> " + th)
        return
    EndIf

    StorageUtil.SetFloatValue(pl, "_RSL_Dz_EL_Prog", prog)
EndFunction

; --- penalties -----------------------------------------------------------

; Axis fill 0..1 on a two-point linear ramp: zero up to `grace`, then linear
; to `max`.
float Function Ramp(float value, float grace, float max)
    If value <= grace
        return 0.0
    EndIf
    If max <= grace
        return 1.0
    EndIf
    float f = (value - grace) / (max - grace)
    If f > 1.0
        f = 1.0
    EndIf
    return f
EndFunction

; Quantization is required: SetNthEffectMagnitude mutates the spell form
; (a form change in the save), and remove/add every tick churns effects and
; the log. So refresh only on a tier change; default step is 5 points.
int Function Tier(float pct)
    float step = gTierStep.GetValue()
    If step < 1.0
        step = 1.0
    EndIf
    return (pct / step) as int
EndFunction

Function ApplyPenalties(bool undead)
    float cap = gPenaltyCap.GetValue()
    float primary = gPenaltyPrimary.GetValue()
    float cross = gPenaltyCross.GetValue()
    float spd = GV(gPenaltySpeed, 10.0)   ; each axis's SpeedMult contribution

    float fSleep = 0.0
    float fHunger = 0.0
    float fCold = 0.0

    If !undead
        fSleep = Ramp(StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0), \
            gSleepGrace.GetValue(), gSleepMax.GetValue())
        fHunger = Ramp(StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0), \
            gHungerGrace.GetValue(), gHungerMax.GetValue())
    EndIf

    fCold = Ramp(StorageUtil.GetFloatValue(pl, K_COLD, 0.0), GV(gColdGrace, 25.0), 100.0)

    ; Each axis's SpeedMult contribution. Total slow is hard-capped by
    ; SpeedCap: RFAB_RestrictMovementOnZeroMS locks the player in place at
    ; SpeedMult <= 0, so never get near it.
    float sSpd = fSleep  * spd
    float hSpd = fHunger * spd
    float cSpd = fCold   * spd
    float totSpd = sSpd + hSpd + cSpd
    float spdCap = GV(gSpeedCap, 30.0)
    If totSpd > spdCap && totSpd > 0.0
        float k = spdCap / totSpd
        sSpd = sSpd * k
        hSpd = hSpd * k
        cSpd = cSpd * k
    EndIf

    ; Primary pool for the axis, plus the cross component on the other two,
    ; plus the cross component on speed.
    ApplyAxis(abSleep,  K_TIER_SL, fSleep  * cross,   fSleep  * primary, fSleep  * cross,   sSpd, cap)
    ApplyAxis(abHunger, K_TIER_HU, fHunger * cross,   fHunger * cross,   fHunger * primary, hSpd, cap)
    ApplyAxis(abCold,   K_TIER_CO, fCold   * primary, fCold   * cross,   fCold   * cross,   cSpd, cap)
EndFunction

; Effect order is fixed by the generator: 0 Health, 1 Magicka, 2 Stamina,
; 3 SpeedMult. Changing it in the generator silently breaks penalties.
Function ApplyAxis(Spell ab, string tierKey, float pctH, float pctM, float pctS, float pctSpd, float cap)
    If !ab
        return
    EndIf

    float step = gTierStep.GetValue()
    If step < 1.0
        step = 1.0
    EndIf

    ; Quantize DOWN to a multiple of step. The same quantized value feeds both
    ; the tier key and the magnitude. Previously the key was quantized but the
    ; magnitude was raw, so a sub-step penalty got stuck (key = tier 0, nonzero
    ; magnitude, refresh skipped) - the source of -4% health at cold=0.
    int th  = Tier(Clamp(pctH,   cap))
    int tm  = Tier(Clamp(pctM,   cap))
    int ts  = Tier(Clamp(pctS,   cap))
    int tsp = Tier(Clamp(pctSpd, cap))
    float qH   = th  * step
    float qM   = tm  * step
    float qS   = ts  * step
    float qSpd = tsp * step

    int t = th * 1000000 + tm * 10000 + ts * 100 + tsp
    If t == (StorageUtil.GetFloatValue(pl, tierKey, -1.0) as int)
        return
    EndIf
    StorageUtil.SetFloatValue(pl, tierKey, t as float)

    _RSL_Log.W("ApplyAxis " + tierKey + ": H=" + qH + " M=" + qM + " S=" + qS + " Spd=" + qSpd)

    ; Magnitude is POSITIVE - Detrimental makes the engine subtract it.
    ; ModActorValue is not used (save/load desync). Magnitude is off the BASE
    ; (GetBaseActorValue), not the current max.
    ab.SetNthEffectMagnitude(0, 0.01 * qH   * pl.GetBaseActorValue("Health"))
    ab.SetNthEffectMagnitude(1, 0.01 * qM   * pl.GetBaseActorValue("Magicka"))
    ab.SetNthEffectMagnitude(2, 0.01 * qS   * pl.GetBaseActorValue("Stamina"))
    ab.SetNthEffectMagnitude(3, 0.01 * qSpd * 100.0)

    ; Refresh: without remove + re-add the new magnitude does not apply.
    pl.RemoveSpell(ab)
    bool anyPenalty = qH > 0.0 || qM > 0.0 || qS > 0.0 || qSpd > 0.0
    If anyPenalty
        pl.AddSpell(ab, false)
    EndIf

    ; SpeedMult does not recompute after add/remove - the engine updates speed
    ; only on a weight change. A CarryWeight nudge forces the recompute.
    If qSpd > 0.0 || StorageUtil.GetFloatValue(pl, tierKey + "_hadSpd", 0.0) > 0.5
        pl.DamageActorValue("CarryWeight", 0.1)
        pl.RestoreActorValue("CarryWeight", 0.1)
    EndIf
    StorageUtil.SetFloatValue(pl, tierKey + "_hadSpd", ((qSpd > 0.0) as int) as float)
EndFunction

float Function Clamp(float v, float cap)
    If v < 0.0
        return 0.0
    EndIf
    If v > cap
        return cap
    EndIf
    return v
EndFunction

; --- full-bar bonuses --------------------------------------------------
; While a need's deprivation stays within BonusThresholdPct of its axis max,
; a +BonusRegenPct% regen buff on the matching pool - same axis->pool mapping
; as the penalties (cold->Health, sleep->Magicka, hunger->Stamina). Three
; separate 1-effect abilities so only the earned ones show in the UI.
; Undead have no sleep/hunger axis - only "warmed" can apply to them.
Function ApplyBonus(bool undead)
    If !abBonusWarm
        return          ; records not in the plugin yet (pre-regen)
    EndIf

    bool on = gModEnabled.GetValue() >= 0.5 && GV(gBonusEnabled, 1.0) >= 0.5
    float pct = GV(gBonusRegenPct, 5.0)
    float thr = GV(gBonusThresholdPct, 10.0) * 0.01

    bool warmed = on && StorageUtil.GetFloatValue(pl, K_COLD, 0.0) <= thr * 100.0
    bool rested = on && !undead && StorageUtil.GetFloatValue(pl, K_SLEEP, 0.0) <= thr * gSleepMax.GetValue()
    bool fed    = on && !undead && StorageUtil.GetFloatValue(pl, K_HUNGER, 0.0) <= thr * gHungerMax.GetValue()

    SetBonus(abBonusWarm, "_RSL_BonWarm", warmed, pct)
    SetBonus(abBonusRest, "_RSL_BonRest", rested, pct)
    SetBonus(abBonusFed,  "_RSL_BonFed",  fed,    pct)
EndFunction

; Add/remove one bonus ability. Dedup on (active, pct) so the SPEL form is
; mutated only on a real change (SetNthEffectMagnitude = a save change).
Function SetBonus(Spell ab, string tierKey, bool onNow, float pct)
    If !ab
        return
    EndIf
    int sig = 0
    If onNow
        sig = 1 + (pct as int) * 2
    EndIf
    If sig == (StorageUtil.GetFloatValue(pl, tierKey, -1.0) as int)
        return
    EndIf
    StorageUtil.SetFloatValue(pl, tierKey, sig as float)

    ab.SetNthEffectMagnitude(0, pct)
    pl.RemoveSpell(ab)
    If onNow
        pl.AddSpell(ab, false)
    EndIf
    _RSL_Log.W("SetBonus " + tierKey + ": on=" + onNow + " pct=" + pct)
EndFunction

Function ClearBonus()
    If !pl
        pl = Game.GetPlayer()
    EndIf
    ClearOneBonus(abBonusWarm, "_RSL_BonWarm")
    ClearOneBonus(abBonusRest, "_RSL_BonRest")
    ClearOneBonus(abBonusFed,  "_RSL_BonFed")
EndFunction

Function ClearOneBonus(Spell ab, string tierKey)
    If ab
        pl.RemoveSpell(ab)
        StorageUtil.SetFloatValue(pl, tierKey, -1.0)
    EndIf
EndFunction

; --- safe shutdown ------------------------------------------------------

; Without this, disabling an axis or the mod leaves penalties on the pools -
; the most likely bug report.
Function ClearAllPenalties()
    If !pl
        pl = Game.GetPlayer()
    EndIf

    ClearAxis(abSleep,  K_TIER_SL)
    ClearAxis(abHunger, K_TIER_HU)
    ClearAxis(abCold,   K_TIER_CO)
    ClearBonus()
EndFunction

Function ClearAxis(Spell ab, string tierKey)
    If !ab
        return
    EndIf
    ab.SetNthEffectMagnitude(0, 0.0)
    ab.SetNthEffectMagnitude(1, 0.0)
    ab.SetNthEffectMagnitude(2, 0.0)
    ab.SetNthEffectMagnitude(3, 0.0)   ; SpeedMult
    pl.RemoveSpell(ab)
    StorageUtil.SetFloatValue(pl, tierKey, -1.0)
    ; restore speed (weight nudge)
    pl.DamageActorValue("CarryWeight", 0.1)
    pl.RestoreActorValue("CarryWeight", 0.1)
    StorageUtil.SetFloatValue(pl, tierKey + "_hadSpd", 0.0)
EndFunction

; Full teardown: strip every effect this mod applies and reset its state, but
; leave the monitor ability alone so the tick keeps running. Called once by
; OnUpdate when "Мод включён" goes on->off; re-enabling lets the tick re-apply
; everything from a clean slate. Does NOT touch the ModEnabled global itself.
Function TeardownAll() global
    Actor p = Game.GetPlayer()

    ShutdownSpell(p, _RSL_Forms.AbSleep())
    ShutdownSpell(p, _RSL_Forms.AbHunger())
    ShutdownSpell(p, _RSL_Forms.AbCold())

    Spell abBW = _RSL_Forms.AbBonusWarm()
    Spell abBR = _RSL_Forms.AbBonusRest()
    Spell abBF = _RSL_Forms.AbBonusFed()
    If abBW
        p.RemoveSpell(abBW)
    EndIf
    If abBR
        p.RemoveSpell(abBR)
    EndIf
    If abBF
        p.RemoveSpell(abBF)
    EndIf
    StorageUtil.SetFloatValue(p, "_RSL_BonWarm", -1.0)
    StorageUtil.SetFloatValue(p, "_RSL_BonRest", -1.0)
    StorageUtil.SetFloatValue(p, "_RSL_BonFed", -1.0)

    StorageUtil.SetFloatValue(p, "_RSL_SleepCounter", 0.0)
    StorageUtil.SetFloatValue(p, "_RSL_HungerCounter", 0.0)
    StorageUtil.SetFloatValue(p, "_RSL_ColdExposure", 0.0)
    StorageUtil.SetFloatValue(p, "_RSL_ElemAccum", 0.0)   ; K_ELEMACC (literal - global fn)
    StorageUtil.SetFloatValue(p, "_RSL_Dz_EL_PAcc", 0.0)  ; K_ELPACC (literal - global fn)
    StorageUtil.SetFloatValue(p, "_RSL_TierSleep", -1.0)
    StorageUtil.SetFloatValue(p, "_RSL_TierHunger", -1.0)
    StorageUtil.SetFloatValue(p, "_RSL_TierCold", -1.0)

    _RSL_Disease.ClearStages(p, "CC", _RSL_Forms.DiseaseColdCommon1(), \
        _RSL_Forms.DiseaseColdCommon2(), _RSL_Forms.DiseaseColdCommon3())
    _RSL_Disease.ClearStages(p, "BR", _RSL_Forms.DiseaseBrownRot1(), \
        _RSL_Forms.DiseaseBrownRot2(), _RSL_Forms.DiseaseBrownRot3())
    _RSL_Disease.ClearStages(p, "GW", _RSL_Forms.DiseaseGutworm1(), \
        _RSL_Forms.DiseaseGutworm2(), _RSL_Forms.DiseaseGutworm3())
    _RSL_Disease.ClearStages(p, "GS", _RSL_Forms.DiseaseGreenspore1(), \
        _RSL_Forms.DiseaseGreenspore2(), _RSL_Forms.DiseaseGreenspore3())
    _RSL_Disease.ClearStages(p, "FP", _RSL_Forms.DiseaseFoodPoison1(), \
        _RSL_Forms.DiseaseFoodPoison2(), _RSL_Forms.DiseaseFoodPoison3())
    _RSL_Disease.ClearStages(p, "EL", _RSL_Forms.DiseaseElemLesion1(), \
        _RSL_Forms.DiseaseElemLesion2(), _RSL_Forms.DiseaseElemLesion3())
    _RSL_Disease.ClearStages(p, "HY", _RSL_Forms.AbHypo1(), \
        _RSL_Forms.AbHypo2(), _RSL_Forms.AbHypo3())
    ClearAllRfabWraps(p)
    p.SetActorValue("Paralysis", 0.0)
    Game.SetInChargen(false, false, false)
    StorageUtil.SetIntValue(p, "_RSL_HypoWaitBlocked", 0)   ; K_HYWAIT (literal - global fn)
    Game.EnablePlayerControls()

    EffectShader shd = _RSL_Forms.FxColdShader()
    If shd
        shd.Stop(p)
    EndIf
    StorageUtil.SetIntValue(p, "_RSL_SHon", 0)
EndFunction

Function ShutdownSpell(Actor p, Spell ab) global
    If !ab
        return
    EndIf
    ab.SetNthEffectMagnitude(0, 0.0)
    ab.SetNthEffectMagnitude(1, 0.0)
    ab.SetNthEffectMagnitude(2, 0.0)
    ab.SetNthEffectMagnitude(3, 0.0)   ; SpeedMult
    p.RemoveSpell(ab)
    ; restore speed
    p.DamageActorValue("CarryWeight", 0.1)
    p.RestoreActorValue("CarryWeight", 0.1)
EndFunction

; MCM "Reset to defaults" button + settings migration. All GLOB defaults
; live in the generator (BuildGlobals -> _RSL_Balance.psc).
Function ResetDefaults() global
    _RSL_Balance.ResetDefaults()
EndFunction
