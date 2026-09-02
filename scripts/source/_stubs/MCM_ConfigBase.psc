ScriptName MCM_ConfigBase Extends SKI_ConfigBase
{Header stub for MCM_ConfigBase from MCM Helper. Decompiled from
 mcm_configbase.pex (Champollion) - this class has no source, it ships
 compiled. Do not compile this folder - see README.txt.

 OnConfigManagerReady was dropped from the decompile: its body called
 Parent.OnConfigManagerReady, which does not exist in the SkyUI 5.2 SDK.
 We do not need it - the real implementation is already in the .pex.}
;-- Functions ---------------------------------------

String Function GetCustomControl(Int a_keyCode) Native

Bool Function GetModSettingBool(String a_settingName) Native

Float Function GetModSettingFloat(String a_settingName) Native

Int Function GetModSettingInt(String a_settingName) Native

String Function GetModSettingString(String a_settingName) Native



Function LoadConfig() Native

Function OnOptionColorAccept(Int a_option, Int a_color) Native

Function OnOptionColorOpen(Int a_option) Native

Function OnOptionDefault(Int a_option) Native

Function OnOptionHighlight(Int a_option) Native

Function OnOptionInputAccept(Int a_option, String a_input) Native

Function OnOptionInputOpen(Int a_option) Native

Function OnOptionKeyMapChange(Int a_option, Int a_keyCode, String a_conflictControl, String a_conflictName) Native

Function OnOptionMenuAccept(Int a_option, Int a_index) Native

Function OnOptionMenuOpen(Int a_option) Native

Function OnOptionSelect(Int a_option) Native

Function OnOptionSliderAccept(Int a_option, Float a_value) Native

Function OnOptionSliderOpen(Int a_option) Native

Function OnPageReset(String a_page) Native

Function OnPageSelect(String a_page)
  ; Empty function
EndFunction

Function OnSettingChange(String a_ID)
  ; Empty function
EndFunction

Function RefreshMenu() Native

Function SetMenuOptions(String a_ID, String[] a_options, String[] a_shortNames) Native

Function SetModSettingBool(String a_settingName, Bool a_value) Native

Function SetModSettingFloat(String a_settingName, Float a_value) Native

Function SetModSettingInt(String a_settingName, Int a_value) Native

Function SetModSettingString(String a_settingName, String a_value) Native
