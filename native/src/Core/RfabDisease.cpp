#include "PCH.h"

#include "Core/RfabDisease.h"

#include "Core/CommonCold.h"
#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Core/Notify.h"
#include "Core/StagedDisease.h"
#include "Settings.h"

namespace RSL
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

        // Papyrus has Actor.DispelSpell and CommonLibSSE does not bind it, so
        // this is it: end every running effect that came from this spell, and
        // report whether anything was actually there.
        //
        // That return value is not incidental. At stage 2 or 3 our own copy
        // carries RFAB's effect names, so "is RFAB's disease on me" cannot be
        // answered by looking; the only reliable answer is whether dispelling
        // their spell removed something.
        //
        // Collected first and dispelled after, because Dispel mutates the list
        // being walked.
        [[nodiscard]] bool DispelSpell(RE::Actor* a_actor, RE::MagicItem* a_spell)
        {
            auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!a_spell || !effects) {
                return false;
            }

            std::vector<RE::ActiveEffect*> doomed;
            for (auto* active : *effects) {
                if (active && active->spell == a_spell) {
                    doomed.push_back(active);
                }
            }
            for (auto* active : doomed) {
                active->Dispel(true);
            }
            return !doomed.empty();
        }

        // Stage 1 is RFAB's own record; 2 and 3 are ours.
        [[nodiscard]] RE::SpellItem* StageSpell(const RfabDiseaseForms& a_forms,
            std::int32_t a_stage)
        {
            switch (a_stage) {
            case 1:  return a_forms.base;
            case 2:  return a_forms.ours[0];
            case 3:  return a_forms.ours[1];
            default: return nullptr;
            }
        }

        void Announce(const RfabDiseaseForms& a_forms, std::int32_t a_stage, std::int32_t a_old)
        {
            auto& notify = Notify::GetSingleton();
            if (a_stage == 0) {
                notify.Push(a_forms.cured);
            } else if (a_stage > a_old && a_stage >= 2) {
                notify.Push(a_forms.msg[a_stage - 2]);
            } else if (a_stage < a_old && a_stage >= 1) {
                // Stepping DOWN said nothing at all before, so an RFAB illness
                // announced every worsening and went quiet the moment it began
                // to lift - the one direction the player most wants confirmed.
                // ease[0] is "eased to 2", ease[1] is "eased to 1"; stage 1 is
                // RFAB's own record again, which is exactly what the message
                // says.
                notify.Push(a_forms.ease[a_stage == 2 ? 0 : 1]);
            }
        }
    }

    RfabDisease& RfabDisease::GetSingleton()
    {
        static RfabDisease singleton;
        return singleton;
    }

    void RfabDisease::HandBack(const RfabDiseaseForms& a_forms)
    {
        // STEPPING ASIDE IS NOT CURING. Stage 1 is RFAB's own record; this layer
        // adopts it and adds two stages above it, and that is all. When the
        // layer stops running - the master switch, its own switch, a vampire -
        // it takes back what it added and leaves RFAB's illness exactly where it
        // found it.
        //
        // What stood here removed forms.base and dispelled it, so flicking the
        // mod off cured an RFAB disease outright. Worse at stage 2 or 3, where
        // OUR copy is the spell on the player and RFAB's base has been swapped
        // out: clearing our stages there would have left the player with no
        // illness at all and nothing to say it had gone.
        auto* player = Player();
        auto& diseases = Disease::GetSingleton();
        const bool wasIll = diseases.Stage(a_forms.id) > 0;

        // Ours, and only ours - note the nullptr where base used to be.
        diseases.ClearStages(a_forms.id, nullptr, a_forms.ours[0], a_forms.ours[1]);

        if (wasIll && player && a_forms.base && !player->HasSpell(a_forms.base)) {
            player->AddSpell(a_forms.base);
            logger::info("rfabDz {}: handed back to RFAB at its own stage 1",
                a_forms.id);
        }
    }

    void RfabDisease::ClearAll()
    {
        for (const auto& forms : Forms::rfabDisease) {
            if (forms.base) {
                HandBack(forms);
            }
        }
    }

    void RfabDisease::Update(float a_sleep, float a_hunger, float a_cold,
        float a_gameHours, bool a_undead)
    {
        for (int i = 0; i < COUNT; ++i) {
            UpdateOne(i, a_sleep, a_hunger, a_cold, a_gameHours, a_undead);
        }
    }

    void RfabDisease::UpdateOne(int a_index, float a_sleep, float a_hunger, float a_cold,
        float a_gameHours, bool a_undead)
    {
        const auto& forms = Forms::rfabDisease[a_index];
        auto*       player = Player();
        if (!forms.base || !player) {
            return;
        }

        auto&              diseases = Disease::GetSingleton();
        const std::int32_t stage = diseases.Stage(forms.id);

        // Our stage 2/3 copies carry RFAB's marker effect too, so the probe
        // below only means anything while neither of them is on. Without this
        // "afflicted" reads true at every stage, forever.
        const bool ourCopyOn =
            (forms.ours[0] && player->HasSpell(forms.ours[0])) ||
            (forms.ours[1] && player->HasSpell(forms.ours[1]));

        const bool afflicted =
            player->HasSpell(forms.base) ||
            (forms.mark && !ourCopyOn && player->AsMagicTarget()->HasMagicEffect(forms.mark));

        if (!Settings::bModEnabled || !Settings::bRfabDiseasesEnabled || a_undead) {
            if (stage > 0) {
                HandBack(forms);
            }
            return;
        }

        // Catching it means adopting the engine-applied disease into the spell
        // list. The marker can linger a tick or two after a cure, so it has to
        // go quiet once before a fresh bite counts - otherwise a cure loops
        // straight back into stage 1.
        if (stage == 0) {
            if (!afflicted) {
                _seenClean[a_index] = true;
            } else if (_seenClean[a_index]) {
                player->AddSpell(forms.base);
                _seenClean[a_index] = false;
                diseases.SetStage(forms.id, 1, nullptr, nullptr);
                diseases.ResetP(forms.id);
                logger::info("rfabDz {}: contracted, base adopted", forms.id);
            }
            return;
        }

        // At stage 2 or 3 a fresh bite lays RFAB's own effects on top of our
        // copy. Dispelling the base removes those and leaves ours alone, and
        // whether it removed anything is the only way to tell a stray instance
        // from our copy wearing RFAB's effect names.
        if (stage >= 2 && DispelSpell(player, forms.base)) {
            logger::info("rfabDz {}: dispelled a stray RFAB instance at stage {}",
                forms.id, stage);
        }

        auto* current = StageSpell(forms, stage);

        // The Peryite blessing is RFAB's own balance and this layer does not get
        // to touch it: for the six base-game diseases stage 1 is RFAB's record,
        // boon included, so it freezes there. No progression, no cure counting.
        // Stages 2 and 3 still run, so something already in progress can settle
        // back to 1 and lock in - which is the way out for a Peryite follower.
        if (a_index <= 5 && Forms::peryiteBlessing &&
            player->HasSpell(Forms::peryiteBlessing) && stage == 1) {
            if (!afflicted) {
                player->AddSpell(forms.base);   // a stray cure stripped it
            }
            diseases.ResetP(forms.id);
            diseases.TakeCures(forms.id);              // discard: frozen
            return;
        }

        // A cure strips whichever Type=Disease spell is currently on. What tells
        // us differs by stage: at 1 the marker goes quiet, at 2 or 3 our own
        // copy vanishes.
        const bool gone = (stage == 1) ? !afflicted
                                       : !(current && player->HasSpell(current));

        // Past stage 1 the spell is an ABILITY, so no cure can reach it and a
        // missing one is not evidence of anything a cure did - only of a
        // console removespell or another mod. Put it back, skip the cure
        // bookkeeping, and CARRY ON TO THE DRIFT BELOW.
        //
        // Carrying on is the whole point. Returning here instead - which is
        // what this did - froze stages 2 and 3 solid: P never moved, so the
        // illness could neither worsen nor heal, and no cure reaches an
        // ability. It also took away the one escape the comment above promises
        // a Peryite follower, since settling back to stage 1 needs the drift.
        //
        // Stage 1 belongs to RFAB, is still a disease and still answers to
        // medicine exactly as RFAB intended.
        if (stage >= 2) {
            if (gone && current) {
                player->AddSpell(current);
                logger::info("rfabDz {}: stage {} ability put back", forms.id, stage);
            }
        } else {
            auto cures = diseases.TakeCures(forms.id);
            if (cures == 0 && gone) {
                cures = 1;
            }
            if (cures > 0) {
                const auto target = std::max(0, stage - cures);
                Announce(forms, target, stage);
                diseases.SetStage(forms.id, target, current, StageSpell(forms, target));
                diseases.HalveP(forms.id);
                if (target == 0) {
                    (void)DispelSpell(player, forms.base);
                }
                logger::info("rfabDz {}: {} cure(s), stage {} -> {}",
                    forms.id, cures, stage, target);
                return;
            }
        }

        const AxisBand band = AxisState(a_sleep, a_hunger, a_cold, a_undead);
        const auto     net = diseases.Step(forms.id, band, a_gameHours, DiseaseResist(),
                Settings::fDiseaseProgressHours, Settings::fDiseaseDecayHours);
        if (net == 0) {
            return;
        }

        const auto target = std::clamp(stage - net, 0, 3);
        if (target == stage) {
            return;
        }

        Announce(forms, target, stage);
        diseases.SetStage(forms.id, target, current, StageSpell(forms, target));
        if (target == 0) {
            // Living well clears it out entirely, RFAB's own stage included.
            (void)DispelSpell(player, forms.base);
        }
        logger::info("rfabDz {}: stage {} -> {} (band {})",
            forms.id, stage, target, AxisBandName(band));
    }
}
