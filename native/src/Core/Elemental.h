#pragma once

// Frost and fire landing on the player move the cold bar.
//
// Frost chills, fire warms, both scaled by the matching resistance. Warming
// yourself on fire damage is deliberately usable - burning costs health, which
// makes it a real trade rather than an exploit.
//
// Hits are queued rather than applied where they land, and folded in on the
// needs tick, so every change to the axis goes through one place.
//
// A fire trap or a cloak effect fires the apply event ten to thirty times a
// second. That used to need a hard debounce, because each event was paid a flat
// amount whether or not anything got through; now each one is paid what it
// actually did, so thirty applications of a trap are worth thirty applications
// of a trap and no more. The debounce is gone from the cold axis.
//
// It still guards the LESION counter, which is unchanged and still counts hits
// rather than damage: thirty of those a second would be thirty wounds.
//
// Damage over time is not counted per event at all. A concentration effect's
// magnitude is damage per SECOND, so it is integrated on the tick against real
// elapsed time - which is exact, and needs no assumption about how often the
// engine chooses to re-announce it.
//
// The other half is elemental lesions: frost, fire AND shock all damage a
// separate progression counter, which is why shock is noticed here at all even
// though it never touches the cold bar.

namespace RSL
{
    class Elemental
    {
    public:
        static Elemental& GetSingleton();

        static void Install();

        // Returns the queued nudge, in bar fractions, and clears it. Positive
        // warms. Called from the needs tick.
        float TakeQueued();

        // The same for the lesion counter: negative is damage. Bandages push it
        // the other way, which is why this is not just "damage taken".
        float TakeLesionP();

        // A RFAB_Bandage was used. Credited to the lesion counter.
        void NoteBandage();

        // Integrates what the damage-over-time effects did since the last call.
        // Keeps its own real-time clock: magnitudes are per real second.
        void Tick();

        // An elemental effect landed on the player.
        void Note(const RE::EffectSetting* a_effect);

        // So did the campfire power's effect, which this sink sees anyway.
        void NoteCampfire(const RE::EffectSetting* a_effect);

    private:

        // Turns damage into bar and queues it. Both paths end here.
        void Queue(float a_sign, float a_damage);

        // At most one LESION hit is counted per this many real seconds.
        static constexpr float EVENT_GAP = 0.5f;

        // A tick longer than this is not time the player spent burning - it is
        // a menu, a load or a fast travel. Integrating across it would hand him
        // the whole gap's worth of damage in one go.
        static constexpr float TICK_LIMIT = 5.0f;

        // The queue cannot grow past a fifth of the bar between ticks, however
        // much fire is flying about.
        static constexpr float QUEUE_LIMIT = 0.20f;

        // v0.4.0 caps the folded lesion damage at 40 between ticks, the same
        // way it caps the cold nudge at 20.
        static constexpr float LESION_LIMIT = 40.0f;

        std::atomic<float>                    _queued{ 0.0f };
        std::atomic<float>                    _lesionP{ 0.0f };
        std::chrono::steady_clock::time_point _last{};
        std::chrono::steady_clock::time_point _ticked{};
        bool                                  _seenAny{ false };
        bool                                  _tickedOnce{ false };
    };
}
