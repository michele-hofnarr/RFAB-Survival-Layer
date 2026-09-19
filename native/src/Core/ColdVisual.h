#pragma once

#include "Core/PlayerShader.h"

// The ice crust on the character once the cold bar bottoms out.
//
// The shader itself is started and stopped by PlayerShader, which is where the
// lifetime question is answered and where the reasoning for it lives. This class
// only decides WHEN: at or below fColdShaderAt, and not while the mod or the
// effect is switched off.
//
// It went through Papyrus until the crash at hypothermia stage one showed what a
// dispatched call costs; the same delete-under-the-VM was sitting here, waiting
// its turn. See Core/PlayerShader.h.
//
// The screen-space part of the cold visuals - the image space modifiers - is
// Core/ColdScreen, not this.

namespace RSL
{
    class ColdVisual
    {
    public:
        static ColdVisual& GetSingleton();

        // a_cold is the reserve, 0..1. The BASE reserve: a bought buffer buys
        // time, it does not thaw the crust.
        void Update(float a_cold);

        void ClearAll();

        // A load. Both our answers go: whether the crust is on, and which
        // instance is ours. The second one cannot outlive a load however it is
        // held - the effect is rebuilt as a different object - which is why
        // taking it off again goes by shader and target instead. See the note
        // on PlayerShader::Stop.
        void Forget();

    private:
        bool         _on{ false };
        PlayerShader _fx;
    };
}
