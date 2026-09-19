#pragma once

// Frost and fire landing on the player move the cold bar.
//
// Frost chills, fire warms. Warming yourself on fire damage is deliberately
// usable - burning costs health, which makes it a real trade rather than an
// exploit.
//
// Hits are queued rather than applied where they land, and folded in on the
// needs tick, so every change to the axis goes through one place.
//
// WHAT MOVES THE BAR IS THE DAMAGE, AND THE DAMAGE IS TAKEN FROM THE ENGINE
// RATHER THAN WORKED OUT.
//
// ValueModifierEffect::ModifyActorValue is the one place an effect changes an
// actor value, and it is handed the actor, the value and which value it is. So
// that is where this listens. Nothing is asked about casting type, duration or
// magnitude: a frost effect took eleven health off the player, and eleven
// health is what the bar is charged for.
//
// WHY NOT THE APPLY EVENT, which is where this used to read the number from.
// Two faults, both measured rather than supposed:
//
//   - TESMagicEffectApplyEvent is dispatched from INSIDE the engine's add, so
//     at the moment it arrives the effect is not in the player's list yet and
//     there is no magnitude to read. Thirty hits over two minutes, fire and
//     frost alike, every one of them "0 entries in the list".
//
//   - Reading it one pass later fixed three hits in four and lost the fourth:
//     an instant effect can live less than a frame, and a pass that lands
//     after it has gone finds nothing. Five of twenty-two, in play.
//
// And a magnitude could not have been read straight even when it was there,
// because its UNIT depends on the effect. Ice Spike's 25 is twenty-five
// damage; Frostbite's 8 is eight per second; a firebolt trap's 4 is four per
// second for three seconds. The hook is paid the same thing in every one of
// those cases - what was actually taken, when it was taken.
//
// A ward that swallows a spell costs nothing to handle: the effect never
// applies anything, so nothing is ever heard. Resistance is off the number
// before it arrives, for the same reason.
//
// The other half is elemental lesions: frost, fire AND shock all damage a
// separate progression counter, which is why shock is noticed at all even
// though it never touches the cold bar.
//
// IT IS CHARGED THE SAME WAY, and used not to be. It counted HITS - a candle
// flame and a dragon's breath were worth the same four points - so it rode the
// apply event and needed a half-second debounce to stop a cloak effect opening
// thirty wounds a second. Both are gone: it reads the same number off the same
// hook as the cold bar, and a hit that takes nothing off the player is worth
// nothing. What differs is only what the number is scaled by, below.

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

        // An effect has just changed an actor value. From the hook, for every
        // effect on every actor - almost all of which is none of our business.
        //
        // a_actorValue is allowed to be kNone, and usually is: it means "the
        // one the effect carries" rather than "none". See the note in the cpp.
        void NoteDamage(const RE::ValueModifierEffect* a_effect, const RE::Actor* a_actor,
            float a_value, RE::ActorValue a_actorValue, const char* a_from);

        // The campfire power's effect, which the apply sink sees anyway.
        void NoteCampfire(const RE::EffectSetting* a_effect);

    private:
        // Turns damage into bar and queues it.
        void Queue(float a_sign, float a_damage);

        // ...and the same damage into P, which is a different scale and a
        // different resistance. See the definition.
        void QueueLesion(float a_damage);

        // The queue cannot grow past a fifth of the bar between ticks, however
        // much fire is flying about.
        static constexpr float QUEUE_LIMIT = 0.20f;

        // A fifth of P between ticks, which is the same share of its scale the
        // cold nudge is allowed of its own. v0.4.0's number was 40 against a
        // threshold of 70; the threshold is 100 now and the two caps say the
        // same thing again.
        static constexpr float LESION_LIMIT = 20.0f;

        std::atomic<float> _queued{ 0.0f };
        std::atomic<float> _lesionP{ 0.0f };
    };
}
