#pragma once

// The seven of RFAB's own diseases given a progression.
//
// Stage 1 is RFAB's record, untouched, carrying its debuff and its Peryite
// boon. Stages 2 and 3 are ours, built by the generator as a copy of RFAB's
// effect list minus the boons. So this is not a disease we invented; it is a
// second and third act bolted onto one of theirs, and most of the difficulty
// is in telling their instance from our copy of it.
//
// Four things make that hard, and every one of them is load-bearing:
//
//   * A creature bite applies the disease's EFFECTS without the SPEL ever
//     entering the spell list, so HasSpell misses it entirely. Catching it has
//     to watch for the marker effect instead - the first, unconditional debuff.
//
//   * Our stage 2/3 copies contain that same marker, because the generator
//     copied RFAB's whole effect list. So the probe only means anything while
//     neither of our copies is on; otherwise "afflicted" reads true at every
//     stage forever.
//
//   * After a cure the marker can linger a tick or two. Without requiring it to
//     go quiet once first, a cure loops straight back into stage 1.
//
//   * At stage 2 or 3 a fresh bite re-applies RFAB's own effects on top of our
//     copy. Dispelling the base spell removes those and leaves ours alone, and
//     whether it removed anything is the only way to tell a real stray instance
//     from our copy showing RFAB's effect names.
//
// The Peryite blessing freezes the six base-game ones at stage 1: that is
// RFAB's own balance and this layer does not get to touch it. Stages 2 and 3
// still run, so a disease already in progress can settle back down to 1 and
// lock there - which is the intended way out for a Peryite follower.

namespace RSL
{
    struct RfabDiseaseForms;

    class RfabDisease
    {
    public:
        static RfabDisease& GetSingleton();

        static constexpr int COUNT = 7;

        void Update(float a_sleep, float a_hunger, float a_cold,
            float a_gameHours, bool a_undead);

        void ClearAll();

    private:
        // Take back the two stages this layer added and leave RFAB's own
        // illness on the player. Used wherever the layer stops running.
        static void HandBack(const RfabDiseaseForms& a_forms);

        void UpdateOne(int a_index, float a_sleep, float a_hunger, float a_cold,
            float a_gameHours, bool a_undead);

        // "The marker has gone quiet at least once since the last cure."
        // Transient on purpose: v0.4.0 defaults it to true when unset, so a
        // fresh session behaving as if the player were clean matches it.
        bool _seenClean[COUNT]{ true, true, true, true, true, true, true };
    };
}
