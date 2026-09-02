Scriptname GlobalVariable extends Form Hidden
{Stub. Declarations only - see README.txt in this folder.}

; returns the global's value
float Function GetValue() native
int Function GetValueInt() native

; sets the global's value
Function SetValue(float afValue) native
Function SetValueInt(int aiValue) native

; adds the given amount to the global's value
Function Mod(float afValue) native

; wrapper property. Required: vanilla Quest.psc reads aModGlobal.value and
; will not compile without it.
float Property Value Hidden
    float Function Get()
        return GetValue()
    EndFunction
    Function Set(float afValue)
        SetValue(afValue)
    EndFunction
EndProperty
