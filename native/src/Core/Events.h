#pragma once

// Game events the needs model has to know about.
//
// Two of them, and both exist because the ordinary per-frame tick cannot see
// what happened:
//
//   - Sleeping and waiting move the clock by hours in one step. The tick
//     deliberately refuses to integrate more than a couple of hours at once, so
//     without catching the jump explicitly a night's sleep would cost two hours
//     of hunger and nothing else. TESSleepStopEvent carries no duration, so the
//     span is measured from the Sleep/Wait menu opening and closing.
//
//   - Eating is an equip of an alchemy item. There is no "ate something" event.

namespace RSL
{
    namespace Events
    {
        void Install();
    }
}
