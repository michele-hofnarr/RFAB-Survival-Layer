#pragma once

#include "Core/Illness.h"

// Frostbite and burns - "Tissue stress" and what it becomes.
//
// The same three stages and the same records as the others, and deliberately
// NOT the same progression. It is easy to simplify this back into the shared
// model and lose the point, so the difference is spelled out:
//
//   worsen   deep cold, or an elemental hit. Frost, fire AND shock count -
//            shock damages tissue without touching the cold bar at all.
//   hold     any other bad axis. Hungry or tired does not heal a burn, but it
//            does not deepen one either.
//   heal     every axis clear, and about three times faster once it has
//            actually set in than while it is still a scratch.
//
// P here is DETERMINISTIC, not a roll, and crossing the same threshold is what
// both catches it and moves a stage - so fElemLesionContractP does two jobs on
// purpose rather than by accident. That is why both Contracts() and Progress()
// are overridden: one hook would have hidden half of it.
//
// Hits are folded in from Elemental, where they accumulate between ticks, and a
// clean linen cloth pushes the counter back.
//
// There is a second way in: sustained serious hypothermia. Deep cold on its own
// is too brief a window, because hypothermia ends it one way or the other
// quickly, so stage 2 or worse rolls for it once per game hour.

namespace RSL
{
    class LesionIllness : public Illness
    {
    public:
        using Illness::Illness;

        // Is there anything for a bandage to patch - a lesion that has set in,
        // or a counter already running towards one? v0.4.0 gates the bandage on
        // exactly this, so one used on an unhurt player is simply eaten.
        [[nodiscard]] bool Wounded() const;

    protected:
        [[nodiscard]] bool Enabled() const override;

        void Observe(const Tick& a_tick, std::int32_t a_stage) override;
        void Idle() override;

        [[nodiscard]] bool         Contracts(const Tick& a_tick) override;
        [[nodiscard]] std::int32_t Progress(const Tick& a_tick) override;

        [[nodiscard]] std::string TraceExtra(const Tick& a_tick) const override;

    private:
        // The ceiling P can actually reach, and the threshold in both
        // directions. It IS 100 now - the same number every other threshold in
        // the engine uses - and the setting is gone from the menu, so the cap
        // guards nothing but a hand-edited ini. Set higher there, the counter
        // would sit at its floor for ever and the lesion could never be
        // caught, with nothing anywhere saying why.
        [[nodiscard]] static float Limit();

        // How P moves this pass, per game hour. Worked out once at the top of
        // the pass, because both the stage-0 path and the progression need the
        // same number and computing it twice is how two of them drift apart.
        float _drift{ 0.0f };
    };
}
