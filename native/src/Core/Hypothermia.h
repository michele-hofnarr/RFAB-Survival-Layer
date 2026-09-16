#pragma once

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

namespace RSL
{
    class Hypothermia
    {
    public:
        static Hypothermia& GetSingleton();

        // a_cold is the reserve, 0..1. a_realSeconds is wall time since the
        // last call, which is what the stage-3 drain is measured in.
        //
        // a_warmthSlow is Climate::ChillSlow() - the same divisor the cold axis
        // falls by. Warmth does not only decide how fast you get here; it
        // decides how fast it gets worse once you are. Passed in rather than
        // read, so the illness and the axis cannot end up using two different
        // readings of the same surroundings.
        void Update(float a_cold, float a_gameHours, float a_realSeconds, bool a_undead,
            float a_warmthSlow);

        // Lift everything: the lockdown, the rest block, the stage spells.
        void ClearAll();

        // Drop what we believe about the ENGINE - rest blocked, controls
        // locked. A loaded game brings its own answers to both, so ours are
        // dropped and re-applied by the next tick rather than carried across
        // from the last one.
        void Forget();

        [[nodiscard]] std::int32_t Stage() const;

    private:
        void SetStage(std::int32_t a_stage);
        void SetLock(bool a_on, bool a_nudge);
        void SyncRestBlock();


        [[nodiscard]] static RE::SpellItem* StageSpell(std::int32_t a_stage);

        bool _restBlocked{ false };

        // Whether that cached answer means anything yet. It describes the
        // ENGINE's state, and a load replaces the engine's - so after one, the
        // first tick applies whatever it decides instead of comparing against a
        // value inherited from the last game. Without this the block stuck: the
        // log showed rest blocked once, and a later stage one made no call at
        // all because the stale cache already said "blocked".
        bool _restKnown{ false };
        bool _locked{ false };

        // Stage three announces itself before it drops the player. The
        // collapse is held for a moment so the message is on screen first -
        // see the note in SetStage.
        // Last attempt at restarting an ability the engine accepted but never
        // ran. Throttled so a genuinely impossible case does not thrash.



    };
}
