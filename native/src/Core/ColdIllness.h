#pragma once

#include "Core/Illness.h"

// The common cold: caught from being cold, shaken off by looking after
// yourself. Ported from AdvanceColdDisease.
//
// It is the illness that established the stochastic model everything else
// inherits, so almost all of it now lives in Illness. What is left here is the
// one thing it does that nothing else does: it CATCHES ITSELF, by rolling once
// a game hour against a chance that ramps with how cold you are.
//
// "Any axis past its line" is deliberately not just the cold one, and that part
// is shared: being tired or hungry keeps a cold from clearing even in the warm,
// which is what makes the three axes a system rather than three separate
// meters.

namespace RSL
{
    class ColdIllness : public Illness
    {
    public:
        using Illness::Illness;

    protected:
        [[nodiscard]] bool Contracts(const Tick& a_tick) override;

        [[nodiscard]] std::string TraceExtra(const Tick& a_tick) const override;

    private:
        // Chance per game hour at this much cold left, already scaled by
        // disease resistance. Zero above the line.
        [[nodiscard]] static float ContractChance(float a_cold);
    };
}
