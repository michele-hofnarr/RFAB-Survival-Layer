#include "PCH.h"

#include "Core/StagedDisease.h"

#include "Core/CommonCold.h"
#include "Core/Disease.h"
#include "Core/Notify.h"
#include "Settings.h"

namespace RSL::StagedDisease
{
    namespace
    {
        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        [[nodiscard]] float DiseaseResist()
        {
            auto* player = Player();
            auto* owner = player ? player->AsActorValueOwner() : nullptr;
            return owner ? owner->GetActorValue(RE::ActorValue::kResistDisease) : 0.0f;
        }

        [[nodiscard]] RE::SpellItem* StageSpell(const DiseaseForms& a_forms, std::int32_t a_stage)
        {
            if (a_stage < 1 || a_stage > 3) {
                return nullptr;
            }
            return a_forms.stage[a_stage - 1];
        }
    }

    void SetStage(const DiseaseForms& a_forms, std::int32_t a_stage, std::int32_t a_old)
    {
        // Announced before the stage is applied, for the same reason
        // hypothermia does it: nothing a stage does should be able to get in
        // front of its own message.
        auto& notify = Notify::GetSingleton();
        if (a_stage == 0) {
            notify.Push(a_forms.cured);
        } else if (a_stage > a_old) {
            notify.Push(a_forms.msg[a_stage - 1]);
        } else if (a_stage < a_old) {
            notify.Push(a_forms.ease[a_stage == 2 ? 0 : 1]);
        }

        Disease::GetSingleton().SetStage(a_forms.id, a_stage,
            StageSpell(a_forms, a_old), StageSpell(a_forms, a_stage));
    }

    void Roll(const DiseaseForms& a_forms, float a_chancePercent, const char* a_why)
    {
        if (!a_forms.Valid() || !Settings::bModEnabled || !Settings::bDiseasesEnabled) {
            return;
        }
        auto* player = Player();
        if (!player || (Forms::kwUndead && player->HasKeyword(Forms::kwUndead))) {
            return;
        }
        if (Disease::GetSingleton().Stage(a_forms.id) > 0) {
            return;
        }
        if (a_chancePercent <= 0.0f) {
            return;
        }

        static std::mt19937                          gen{ std::random_device{}() };
        static std::uniform_real_distribution<float> dist{ 0.0f, 100.0f };
        if (dist(gen) >= a_chancePercent) {
            return;
        }

        SetStage(a_forms, 1, 0);
        Disease::GetSingleton().ResetP(a_forms.id);
        logger::info("disease {}: contracted {} (chance {:.0f}%)",
            a_forms.id, a_why, a_chancePercent);
    }

    float GutwormHungerMult()
    {
        switch (Disease::GetSingleton().Stage("GW"sv)) {
        case 3:  return 2.5f;
        case 2:  return 1.7f;
        case 1:  return 1.3f;
        default: return 1.0f;
        }
    }

    float BrownRotSleepMult()
    {
        switch (Disease::GetSingleton().Stage("BR"sv)) {
        case 3:  return 0.7f;
        case 2:  return 0.8f;
        case 1:  return 0.9f;
        default: return 1.0f;
        }
    }

    float GutwormFoodPenalty()
    {
        switch (Disease::GetSingleton().Stage("GW"sv)) {
        case 3:  return 0.8f;
        case 2:  return 0.5f;
        case 1:  return 0.25f;
        default: return 0.0f;
        }
    }

    void Contract(const DiseaseForms& a_forms)
    {
        if (!a_forms.Valid()) {
            return;
        }
        auto& diseases = Disease::GetSingleton();
        if (diseases.Stage(a_forms.id) > 0) {
            return;   // already ill; a second dose is not a worse dose
        }
        SetStage(a_forms, 1, 0);
        diseases.ResetP(a_forms.id);
        logger::info("disease {}: contracted", a_forms.id);
    }

    void Clear(const DiseaseForms& a_forms)
    {
        if (!a_forms.Valid()) {
            return;
        }
        Disease::GetSingleton().ClearStages(a_forms.id,
            a_forms.stage[0], a_forms.stage[1], a_forms.stage[2]);
    }

    std::int32_t Advance(const DiseaseForms& a_forms, float a_sleep, float a_hunger,
        float a_cold, float a_gameHours, bool a_undead)
    {
        if (!a_forms.Valid()) {
            return 0;
        }

        auto&              diseases = Disease::GetSingleton();
        const std::int32_t stage = diseases.Stage(a_forms.id);

        if (!Settings::bDiseasesEnabled || a_undead || !Settings::bModEnabled) {
            if (stage > 0) {
                Clear(a_forms);
            }
            return 0;
        }

        if (stage <= 0) {
            return 0;   // catching it is somebody else's job
        }

        // Cures first: a counted Cure Disease effect walks it back a stage and
        // halves what had accumulated towards the next one.
        //
        // A cure another mod fired that the scan missed, or a console
        // removespell, leaves the stage spell gone with nothing counted. v0.4.0
        // treats that as one cure rather than letting the illness sit there
        // with no effect to show for it.
        auto*      player = Player();
        auto*      current = StageSpell(a_forms, stage);
        const bool spellMissing = !current || !player || !player->HasSpell(current);

        // Past stage 1 the spell is an ABILITY, so no cure can reach it and a
        // missing one is not evidence of anything a cure did - only of a
        // console removespell or another mod. Put it back, skip the cure
        // bookkeeping, and CARRY ON TO THE DRIFT BELOW.
        //
        // Carrying on is the whole point. Returning here instead - which is what
        // this did - froze stages 2 and 3 solid: P never moved, so the illness
        // could not worsen and could not heal, and no cure reaches an ability.
        // A permanent, unremovable disease, and it made fCurePotency dead code
        // into the bargain.
        if (stage >= 2) {
            if (spellMissing && current && player) {
                player->AddSpell(current);
                logger::info("disease {}: stage {} ability put back", a_forms.id, stage);
            }
        } else {
            // Counted or inferred - see the note in ElemLesion for why the
            // line has to say which.
            auto       cures = diseases.TakeCures(a_forms.id);
            const bool guessed = cures == 0 && spellMissing;
            if (guessed) {
                cures = 1;
            }
            if (cures > 0) {
                const auto target = std::max(0, stage - cures);
                SetStage(a_forms, target, stage);
                diseases.HalveP(a_forms.id);
                logger::info("disease {}: {} cure(s), stage {} -> {} ({})",
                    a_forms.id, cures, stage, target,
                    guessed ? "the stage spell was gone, so a cure went unheard" : "a cure was counted");
                return target;
            }
        }

        const AxisBand band = AxisState(a_sleep, a_hunger, a_cold, a_undead);
        const auto     net = diseases.Step(a_forms.id, band, a_gameHours, DiseaseResist(),
                Settings::fDiseaseProgressHours, Settings::fDiseaseDecayHours);
        if (net == 0) {
            return stage;
        }

        // A positive net means P reached the healing end, so the stage comes
        // down - the sign is inverted on purpose.
        const auto target = std::clamp(stage - net, 0, 3);
        if (target != stage) {
            SetStage(a_forms, target, stage);
            logger::info("disease {}: stage {} -> {} (band {})",
                a_forms.id, stage, target, AxisBandName(band));
        }
        return target;
    }
}
