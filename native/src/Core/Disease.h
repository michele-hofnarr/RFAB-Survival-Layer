#pragma once

// The progressive-stage disease engine, ported from _RSL_Disease.psc.
//
// Every disease in this mod - the from-scratch ones, food poisoning,
// hypothermia and the RFAB wrappers - runs on the same hidden accumulator,
// called P in the original and kept under that name here.
//
// P lives in -100..100 and drifts at a rate the caller sets. The engine offers
// two ways to turn drift into a stage change:
//
//   Step()       stochastic. Each sub-step rolls against |P|, and a hit moves
//                one stage and resets P to zero. This is what makes an illness
//                feel like it might turn either way rather than like a timer.
//
//   StepLinear() deterministic. P simply crosses +/-100. Used where the outcome
//                should be certain and only its timing is in question, which is
//                what hypothermia wants.
//
// State is per disease id and lives in the co-save, where v0.4.0 kept it in
// StorageUtil under "_RSL_Dz_<id>_*".

namespace RSL
{
    // Which way an illness is being pushed by how well the character is coping.
    //
    // v0.4.0's common cold had only two of these: any axis past its line pushed
    // P down, anything else pushed it up, so an illness was always moving one
    // way or the other. The middle band is deliberate - "not coping well enough
    // to recover, not badly enough to get worse" is a real state, and without
    // it a cold cleared while the character was visibly run down.
    enum class AxisBand : std::uint8_t
    {
        kWorsen,   // at least one axis is below half - P falls
        kHold,     // neither rule fired - P stops moving, the roll goes on
        kHeal      // every axis is above its safe mark - P rises
    };

    [[nodiscard]] std::string_view AxisBandName(AxisBand a_band);

    class Disease
    {
    public:
        struct State
        {
            std::int32_t stage{ 0 };       // 0..3, 0 = not ill
            std::int32_t cures{ 0 };       // counted cure-disease hits pending
            float        prog{ 0.0f };     // P, the hidden accumulator
            float        lastRollDays{ 0.0f };
            float        stage3Seconds{ 0.0f };
        };

        static Disease& GetSingleton();

        // Build the set of magic effects that count as a cure.
        //
        // v0.4.0 could not do this: the generator scanned five named plugins
        // for the Cure Disease archetype and baked seven form ids into
        // IsCureEffect, which then had to be regenerated whenever the load
        // order changed. Here every effect the game loaded is examined at
        // startup, so a cure from any mod counts and nothing needs updating.
        static void ScanCureEffects();

        [[nodiscard]] static bool IsCureEffect(RE::FormID a_effect);

        // A cure landed. Every illness currently running takes one step back,
        // which is what a counted cure means.
        void ApplyCure();

        // Keep the hidden disease marker in step with the illnesses.
        //
        // Stages 2 and 3 are abilities, so the engine no longer sees an
        // advanced illness as a disease at all - and the vanilla
        // WICommentDiseased quest, which is what makes strangers remark on a
        // sick traveller, tests exactly that (condition 39, GetDisease). The
        // marker is a Type=Disease spell carrying one hidden library effect at
        // magnitude zero - enough for the engine to instantiate something,
        // nothing for the player to see. It is on whenever an illness stands at
        // stage 2 or worse, and off otherwise.
        //
        // A cure strips it, being a disease. That is fine: the next pass puts
        // it back, and nothing visible rides on it to flicker.
        void SyncMarker();

        [[nodiscard]] State&       Get(std::string_view a_id);
        [[nodiscard]] std::int32_t Stage(std::string_view a_id);

        // True at most once per a_perHours of game time, stamping on success,
        // so a caller reads: if (RollDue(id, 1.0f) && roll < chance) ...
        bool RollDue(std::string_view a_id, float a_perHours);

        void SetStage(std::string_view a_id, std::int32_t a_stage,
            RE::SpellItem* a_previous, RE::SpellItem* a_next);

        void ClearStages(std::string_view a_id,
            RE::SpellItem* a_s1, RE::SpellItem* a_s2, RE::SpellItem* a_s3);

        std::int32_t TakeCures(std::string_view a_id);
        void         AddCure(std::string_view a_id);

        void ResetP(std::string_view a_id);
        void HalveP(std::string_view a_id);
        void AddP(std::string_view a_id, float a_delta);

        // Stochastic. Returns -1, 0 or +1 - net capped either way so the
        // frequent tick catches up a step at a time rather than lurching.
        std::int32_t StepManual(std::string_view a_id, float a_drift, float a_hours);

        // Derives the drift from the axis band and delegates. Disease
        // resistance tilts both directions. kHold means a drift of zero: P
        // holds its value but still rolls, so the illness keeps living rather
        // than parking mid-progress.
        std::int32_t Step(std::string_view a_id, AxisBand a_band, float a_hours,
            float a_diseaseResist, float a_worsenHours, float a_recoverHours);

        // Deterministic threshold crossing.
        std::int32_t StepLinear(std::string_view a_id, float a_drift, float a_hours);

        // One line per illness that has anything to say: stage, P, cures
        // pending, and whether the stage spell is actually on the player.
        //
        // EVERY MODULE HERE LOGS TRANSITIONS AND ONLY TRANSITIONS, which is
        // right for a log that runs all session and useless the moment
        // somebody turns the switch on to look into something: an illness
        // that is merely sitting there has already said its piece, hours
        // ago, at a log level that threw it away. This is the standing
        // state, asked for rather than waited for.
        //
        // The spell column is the one that matters. Our stage and the
        // player's spell list are two records of the same fact, and every
        // illness here treats them disagreeing as evidence - a cure nobody
        // heard, at stage 1; a console removespell, past it. When one of
        // those inferences fires there is no way to check it afterwards,
        // because the thing it read is gone. This prints it while it is
        // still there.
        void Report(std::string_view a_why);

        void Clear();

        [[nodiscard]] auto& All() { return _states; }

    private:
        std::map<std::string, State, std::less<>> _states;

        static inline std::unordered_set<RE::FormID> _cureEffects;
    };
}
