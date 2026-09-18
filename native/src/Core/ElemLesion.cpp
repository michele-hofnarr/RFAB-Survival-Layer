#include "PCH.h"

#include "Core/ElemLesion.h"

#include "Core/CommonCold.h"
#include "Core/Disease.h"
#include "Core/Elemental.h"
#include "Core/Forms.h"
#include "Core/StagedDisease.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view ID = "EL"sv;

        // An established lesion closes about three times faster than a fresh
        // scratch settles. v0.4.0's number, and it is what stops the "not quite
        // ill yet" state from lasting forever.
        constexpr float HEALING_SPEEDUP = 3.0f;

        [[nodiscard]] float DiseaseResist()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* owner = player ? player->AsActorValueOwner() : nullptr;
            return owner ? owner->GetActorValue(RE::ActorValue::kResistDisease) : 0.0f;
        }

        [[nodiscard]] float RandomPercent()
        {
            static std::mt19937                          gen{ std::random_device{}() };
            static std::uniform_real_distribution<float> dist{ 0.0f, 100.0f };
            return dist(gen);
        }
    }

    ElemLesion& ElemLesion::GetSingleton()
    {
        static ElemLesion singleton;
        return singleton;
    }

    std::int32_t ElemLesion::Stage() const
    {
        return Disease::GetSingleton().Stage(ID);
    }

    bool ElemLesion::Wounded() const
    {
        auto& diseases = Disease::GetSingleton();
        return diseases.Stage(ID) > 0 || diseases.Get(ID).prog < 0.0f;
    }

    void ElemLesion::ClearAll()
    {
        StagedDisease::Clear(Forms::elemLesion);
    }

    void ElemLesion::Update(float a_sleep, float a_hunger, float a_cold,
        float a_gameHours, bool a_undead)
    {
        const auto& forms = Forms::elemLesion;
        if (!forms.Valid()) {
            return;
        }

        auto&              diseases = Disease::GetSingleton();
        const std::int32_t stage = diseases.Stage(ID);

        if (!Settings::bModEnabled || !Settings::bElemLesionEnabled ||
            !Settings::bDiseasesEnabled || a_undead) {
            if (stage > 0) {
                ClearAll();
            }
            Elemental::GetSingleton().TakeLesionP();   // drop what was queued
            return;
        }

        // Fold in what the hits and bandages did since the last pass. This is
        // done before the drift so a burst of damage counts even if the tick
        // that follows it is a healing one.
        if (const float pending = Elemental::GetSingleton().TakeLesionP();
            pending != 0.0f) {
            diseases.AddP(ID, pending);
        }

        // Capped at the ceiling P can actually reach. Set higher - which only
        // a hand-edited ini can do, the slider stops at 100 - the counter would
        // sit at its floor for ever and the lesion could never be caught, with
        // nothing anywhere saying why.
        const float limit = std::min(100.0f, Settings::fElemLesionContractP);
        const bool  coldDeep = a_cold <= Settings::fElemLesionColdAt;

        float drift = 0.0f;
        if (coldDeep) {
            drift = -(100.0f / std::max(0.01f, Settings::fDiseaseProgressHours));
        } else if (AxisState(a_sleep, a_hunger, a_cold, a_undead) == AxisBand::kHeal) {
            drift = (100.0f / std::max(0.01f, Settings::fDiseaseDecayHours)) *
                    (1.0f + DiseaseResist() * 0.01f);
            if (stage >= 1) {
                drift *= HEALING_SPEEDUP;
            }
        }

        // A COUNTED CURE IS A CURE SPENT, and at stage one that is the whole
        // of it. Disease::ApplyCure is the one door every illness comes
        // through: at stage 1 it counts a cure to be spent here, past that it
        // eases P by fCurePotency instead and counts nothing, and hypothermia
        // is excused altogether because it is a state of the body rather than
        // an illness. So a lesion at stage 2 or 3 has already had what a
        // potion is worth to it by the time this runs.
        //
        // Which is why the fallback below stops at stage one. Past it the
        // spell is an ability no cure can take off, and a missing one is
        // evidence of a console removespell, not of medicine.
        //
        // What was wrong: nobody spent it at all. ElemLesion read P and drift
        // and never asked. Measured - "cure counted for EL" at 15:04:28, and
        // the stage did not move until 15:11:54, when P crossed the threshold
        // on its own. The potion did take the stage-1 effect off; our own
        // stage stayed where it was and went on dripping.
        if (stage == 1) {
            auto*      player = RE::PlayerCharacter::GetSingleton();
            auto*      current = forms.stage[0];
            const bool spellMissing = !current || !player || !player->HasSpell(current);

            auto cures = diseases.TakeCures(ID);
            if (cures == 0 && spellMissing) {
                cures = 1;
            }

            if (cures > 0) {
                StagedDisease::SetStage(forms, 0, stage);
                diseases.HalveP(ID);
                logger::info("elemental lesions: {} cure(s), stage 1 -> 0", cures);
                return;
            }
        }

        if (stage == 0) {
            // Not ill yet: P only ever sits at or below zero, and reaching the
            // threshold is what catches it.
            auto& state = diseases.Get(ID);
            state.prog = std::clamp(state.prog + drift * a_gameHours, -100.0f, 0.0f);

            if (state.prog <= -limit) {
                StagedDisease::SetStage(forms, 1, 0);
                diseases.ResetP(ID);
                logger::info("elemental lesions: contracted (P {:.0f})", state.prog);
                return;
            }

            // The other way in. Deep cold by itself is too brief a window -
            // hypothermia resolves it quickly one way or the other - so serious
            // hypothermia rolls for it instead, once per game hour.
            if (diseases.Stage("HY"sv) >= 2 && diseases.RollDue(ID, 1.0f) &&
                RandomPercent() < Settings::fElemLesionHypoChance) {
                StagedDisease::SetStage(forms, 1, 0);
                diseases.ResetP(ID);
                logger::info("elemental lesions: contracted from hypothermia");
            }
            return;
        }

        // Ill: deterministic, and crossing the same threshold either way moves
        // a stage. No roll, and no clamp pinning P at the bottom - v0.4.0 is
        // explicit that this one is meant to be predictable.
        auto&       state = diseases.Get(ID);
        const float before = state.prog;
        state.prog += drift * a_gameHours;

        std::int32_t step = 0;
        if (state.prog <= -limit) {
            step = 1;    // worse
        } else if (state.prog >= limit) {
            step = -1;   // better
        }

        if (step != 0) {
            const auto target = std::clamp(stage + step, 0, 3);
            diseases.ResetP(ID);
            if (target != stage) {
                StagedDisease::SetStage(forms, target, stage);
                logger::info("elemental lesions: stage {} -> {} (P {:.0f} -> 0)",
                    stage, target, before);
            }
        }
    }
}
