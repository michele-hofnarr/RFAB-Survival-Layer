#pragma once

#include "Core/Illness.h"

// Brown rot, gutworm, green spore, food poisoning: the four caught by something
// happening TO the player - a draugr's touch, a troll's claws, a slaughterfish
// bite, a bad meal.
//
// NOTHING IS OVERRIDDEN, and that is the point of the class existing. These
// four are exactly what Illness describes, so the base is not a compromise
// struck between five different illnesses - it is the shape of these, and every
// other illness in the hierarchy is read as a departure from it. An override
// anywhere else is a statement that something genuinely differs; if this class
// ever grows one, the base has drifted.
//
// Catching them is not here. It happens on the event - Events.cpp, through
// Illness::Roll for a hit and Illness::Contract for raw food - because nothing
// about being bitten is visible from a tick.

namespace RSL
{
    class HitIllness : public Illness
    {
    public:
        using Illness::Illness;
    };

    // Two of them bend a NEED rather than an actor value, because there is no
    // actor value that says either of these things. Controller-side in v0.4.0
    // for the same reason.
    //
    // Free functions rather than methods: the callers are the needs model,
    // which has no business holding an illness, and all three are pure reads of
    // a stage.
    [[nodiscard]] float GutwormHungerMult();    // 1.0 / 1.3 / 1.7 / 2.5
    [[nodiscard]] float GutwormFoodPenalty();   // 0 / .25 / .5 / .8 off a meal
    [[nodiscard]] float BrownRotSleepMult();    // 1.0 / .9 / .8 / .7
}
