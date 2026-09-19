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

        // A load. Only "do I believe the crust is on" is dropped, so the next
        // pass decides again from the cold bar - and PlayerShader answers it by
        // looking at the engine rather than by re-applying blind. What is NOT
        // dropped is which effect is ours: it survives the load, and so must
        // the pointer to it - see "A LOAD DOES NOT TAKE THE EFFECT WITH IT"
        // in Core/PlayerShader.h, which is where that was got wrong.
        void Forget();

    private:
        bool         _on{ false };
        PlayerShader _fx;
    };
}
