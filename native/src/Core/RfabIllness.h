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
// The Peryite blessing stops P for the six base-game ones at stage 1, AND THAT
// IS THE WHOLE OF IT. Stage 1 is RFAB's own record and carries their Peryite
// boon, so this layer must not drive it anywhere - but it does not hold the
// player ill either. Medicine reaches it like any other disease, a cure takes
// it off, and stages 2 and 3 run normally, so a disease already in progress
// settles back to 1 and stops there.
//
// It used to do more, and none of the rest was ever asked for. The same branch
// re-added RFAB's disease spell whenever it found it gone - "re-assert if a
// stray cure stripped it" - which cannot tell a player who drank a cure from
// some hypothetical third party, so under the blessing these six could not be
// cured at all: the potion stripped the spell and the next pass put it back.
// It also discarded the counted cure and returned before the cure block ran,
// so nothing downstream ever saw the attempt. All of it went in with the
// freeze in 5f2bda0 and none of it was in the version before.
//
// Dragonborn's Droops is not covered by the blessing, which is why the flag is
// passed in per illness rather than tested as an index against five.

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

        // P does not move while the blessing holds this at stage 1. Everything
        // else about the pass is left alone.
        [[nodiscard]] std::int32_t Progress(const Tick& a_tick) override;

        // Stage 1 is RFAB's own disease record. Their scripts watch it,
        // and taking it off the player to restart OUR effects is not ours
        // to do. Stages 2 and 3 are our copies and may be restarted.
        [[nodiscard]] bool MayRestart(std::int32_t a_stage) const override;

        void Announce(std::int32_t a_stage, std::int32_t a_old) override;
        void OnStageChanged(std::int32_t a_old, std::int32_t a_stage) override;

        [[nodiscard]] std::string TraceExtra(const Tick& a_tick) const override;

    private:
        // The shared pass works from DiseaseForms; RFAB's records are a
        // different shape. This maps one onto the other - stage 1 is their
        // record, 2 and 3 are ours, and the "caught" message is deliberately
        // absent because their record announces itself.
        [[nodiscard]] static DiseaseForms AsCommon(const RfabDiseaseForms& a_src);

        // Is the Peryite blessing holding this one still right now?
        [[nodiscard]] bool Frozen() const;

        const RfabDiseaseForms& _src;
        bool                    _blessingFreezes{ false };

        // For the trace, so a frozen illness says so rather than looking like
        // one that simply is not moving.
        bool _frozen{ false };

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
