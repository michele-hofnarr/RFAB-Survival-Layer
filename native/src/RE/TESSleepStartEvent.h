#pragma once

// The event CommonLibSSE forgot to declare.
//
// ScriptEventSourceHolder holds a BSTEventSource<TESSleepStartEvent> - the
// forward declaration and the source are both there - but no header defines the
// struct, so it cannot be sunk without writing one.
//
// The layout is the one Papyrus exposes. Actor.RegisterForSleep delivers
//
//     Event OnSleepStart(float afSleepStartTime, float afDesiredSleepEndTime)
//
// and Papyrus events are thin wrappers over these native ones, so the struct is
// those two floats, in game-time days. Both are logged the first time the event
// fires precisely because this is inferred rather than declared: game time here
// is a small positive number of days, so a wrong guess is obvious on sight.

namespace RE
{
    struct TESSleepStartEvent
    {
    public:
        // members
        float sleepStartTime;        // 00
        float desiredSleepEndTime;   // 04
    };
    static_assert(sizeof(TESSleepStartEvent) == 0x8);
}
