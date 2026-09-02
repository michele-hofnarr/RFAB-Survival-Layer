scriptname _RSL_HUDWidget extends SKI_WidgetBase
{ HUD widget: 3 bars (SLEEP/FOOD/COLD) with a notch at the safe threshold.

  SkyUI route (SKI_WidgetBase loads and positions the .swf). Values come from
  _RSL_Controller once per tick via PushData(). The SKIWF registration is lost
  on save load - _RSL_Controller calls Kick() from OnPlayerLoadGame, like
  PokeMCM for the menu. }

int function GetVersion()
    return 1
endFunction

; SKI_WidgetBase.IsExtending() compares this to its own script name. Return
; the PARENT name so the check sees inheritance and does not complain.
string function GetWidgetType()
    return "SKI_WidgetBase"
endFunction

string function GetWidgetSource()
    return "RFABSurvivalLayer/RSLHud.swf"
endFunction

event OnWidgetInit()
    ; All HUD gameplay modes, so the widget does not hide while swimming /
    ; mounted / sneaking.
    Modes = new string[9]
    Modes[0] = "All"
    Modes[1] = "StealthMode"
    Modes[2] = "Favor"
    Modes[3] = "Swimming"
    Modes[4] = "HorseMode"
    Modes[5] = "WarHorseMode"
    Modes[6] = "MovementDisabled"
    Modes[7] = "SleepWaitMode"
    Modes[8] = "TweenMode"
endEvent

event OnWidgetLoad()
    parent.OnWidgetLoad()
    _RSL_Log.W("HUDWidget loaded: id=" + WidgetID + " root='" + WidgetRoot + "'")
endEvent

; ---- API for _RSL_Controller ----

; Re-register with the SkyUI manager after a save load (like PokeMCM).
function Kick()
    OnGameReload()
endFunction

function SetScale(float a_pct)
    If Ready
        UI.InvokeFloat(HUD_MENU, WidgetRoot + ".setScale", a_pct)
    EndIf
endFunction

function PushData(bool sleepShown, float sleepFill, float sleepSafe, \
                  bool hungerShown, float hungerFill, float hungerSafe, \
                  bool coldShown, float coldFill, float coldSafe, \
                  bool autoHide, float masterAlpha, int tempFeel, bool colorUI)
    float s = 0.0
    float h = 0.0
    float c = 0.0
    float a = 0.0
    float ci = 0.0
    If sleepShown
        s = 1.0
    EndIf
    If hungerShown
        h = 1.0
    EndIf
    If coldShown
        c = 1.0
    EndIf
    If autoHide
        a = 1.0
    EndIf
    If colorUI
        ci = 1.0
    EndIf
    PushRaw(s, sleepFill, sleepSafe, h, hungerFill, hungerSafe, c, coldFill, coldSafe, a, masterAlpha, tempFeel as float, ci)
EndFunction

function PushRaw(float p0, float p1, float p2, float p3, float p4, float p5, \
                float p6, float p7, float p8, float p9, float p10, float p11, float p12)
    If !Ready
        return
    EndIf
    float[] args = new float[13]
    args[0]  = p0
    args[1]  = p1
    args[2]  = p2
    args[3]  = p3
    args[4]  = p4
    args[5]  = p5
    args[6]  = p6
    args[7]  = p7
    args[8]  = p8
    args[9]  = p9
    args[10] = p10
    args[11] = p11
    args[12] = p12
    UI.InvokeFloatA(HUD_MENU, WidgetRoot + ".setData", args)
EndFunction
