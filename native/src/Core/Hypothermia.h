#pragma once

#include "Core/BodyCondition.h"

// Hypothermia: what happens once the cold bar bottoms out.
//
// Ported from AdvanceHypothermia. Three stages, entered the moment cold passes
// the threshold and then advanced by a deterministic accumulator rather than a
// roll - once you are that cold the outcome is not in doubt, only its timing.
//
// Stage 3 locks the player down and bleeds current health on a curve that grows
// with time spent there, so a big health pool or heavy regeneration buys time
// and nothing more.
//
// Rest is blocked from stage 1, with one exception: indoors and warming up. A
// warm shelter is exactly where hypothermia is meant to be slept off.
//
// It is the only BodyCondition there is. That is not an argument for folding it
// back in with the illnesses - see the note on the base for the four ways it is
// not one of them, every one of which had to be written as an exception inside
// the illnesses while it was filed as one.

namespace RSL
{
    class Hypothermia : public BodyCondition
    {
    public:
        static Hypothermia& GetSingleton();

        // a_realSeconds is wall time since the last call, which is what the
        // stage-3 drain is measured in.
        //
        // a_warmthSlow is Climate::ChillSlow() - the same divisor the cold axis
        // falls by. Warmth does not only decide how fast you get here; it
        // decides how fast it gets worse once you are. PASSED IN rather than
        // read, so the condition and the axis cannot end up using two different
        // readings of the same surroundings.
        void Update(const Tick& a_tick, float a_realSeconds, float a_warmthSlow);

        // Lift everything: the lockdown, the rest block, the stage spells.
        void Clear() override;

        void Forget() override;

    protected:
        [[nodiscard]] bool Enabled() const override;
        [[nodiscard]] bool Onsets(const Tick& a_tick) override;

        [[nodiscard]] std::int32_t Progress(const Tick& a_tick,
            float a_realSeconds) override;

        void Maintain(const Tick& a_tick, float a_realSeconds) override;
        void OnStageChanged(std::int32_t a_old, std::int32_t a_stage) override;

        [[nodiscard]] std::string TraceExtra(const Tick& a_tick) const override;

    private:
        // The records are a static that Forms::Load fills, and the base
        // holds a reference to it - so the singleton may be constructed
        // before or after the resolve without caring which.
        Hypothermia() :
            BodyCondition(Forms::hypothermia)
        {}

        void SetLock(bool a_on, bool a_nudge);
        void SyncRestBlock();

        // This pass's reading of how much the character's clothing is slowing
        // the cold down. Handed in by the caller with the rest of the tick.
        float _warmthSlow{ 1.0f };

        bool _restBlocked{ false };

        // Whether that cached answer means anything yet. It describes the
        // ENGINE's state, and a load replaces the engine's - so after one, the
        // first pass applies whatever it decides instead of comparing against a
        // value inherited from the last game. Without this the block stuck: the
        // log showed rest blocked once, and a later stage made no call at all
        // because the stale cache already said "blocked".
        bool _restKnown{ false };
        bool _locked{ false };
    };
}
