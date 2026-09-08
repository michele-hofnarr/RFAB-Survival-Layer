scriptname _RSL_HUDWidget extends SKI_WidgetBase
{ HUD widget: 3 bars (SLEEP/FOOD/COLD) with a notch at the safe threshold and
  the current penalty printed over each bar.

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

; _RSL_Controller toggles this on OnMenuOpen/Close for the SkyUI item menus
; (inventory / container / barter / gift), which never drive the .swf's own
; menu-hide path. Container-level _visible, independent of the opacity/autohide
; the tick pushes via setData.
function SetMenuHidden(bool a_hidden)
    If Ready
        UI.InvokeBool(HUD_MENU, WidgetRoot + ".setMenuHidden", a_hidden)
    EndIf
endFunction

function PushData(bool sleepShown, float sleepFill, float sleepSafe, \
                  bool hungerShown, float hungerFill, float hungerSafe, \
                  bool coldShown, float coldFill, float coldSafe, \
                  bool autoHide, float masterAlpha, int tempFeel, bool colorUI, \
                  float sleepPen, float hungerPen, float coldPen)
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
    PushRaw(s, sleepFill, sleepSafe, h, hungerFill, hungerSafe, c, coldFill, coldSafe, \
            a, masterAlpha, tempFeel as float, ci, sleepPen, hungerPen, coldPen)
EndFunction

function PushRaw(float p0, float p1, float p2, float p3, float p4, float p5, \
                float p6, float p7, float p8, float p9, float p10, float p11, float p12, \
                float p13, float p14, float p15)
    If !Ready
        return
    EndIf
    float[] args = new float[16]
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
    args[13] = p13
    args[14] = p14
    args[15] = p15
    UI.InvokeFloatA(HUD_MENU, WidgetRoot + ".setData", args)
EndFunction

; The temperature icon and the inventory food bar are placed on their own, not
; as part of the bar block. _RSL_Controller does the arithmetic - it knows the
; widget origin and scale, so what arrives here is already in the widget's own
; coordinate space and needs no further conversion.

function SetTempPos(float a_x, float a_y, float a_scale)
    If !Ready
        return
    EndIf
    float[] args = new float[3]
    args[0] = a_x
    args[1] = a_y
    args[2] = a_scale
    UI.InvokeFloatA(HUD_MENU, WidgetRoot + ".setTempPos", args)
endFunction

; The food bar drawn over the inventory menu. `projected` is where the bar
; would sit after eating whatever is highlighted; the widget shades the gap
; between it and `fill` lighter or darker to show which way it would move.
function SetInvBar(bool shown, float fill, float safe, float projected, \
                   float a_x, float a_y, float a_scale)
    If !Ready
        return
    EndIf
    float s = 0.0
    If shown
        s = 1.0
    EndIf
    float[] args = new float[7]
    args[0] = s
    args[1] = fill
    args[2] = safe
    args[3] = projected
    args[4] = a_x
    args[5] = a_y
    args[6] = a_scale
    UI.InvokeFloatA(HUD_MENU, WidgetRoot + ".setInvBar", args)
endFunction
