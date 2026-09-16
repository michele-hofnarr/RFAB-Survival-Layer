#pragma once

#include "Core/Forms.h"

namespace RSL
{
    // The part every progressive illness does the same way.
    //
    // v0.4.0 has this as AdvanceHitDz, a loop over four table rows, and it is
    // identical to the body of AdvanceColdDisease with the contract roll taken
    // out: count cures, walk the stage back if any landed, otherwise step P and
    // move a stage if it crossed. Only catching it differs between illnesses,
    // and that is not in here - it comes from a hit, a meal, or a roll made by
    // whoever owns the disease.
    //
    // The common cold is deliberately not routed through this yet. It works,
    // it is tested, and folding it in is a refactor with nothing to gain until
    // the rest of the illnesses have proved this path.
    namespace StagedDisease
    {
        // Cures, progression, notification. Returns the stage it settled on.
        //
        // a_bandFrom* are the survival axes, and they drive the three-band
        // classifier the same way the cold uses it.
        std::int32_t Advance(const DiseaseForms& a_forms, float a_sleep, float a_hunger,
            float a_cold, float a_gameHours, bool a_undead);

        // Put the illness on at this stage, announcing it as newly caught.
        // Used by the OnHit and the raw-food paths.
        void Contract(const DiseaseForms& a_forms);

        // Strip it, for the mod being switched off or the player turning undead.
        void Clear(const DiseaseForms& a_forms);

        // Move to an exact stage, saying whichever of the six things fits.
        void SetStage(const DiseaseForms& a_forms, std::int32_t a_stage, std::int32_t a_old);

        // Roll for it, the way a hit or a bad meal does. a_chancePercent is
        // taken as given - disease resistance is the caller's business, because
        // v0.4.0 applies it to a hit and deliberately not to raw food.
        void Roll(const DiseaseForms& a_forms, float a_chancePercent, const char* a_why);

        // Gutworm eats what you eat. Both multipliers are controller-side in
        // v0.4.0 too - there is no actor value that says either of these.
        [[nodiscard]] float GutwormHungerMult();   // 1.0 / 1.3 / 1.7 / 2.5
        [[nodiscard]] float GutwormFoodPenalty();  // 0 / .25 / .5 / .8 taken off a meal

        // Brown rot rests you badly: rotting flesh sleeps poorly. Scales what a
        // night is worth, exactly as gutworm scales what a meal is worth - and
        // for the same reason it lives here rather than in an actor value.
        [[nodiscard]] float BrownRotSleepMult();   // 1.0 / .9 / .8 / .7
    }
}
