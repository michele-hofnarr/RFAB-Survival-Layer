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

        // Capped at the ceiling P can actually reach. Set higher - which only
        // a hand-edited ini can do, the slider stops at 100 - the counter would
        // sit at its floor for ever and the lesion could never be caught, with
        // nothing anywhere saying why.
        //
        // Read before the fold below rather than after, so the fold can say
        // what P is heading for as well as where it is.
        const float limit = std::min(100.0f, Settings::fElemLesionContractP);

        // Fold in what the hits and bandages did since the last pass. This is
        // done before the drift so a burst of damage counts even if the tick
        // that follows it is a healing one.
        if (const float pending = Elemental::GetSingleton().TakeLesionP();
            pending != 0.0f) {
            diseases.AddP(ID, pending);
            if (Settings::bDebugLog) {
                logger::info("elemental lesions: {:+.1f} folded in, P {:+.1f} "
                             "of -{:.0f} (stage {})",
                    pending, diseases.Get(ID).prog, limit, stage);
            }
        }

        const bool coldDeep = a_cold <= Settings::fElemLesionColdAt;

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
        // Which is why the cure half below stops at stage one. Past it the
        // spell is an ability no cure can take off, and a missing one is
        // evidence of a console removespell, not of medicine - so it is put
        // back instead of being read as a cure, and the pass carries on to
        // the drift below.
        //
        // What was wrong: nobody spent it at all. ElemLesion read P and drift
        // and never asked. Measured - "cure counted for EL" at 15:04:28, and
        // the stage did not move until 15:11:54, when P crossed the threshold
        // on its own. The potion did take the stage-1 effect off; our own
        // stage stayed where it was and went on dripping.
        //
        // AND THE STAGE-2/3 HALF WAS SIMPLY ABSENT. The three other illnesses
        // all carry it - StagedDisease::Advance, CommonCold::Update and
        // RfabDisease::Update each put a missing ability back and say so -
        // and the lesions were written without it for no reason anyone can
        // name. The cost: an ability taken off at stage 2 or 3 never came
        // back, so the illness went on running with no penalty on the player
        // and nothing anywhere saying it was still there.
        if (stage >= 1) {
            auto*      player = RE::PlayerCharacter::GetSingleton();
            auto*      current = forms.stage[stage - 1];
            const bool spellMissing = !current || !player || !player->HasSpell(current);

            if (stage >= 2) {
                if (spellMissing && current && player) {
                    player->AddSpell(current);
                    logger::info("elemental lesions: stage {} ability put back",
                        stage);
                }
            } else {
                // WHERE THE CURE CAME FROM, because the two are not the same
                // claim and the line could not tell them apart. One is a
                // potion this mod heard land; the other is an inference from
                // a spell that is no longer on the player, drawn after the
                // evidence for it has gone. They read identically to the
                // player - the illness announces that it has passed - and
                // when that announcement is the thing being questioned, this
                // is the only line that can answer it.
                auto       cures = diseases.TakeCures(ID);
                const bool guessed = cures == 0 && spellMissing;
                if (guessed) {
                    cures = 1;
                }

                if (cures > 0) {
                    StagedDisease::SetStage(forms, 0, stage);
                    diseases.HalveP(ID);
                    logger::info("elemental lesions: {} cure(s), stage 1 -> 0 ({})",
                        cures, guessed ? "the stage spell was gone, so a cure went unheard" : "a cure was counted");
                    return;
                }
            }
        }

        if (stage == 0) {
            // Not ill yet: P only ever sits at or below zero, and reaching the
            // threshold is what catches it.
            auto& state = diseases.Get(ID);
            state.prog = std::clamp(state.prog + drift * a_gameHours, -100.0f, 0.0f);

            if (state.prog <= -limit) {
                // TAKEN BEFORE THE RESET, because state is a reference into
                // the table and ResetP zeroes that same object. Logging
                // state.prog afterwards printed "contracted (P 0)" for every
                // contraction there has ever been, which is a line that
                // cannot tell a threshold that was reached from one that was
                // not. The ill branch below already does this correctly.
                const float caught = state.prog;
                StagedDisease::SetStage(forms, 1, 0);
                diseases.ResetP(ID);
                logger::info("elemental lesions: contracted (P {:.0f} of -{:.0f})",
                    caught, limit);
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
