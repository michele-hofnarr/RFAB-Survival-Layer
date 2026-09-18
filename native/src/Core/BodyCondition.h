#pragma once

#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Core/Illness.h"

// A STATE OF THE BODY, which is not an illness and must not be filed as one.
//
// Hypothermia runs on the same accumulator and keeps its stage in the same
// co-save table, and there the resemblance stops:
//
//   * no medicine reaches it. Not a potion, not an altar, not a spell - only
//     warmth. All three of its stages are abilities for exactly that reason,
//     where an illness keeps stage 1 as a Type=Disease spell so a cure CAN
//     reach it.
//   * no axis bands. Sleep and food do not thaw anyone; only the cold axis and
//     how well the character is dressed decide anything here.
//   * a linear accumulator, not a roll. Once you are that cold the outcome is
//     not in doubt, only its timing.
//   * it reaches outside itself - locking the player down, blocking rest,
//     bleeding health - which no illness does.
//
// It used to be an exception INSIDE the illnesses: skipped in ApplyCure by
// comparing its id to the letters "HY", skipped again in SyncMarker the same
// way, and otherwise a fifth hand-written copy of the stage bookkeeping. Being
// a different kind of thing is now a different type, so nothing has to remember
// to exclude it.
//
// What IS shared, and is all this base holds: three stages kept in
// Disease::State, a record set of three spells and six messages, one message
// per transition announced before the stage lands, and a trace.

namespace RSL
{
    class BodyCondition
    {
    public:
        // BY REFERENCE, unlike the illnesses. There is one condition and its
        // records live in a static that outlives everything, so there is no
        // synthesized view to dangle and no build step to keep a copy in step
        // with - it simply reads whatever Forms::Load last resolved.
        explicit BodyCondition(const DiseaseForms& a_forms) :
            _forms(a_forms)
        {}

        virtual ~BodyCondition() = default;

        BodyCondition(const BodyCondition&) = delete;
        BodyCondition(BodyCondition&&) = delete;
        BodyCondition& operator=(const BodyCondition&) = delete;
        BodyCondition& operator=(BodyCondition&&) = delete;

        [[nodiscard]] std::string_view Id() const { return _forms.id; }
        [[nodiscard]] std::int32_t     Stage() const;
        [[nodiscard]] float            P() const;
        [[nodiscard]] bool             Ready() const { return _forms.Valid(); }

        [[nodiscard]] RE::SpellItem* StageSpell(std::int32_t a_stage) const;

        // Lift everything this has put on the player.
        virtual void Clear();

        // Drop what we believe about the ENGINE - not about the condition.
        // A loaded game brings its own answers to whatever was toggled on our
        // behalf, so ours are dropped and re-applied by the next pass rather
        // than carried across from the last one.
        virtual void Forget() {}

    protected:
        // One pass. Same shape as the illnesses' - guard, onset, progression,
        // transition - with the parts that do not apply left out rather than
        // stubbed.
        void Run(const Tick& a_tick, float a_realSeconds);

        [[nodiscard]] virtual bool Enabled() const = 0;

        // Stage 0: has it begun? Onset is a threshold rather than a roll, and
        // it is instant - crossing the line goes straight to stage 1 instead of
        // starting an accumulator.
        [[nodiscard]] virtual bool Onsets(const Tick& a_tick) = 0;

        // Stages 1..3: the change of stage this pass. +1 worse, -1 better, the
        // same convention the illnesses use.
        [[nodiscard]] virtual std::int32_t Progress(const Tick& a_tick,
            float a_realSeconds) = 0;

        // Whatever the engine has to be told on EVERY pass, at every stage,
        // including one that ends with the condition switched off. The rest
        // block is the reason this exists: nothing may leave it on for ever,
        // and stage 0 can be arrived at without any transition - a load resets
        // the stage in memory while the engine's own flag is part of the save.
        virtual void Maintain(const Tick&, float) {}

        virtual void OnStageChanged(std::int32_t, std::int32_t) {}

        [[nodiscard]] virtual std::string TraceExtra(const Tick&) const { return {}; }

        void SetStage(std::int32_t a_stage);

        [[nodiscard]] Disease& Diseases() const { return Disease::GetSingleton(); }

        // Set by whatever computed one, printed by the trace.
        std::string_view _drive{ "hold"sv };

    private:
        // A stage spell on the player with nothing running behind it is
        // put back on. Hypothermia is where this was first measured, in
        // e275a60: AddSpell accepted, HasSpell true across a reload, and
        // no ActiveEffect ever created.
        void RestartDeadEffects(std::int32_t a_stage);

        void Announce(std::int32_t a_stage, std::int32_t a_old) const;
        void Trace(const Tick& a_tick, std::int32_t a_stage, bool a_force) const;

        const DiseaseForms& _forms;

        mutable std::chrono::steady_clock::time_point _tracedAt{};
        mutable bool                                  _traced{ false };

        std::chrono::steady_clock::time_point _checkedAt{};
        bool                                  _checked{ false };
        int                                   _restarts{ 0 };
    };
}
