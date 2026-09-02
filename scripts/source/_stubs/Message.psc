Scriptname Message extends Form Hidden
{Stub. Declarations only - see README.txt in this folder.}

; shows a message; returns the pressed button index (-1 if no buttons)
int Function Show(float afArg1 = 0.0, float afArg2 = 0.0, float afArg3 = 0.0, \
                  float afArg4 = 0.0, float afArg5 = 0.0, float afArg6 = 0.0, \
                  float afArg7 = 0.0, float afArg8 = 0.0, float afArg9 = 0.0) native

; shows a message as a notification
Function ShowAsHelpMessage(string asMessageID, float afDuration, float afInterval, int aiTimesToShow) native

; resets the notification show counter
Function ResetHelpMessage(string asMessageID) global native
