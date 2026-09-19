#pragma once

#include "Core/Disease.h"
#include "Core/Forms.h"

// One progressive illness, and the whole of what every illness does the same.
//
// THERE WERE FIVE COPIES OF THIS. StagedDisease, CommonCold, ElemLesion,
// RfabDisease and Hypothermia each carried their own version of the same pass -
// guard, cures, repair, progression, announcement - and the copies had already
// drifted apart. The repair that puts a missing stage-2 ability back existed in
// three of them and not the fourth, so an elemental lesion at stage 2 ran with
// no penalty on the player and nothing anywhere saying so; fixing it meant
// finding the same block four times. That is what this file is for.
//
// The rule for what belongs here: if an illness does it the same way as the
// others, it lives in Update() and nowhere else. If it does it differently,
// there is a hook for it below and the difference is an override - visible in
// that illness's header, next to the reason.
//
// Hypothermia is deliberately NOT one of these. It is a state of the body
// rather than an illness: medicine does not reach it, it has no axis bands, it
// runs a linear accumulator instead of a roll, and it locks the player down and
// blocks rest. Forcing it into this shape would mean five hooks that only it
// ever uses, which is the same duplication wearing a base class. It has its own
// in Core/BodyCondition.h.

namespace RSL
{
    // What a pass knows about the character. Five arguments were threaded
    // through five signatures before this; adding a sixth meant editing all of
    // them, which is how the reserves and the bars once ended up disagreeing
    // about which was which.
    //
    // The three axes are the EARNED halves, not the bars. A body running on
    // naps and snacks is not mending, and reading the sum let P heal on exactly
    // that.
    struct Tick
    {
        float sleep{ 1.0f };
        float hunger{ 1.0f };
        float cold{ 1.0f };
        float gameHours{ 0.0f };
        bool  undead{ false };
    };

    class Illness
    {
    public:
        // BY VALUE, not by reference. The RFAB wrappers keep their records in a
        // different shape and hand this one a view built from theirs, which
        // could not be a reference without outliving the expression that made
        // it. The struct is a dozen pointers; the copy costs nothing, and the
        // registry rebuilds every illness whenever the forms are resolved
        // again, so a copy cannot go stale.
        explicit Illness(const DiseaseForms& a_forms) :
            _forms(a_forms)
        {}

        virtual ~Illness() = default;

        Illness(const Illness&) = delete;
        Illness(Illness&&) = delete;
        Illness& operator=(const Illness&) = delete;
        Illness& operator=(Illness&&) = delete;

        // The co-save key. Two letters, and they must never change: renaming
        // one loses that illness on every save that exists.
        [[nodiscard]] std::string_view Id() const { return _forms.id; }

        [[nodiscard]] std::int32_t Stage() const;
        [[nodiscard]] float        P() const;

        // Are the records this illness needs actually resolved? False means the
        // plugin did not load or is older than this build, and the illness sits
        // out rather than dereferencing nothing.
        [[nodiscard]] virtual bool Ready() const { return _forms.Valid(); }

        // ONE PASS, and not virtual. The order is the same for every illness
        // and what differs is the hooks further down. Anything that wants to
        // change this order is asking for a sixth copy.
        void Update(const Tick& a_tick);

        // Put it on at stage 1, announced as newly caught. The hit sink and
        // the raw-food path reach for this from outside.
        void Contract(const char* a_why);

        // ...or roll for it first. a_chancePercent is taken as given:
        // resistance is the caller's business, because v0.4.0 applies it to a
        // hit and deliberately not to raw food.
        void Roll(float a_chancePercent, const char* a_why);

        // Take it off the player entirely - the master switch, this illness's
        // own switch, the player turning undead.
        virtual void Clear();

        // Which spell a given stage puts on the player. Public because the
        // report asks it of every illness in order to say whether our record
        // and the player's spell list still agree.
        [[nodiscard]] virtual RE::SpellItem* StageSpell(std::int32_t a_stage) const;

        [[nodiscard]] const DiseaseForms& Forms() const { return _forms; }

    protected:
        // ---- the hooks, and there are deliberately few -------------------

        // Is this illness allowed to run at all this pass?
        [[nodiscard]] virtual bool Enabled() const;

        // Stage 0: has it just been caught? The default says no - most
        // illnesses are caught by something that happens TO the player, on an
        // event, and Contract() is how that arrives.
        //
        // Overridden by the illnesses that catch themselves: the common cold
        // rolls against how cold you are, an elemental lesion crosses its own
        // threshold, an RFAB wrapper adopts a disease the engine already
        // applied.
        [[nodiscard]] virtual bool Contracts(const Tick&) { return false; }

        // Stages 1..3: how far the illness moved this pass, AS A CHANGE OF
        // STAGE. +1 is worse, -1 is better, 0 is neither.
        //
        // THE SIGN IS THE POINT. Disease::Step returns the opposite - positive
        // means P reached the HEALING end - and the lesions used the other
        // convention from the same engine, so two of the five copies read
        // `stage - net` and one read `stage + step` for the same idea. One
        // convention, stated here, and the conversion happens once.
        [[nodiscard]] virtual std::int32_t Progress(const Tick& a_tick);

        // Is the stage spell no longer on the player? Default is the spell
        // list. The RFAB wrappers override it: a creature bite applies the
        // disease's EFFECTS without the spell ever entering the list, so there
        // the question has to be asked of a marker effect instead.
        [[nodiscard]] virtual bool StageSpellGone(std::int32_t a_stage) const;

        // Say the one thing this transition means. Split out because the RFAB
        // wrappers number their messages differently - their stage 1 is RFAB's
        // own record and already announced itself - and because every illness
        // announces BEFORE the stage lands, so that nothing a stage does can
        // get in front of its own message.
        virtual void Announce(std::int32_t a_stage, std::int32_t a_old);

        // May the stage spell be taken off and put back to restart its effects?
        //
        // Only the RFAB wrappers say no, and only at stage 1: that is RFAB's
        // own record, their scripts watch it, and pulling it out from under
        // them to fix our problem is not ours to do.
        [[nodiscard]] virtual bool MayRestart(std::int32_t) const { return true; }

        // Anything this illness wants to do at the top of a pass, before the
        // stage is read for anything. The lesions fold in the damage that
        // arrived since the last one; the wrappers clear out stray instances
        // of RFAB's own spell.
        virtual void Observe(const Tick&, std::int32_t) {}

        // ...and the same for a pass that ends early because the illness is
        // switched off. The lesions drop what they had queued rather than
        // banking it for whenever the switch comes back.
        virtual void Idle() {}

        // Anything extra a transition needs. The wrappers dispel RFAB's own
        // spell when the illness clears entirely.
        virtual void OnStageChanged(std::int32_t, std::int32_t) {}

        // Extra columns for the trace line - whatever this illness is actually
        // driven by, which is not the same thing for all of them.
        [[nodiscard]] virtual std::string TraceExtra(const Tick&) const { return {}; }

        // ---- what the hooks are given to work with -----------------------

        // Move to an exact stage: announce, swap the spells, tell the subclass.
        void MoveTo(std::int32_t a_stage, std::int32_t a_old);

        [[nodiscard]] Disease& Diseases() const { return Disease::GetSingleton(); }

        // Which way the character's own state is pushing this illness, as the
        // last pass read it. Set by whichever Progress() computed one and read
        // by the trace - so an illness with no bands at all (the lesions run on
        // a threshold, not a roll) simply leaves it at kHold and says what it
        // is really driven by through TraceExtra.
        AxisBand _band{ AxisBand::kHold };

    private:
        // a_force prints regardless of the throttle: the pass that moves a
        // stage always says what it was reading, or the one line worth
        // having is the one most likely to be swallowed.
        void Trace(const Tick& a_tick, std::int32_t a_stage, bool a_force) const;

        DiseaseForms _forms;

        // Per illness, so one chatty illness cannot starve the rest of the
        // trace and a quiet one still reports.
        mutable std::chrono::steady_clock::time_point _tracedAt{};
        mutable bool                                  _traced{ false };

        // A stage spell on the player with nothing running behind it is put
        // back on. See the definition for the state this recovers from.
        void RestartDeadEffects(std::int32_t a_stage);

        // The check walks the player's active effects, so it is not free, and a
        // case the engine will never satisfy must not be retried every frame
        // for the rest of the session.
        std::chrono::steady_clock::time_point _checkedAt{};
        bool                                  _checked{ false };
        int                                   _restarts{ 0 };
    };
}
