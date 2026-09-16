#pragma once

// The common cold: caught from being cold, shaken off by looking after
// yourself. Ported from AdvanceColdDisease.
//
// It is the first illness to use the stochastic half of the disease engine, and
// the shape it establishes is shared by everything that follows:
//
//   contract  a roll, gated to once per game hour, whose chance ramps with how
//             cold you are and is reduced by disease resistance
//   progress  P drifts down while any survival axis is past its line and up
//             while they are all clear, so an illness gets worse when you are
//             not coping and eases off when you are
//   cure      each counted Cure Disease effect walks it back one stage
//
// "Any axis past its line" is deliberately not just the cold one. Being tired
// or hungry keeps a cold from clearing even in the warm, which is what makes
// the three axes a system rather than three separate meters.

#include "Core/Disease.h"

namespace RSL
{
    class CommonCold
    {
    public:
        static CommonCold& GetSingleton();

        // a_* are reserves, 0..1.
        void Update(float a_sleep, float a_hunger, float a_cold,
            float a_gameHours, bool a_undead);

        void ClearAll();

        [[nodiscard]] std::int32_t Stage() const;

    private:
        void SetStage(std::int32_t a_stage, std::int32_t a_old);

        [[nodiscard]] static RE::SpellItem* StageSpell(std::int32_t a_stage);
        [[nodiscard]] static float          ContractChance(float a_cold);
    };

    // Shared by every disease that uses the stochastic model. Three bands, in
    // priority order:
    //
    //   worsen  any axis below half its bar
    //   heal    every axis above its own safe mark
    //   hold    neither - P stops accumulating but still rolls
    //
    // The bands cannot overlap: every safe mark sits above half, so an axis
    // that is above its mark is never also below half.
    //
    // This is a deliberate divergence from v0.4.0, which had no hold band and
    // measured "bad" as half the distance from the safe mark to the bottom
    // rather than half the bar. Under the old rule a sleep bar of 48% scored
    // 0.28 and counted as clear, so a cold healed while the character was well
    // below the notch the widget draws.
    [[nodiscard]] AxisBand AxisState(float a_sleep, float a_hunger, float a_cold, bool a_undead);
}
