#include "PCH.h"

#include "Core/CommonCold.h"

#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Core/Notify.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view ID = "CC"sv;

        // An axis at or below this much of its bar keeps an illness going.
        // Read straight off the bar the widget draws, so what the player sees
        // and what the illness reacts to are the same number.
        constexpr float AXIS_WORSEN_BELOW = 0.5f;

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

        [[nodiscard]] float RandomPercent()
        {
            static std::mt19937                          gen{ std::random_device{}() };
            static std::uniform_real_distribution<float> dist{ 0.0f, 100.0f };
            return dist(gen);
        }

        // Load on one axis, 0..1: nothing until the grace line, then linear.
        // The axes are reserves here and deprivations in v0.4.0, so this reads
        // from the other end.
        [[nodiscard]] float Load(float a_reserve, float a_safe)
        {
            const float used = 1.0f - std::clamp(a_reserve, 0.0f, 1.0f);
            const float grace = 1.0f - std::clamp(a_safe, 0.0f, 1.0f);
            if (used <= grace || grace >= 1.0f) {
                return used <= grace ? 0.0f : 1.0f;
            }
            return std::min(1.0f, (used - grace) / (1.0f - grace));
        }
    }

    AxisBand AxisState(float a_sleep, float a_hunger, float a_cold, bool a_undead)
    {
        // Worsening wins: one axis in the ground is enough, however good the
        // others are.
        if (!a_undead && (a_sleep < AXIS_WORSEN_BELOW || a_hunger < AXIS_WORSEN_BELOW)) {
            return AxisBand::kWorsen;
        }
        if (a_cold < AXIS_WORSEN_BELOW) {
            return AxisBand::kWorsen;
        }

        // Healing needs everything to be genuinely fine, not merely not-awful.
        const bool sleepOk = a_undead || a_sleep > Settings::fSleepSafe;
        const bool hungerOk = a_undead || a_hunger > Settings::fHungerSafe;
        if (sleepOk && hungerOk && a_cold > Settings::fColdSafe) {
            return AxisBand::kHeal;
        }

        return AxisBand::kHold;
    }

    CommonCold& CommonCold::GetSingleton()
    {
        static CommonCold singleton;
        return singleton;
    }

    std::int32_t CommonCold::Stage() const
    {
        return Disease::GetSingleton().Stage(ID);
    }

    RE::SpellItem* CommonCold::StageSpell(std::int32_t a_stage)
    {
        switch (a_stage) {
        case 1:
            return Forms::abCold1;
        case 2:
            return Forms::abCold2;
        case 3:
            return Forms::abCold3;
        default:
            return nullptr;
        }
    }

    float CommonCold::ContractChance(float a_cold)
    {
        // The settings are already reserves - 0.50 and 0.10 are v0.4.0's 50 and
        // 90 points of deprivation - so they are used as they are. An earlier
        // version inverted them a second time, which put the worst-case line at
        // 0.90 instead of 0.10. That made the ramp condition impossible, so the
        // chance sat at its maximum from the moment the bar passed 0.50: a cold
        // was caught almost instantly, and since it could also clear in that
        // same band, it caught and cleared in a loop.
        const float catchAt = Settings::fColdCatchAt;
        const float worstAt = Settings::fColdCatchWorstAt;

        if (a_cold > catchAt) {
            return 0.0f;
        }

        // Lowest chance at the line, highest once the bar is down at worstAt.
        float chance = Settings::fColdCatchChanceMax;
        if (a_cold > worstAt && catchAt > worstAt) {
            const float t = (catchAt - a_cold) / (catchAt - worstAt);
            chance = Settings::fColdCatchChanceMin +
                     t * (Settings::fColdCatchChanceMax - Settings::fColdCatchChanceMin);
        }

        return std::max(0.0f, chance * (1.0f - DiseaseResist() * 0.01f));
    }

    void CommonCold::Update(float a_sleep, float a_hunger, float a_cold,
        float a_gameHours, bool a_undead)
    {
        if (!Forms::abCold1) {
            return;
        }

        auto&              diseases = Disease::GetSingleton();
        const std::int32_t stage = diseases.Stage(ID);

        if (!Settings::bDiseasesEnabled || a_undead || !Settings::bModEnabled) {
            if (stage > 0) {
                ClearAll();
            }
            return;
        }

        // Cures first: a counted Cure Disease effect walks it back a stage and
        // halves what had accumulated towards the next one.
        if (stage > 0) {
            // A cure another mod fired that the scan missed, or a console
            // removespell, leaves the stage spell gone with nothing counted.
            // v0.4.0 treats that as one cure rather than letting the illness
            // sit there with no effect to show for it.
            auto* player = Player();
            auto* current = StageSpell(stage);
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
                    logger::info("cold disease: stage {} ability put back", stage);
                }
            } else {
                // Counted or inferred - see the note in ElemLesion for why
                // the line has to say which.
                auto       cures = diseases.TakeCures(ID);
                const bool guessed = cures == 0 && spellMissing;
                if (guessed) {
                    cures = 1;
                }

                if (cures > 0) {
                    const auto target = std::max(0, stage - cures);
                    SetStage(target, stage);
                    diseases.HalveP(ID);
                    logger::info("cold disease: {} cure(s), stage {} -> {} ({})",
                        cures, stage, target,
                        guessed ? "the stage spell was gone, so a cure went unheard" : "a cure was counted");
                    return;
                }
            }
        }

        if (stage == 0) {
            if (diseases.RollDue(ID, 1.0f)) {
                const float chance = ContractChance(a_cold);
                if (chance > 0.0f && RandomPercent() < chance) {
                    SetStage(1, 0);
                    diseases.ResetP(ID);
                }
            }
            return;
        }

        const AxisBand band = AxisState(a_sleep, a_hunger, a_cold, a_undead);

        const auto net = diseases.Step(ID, band, a_gameHours, DiseaseResist(),
            Settings::fDiseaseProgressHours, Settings::fDiseaseDecayHours);

        // v0.4.0 logged the whole picture every tick because tuning an illness
        // you cannot see is guesswork - the same reason the climate sample is
        // traced. Throttled rather than gated so a quiet run still shows the
        // state that produced a stage change, and printed unconditionally on
        // the tick that actually moved a stage.
        if (Settings::bDebugLog) {
            static auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
            const auto  now = std::chrono::steady_clock::now();
            if (net != 0 || now - last >= std::chrono::seconds(5)) {
                last = now;
                logger::info(
                    "coldDz: stage={} P={:.1f} band={} | sleep {:.2f} ({:.2f}) "
                    "hunger {:.2f} ({:.2f}) cold {:.2f} ({:.2f}) | dt={:.3f}h "
                    "resist={:.0f} chance/h={:.1f} -> net {}",
                    stage, diseases.Get(ID).prog, AxisBandName(band),
                    a_sleep, Load(a_sleep, Settings::fSleepSafe),
                    a_hunger, Load(a_hunger, Settings::fHungerSafe),
                    a_cold, Load(a_cold, Settings::fColdSafe),
                    a_gameHours, DiseaseResist(), ContractChance(a_cold), net);
            }
        }

        if (net != 0) {
            // A positive net means P reached the healing end, so the stage
            // comes down - the sign is inverted on purpose.
            const auto target = std::clamp(stage - net, 0, 3);
            if (target != stage) {
                SetStage(target, stage);
            }
        }
    }

    void CommonCold::SetStage(std::int32_t a_stage, std::int32_t a_old)
    {
        // Announced before the stage is applied, for the same reason
        // hypothermia does it: nothing a stage does should be able to get in
        // front of its own message.
        auto& notify = Notify::GetSingleton();
        if (a_stage == 0) {
            notify.Push(Forms::msgCold0);
        } else if (a_stage > a_old) {
            notify.Push(a_stage == 1   ? Forms::msgCold1
                        : a_stage == 2 ? Forms::msgCold2
                                       : Forms::msgCold3);
        } else if (a_stage < a_old) {
            // Easing used to be silent, which is what looked like notifications
            // going missing: the stage moved in the log with nothing queued.
            notify.Push(a_stage == 2 ? Forms::msgColdEase2 : Forms::msgColdEase1);
        }

        Disease::GetSingleton().SetStage(ID, a_stage, StageSpell(a_old), StageSpell(a_stage));
    }

    void CommonCold::ClearAll()
    {
        Disease::GetSingleton().ClearStages(ID, Forms::abCold1, Forms::abCold2, Forms::abCold3);
    }
}
