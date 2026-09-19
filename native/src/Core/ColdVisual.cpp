#include "PCH.h"

#include "Core/ColdVisual.h"

#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    ColdVisual& ColdVisual::GetSingleton()
    {
        static ColdVisual singleton;
        return singleton;
    }

    void ColdVisual::Update(float a_cold)
    {
        if (!Forms::fxColdShader) {
            return;
        }

        const bool wanted = Settings::bColdShaderEnabled && Settings::bModEnabled &&
                            a_cold <= Settings::fColdShaderAt;

        // A repair loop stood here once, re-issuing the start every second
        // whenever the shader could not be found running. It never worked and
        // could not: the call it kept repeating was malformed. Both are gone -
        // Apply goes straight at the engine and says whether it took.
        if (wanted == _on) {
            return;
        }

        if (wanted) {
            // Nothing to attach to while the player has no model, which is the
            // state a fast travel passes through. Failing leaves _on alone, so
            // the next pass tries again.
            if (!_fx.Apply(Forms::fxColdShader)) {
                return;
            }
        } else {
            _fx.Stop(Forms::fxColdShader);
        }

        _on = wanted;
        logger::info("frost shader {}", wanted ? "on" : "off");
    }

    void ColdVisual::ClearAll()
    {
        // UNCONDITIONALLY, unlike everything else here. This is the master
        // switch, and the one thing it may not do is leave a crust behind on
        // the strength of a flag - a load drops that flag while the effect it
        // describes goes on running. Stop goes by shader and target now, so
        // asking it when there is nothing to end costs one walk of the temp
        // effects and says so in the log.
        _on = false;
        _fx.Stop(Forms::fxColdShader);
    }

    void ColdVisual::Forget()
    {
        _on = false;
        _fx.Forget();
    }
}
