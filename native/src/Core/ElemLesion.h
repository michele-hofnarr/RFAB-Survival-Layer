#pragma once

// Frostbite and burns.
//
// A Disease-type illness with three stages like the others, but its own
// progression, and the difference is worth stating because it is easy to
// "simplify" back into the shared engine and lose the point:
//
//   worsen   deep cold, or an elemental hit. Frost, fire and shock all count -
//            shock damages tissue without touching the cold bar at all.
//   hold     any other bad axis. Hungry or tired does not heal a burn, but it
//            does not deepen one either.
//   heal     every axis clear, and about three times faster once it has
//            actually set in than while it is still a scratch.
//
// P is deterministic here, not a roll, and crossing the SAME threshold is what
// both catches it and moves a stage - so ElemLesionContractP does two jobs on
// purpose rather than by accident. Hits are folded in from Elemental, where
// they were accumulated between ticks, and a bandage pushes the counter back.
//
// There is a second way in: sustained serious hypothermia. Deep cold on its own
// is too brief a window, because hypothermia ends it one way or the other
// quickly, so stage 2 or worse rolls for it per game hour.

namespace RSL
{
    class ElemLesion
    {
    public:
        static ElemLesion& GetSingleton();

        // a_cold is the reserve, 0..1.
        void Update(float a_sleep, float a_hunger, float a_cold,
            float a_gameHours, bool a_undead);

        void ClearAll();

        [[nodiscard]] std::int32_t Stage() const;

        // Is there anything for a bandage to patch - a lesion that has set in,
        // or a counter already running towards one. v0.4.0 gates the bandage on
        // exactly this, so one used on an unhurt player is simply eaten.
        [[nodiscard]] bool Wounded() const;
    };
}
