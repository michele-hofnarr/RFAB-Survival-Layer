#pragma once

#include "Core/Illness.h"

// The seven of RFAB's own diseases given a progression.
//
// Stage 1 is RFAB's record, untouched, carrying its debuff and its Peryite
// boon. Stages 2 and 3 are ours, built by the generator as a copy of RFAB's
// effect list minus the boons. So this is not a disease this mod invented; it
// is a second and third act bolted onto one of theirs, and most of the
// difficulty is in telling their instance from our copy of it.
//
// Four things make that hard, and every one of them is why a hook is overridden
// here rather than bent into the shared pass:
//
//   * A creature bite applies the disease's EFFECTS without the SPEL ever
//     entering the spell list, so HasSpell misses it entirely - which is what
//     StageSpellGone exists to let this illness answer differently. Catching it
//     watches for the marker effect instead: the first, unconditional debuff.
//
//   * Our stage 2/3 copies contain that same marker, because the generator
//     copied RFAB's whole effect list. So the probe only means anything while
//     neither of our copies is on; otherwise "afflicted" reads true at every
//     stage for ever.
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
// lock there - which is the intended way out for a Peryite follower. Dragonborn
// 's Droops is not covered by the blessing, which is why the flag is passed in
// per illness rather than tested as an index against five, as it used to be.

namespace RSL
{
    struct RfabDiseaseForms;

    class RfabIllness : public Illness
    {
    public:
        RfabIllness(const RfabDiseaseForms& a_src, bool a_blessingFreezes);

        // Take back the two stages this layer added and leave RFAB's illness
        // exactly where it was found.
        //
        // STEPPING ASIDE IS NOT CURING. What stood here removed RFAB's own
        // record and dispelled it, so flicking the mod off cured an RFAB
        // disease outright - and worse at stage 2 or 3, where OUR copy is the
        // spell on the player and RFAB's base has been swapped out: clearing
        // our stages there left the player with no illness at all and nothing
        // to say it had gone.
        void Clear() override;

        [[nodiscard]] bool Ready() const override;

    protected:
        [[nodiscard]] bool Enabled() const override;

        void Observe(const Tick& a_tick, std::int32_t a_stage) override;

        [[nodiscard]] bool Contracts(const Tick& a_tick) override;
        [[nodiscard]] bool StageSpellGone(std::int32_t a_stage) const override;
        [[nodiscard]] bool Held(std::int32_t a_stage) override;

        void Announce(std::int32_t a_stage, std::int32_t a_old) override;
        void OnStageChanged(std::int32_t a_old, std::int32_t a_stage) override;

        [[nodiscard]] std::string TraceExtra(const Tick& a_tick) const override;

    private:
        // The shared pass works from DiseaseForms; RFAB's records are a
        // different shape. This maps one onto the other - stage 1 is their
        // record, 2 and 3 are ours, and the "caught" message is deliberately
        // absent because their record announces itself.
        [[nodiscard]] static DiseaseForms AsCommon(const RfabDiseaseForms& a_src);

        const RfabDiseaseForms& _src;
        bool                    _blessingFreezes{ false };

        // Read once at the top of each pass: is RFAB's own illness on the
        // player at all, and is one of our copies wearing its effect names?
        bool _afflicted{ false };
        bool _ourCopyOn{ false };

        // "The marker has gone quiet at least once since the last cure."
        // Transient on purpose: v0.4.0 defaults it to true when unset, so a
        // fresh session behaving as if the player were clean matches it.
        bool _seenClean{ true };
    };
}
