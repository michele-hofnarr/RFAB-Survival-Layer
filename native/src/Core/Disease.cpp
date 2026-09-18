#include "PCH.h"

#include "Core/Disease.h"

#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        [[nodiscard]] float GameDays()
        {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        // The stage spell an id and stage ought to put on the player, or null
        // where this build cannot name one. Only the report needs this: every
        // other path already holds the forms it is working with, and this is
        // the one place that starts from a co-save key instead.
        [[nodiscard]] RE::SpellItem* StageSpellOf(std::string_view a_id,
            std::int32_t a_stage)
        {
            if (a_stage < 1 || a_stage > 3) {
                return nullptr;
            }
            const auto index = static_cast<std::size_t>(a_stage - 1);

            if (a_id == "HY"sv) {
                RE::SpellItem* const hypo[3] = { Forms::abHypo1, Forms::abHypo2,
                    Forms::abHypo3 };
                return hypo[index];
            }
            if (a_id == "CC"sv) {
                RE::SpellItem* const cold[3] = { Forms::abCold1, Forms::abCold2,
                    Forms::abCold3 };
                return cold[index];
            }
            for (const auto& dz : Forms::hitDisease) {
                if (dz.id == a_id) {
                    return dz.stage[index];
                }
            }
            if (Forms::elemLesion.id == a_id) {
                return Forms::elemLesion.stage[index];
            }
            for (const auto& dz : Forms::rfabDisease) {
                if (dz.id == a_id) {
                    // Stage 1 is RFAB's own record and stays theirs; 2 and 3
                    // are ours.
                    return a_stage == 1 ? dz.base : dz.ours[index - 1];
                }
            }
            return nullptr;
        }

        [[nodiscard]] float RandomPercent()
        {
            static std::mt19937                          gen{ std::random_device{}() };
            static std::uniform_real_distribution<float> dist{ 0.0f, 100.0f };
            return dist(gen);
        }
    }

    Disease& Disease::GetSingleton()
    {
        static Disease singleton;
        return singleton;
    }

    Disease::State& Disease::Get(std::string_view a_id)
    {
        if (auto it = _states.find(a_id); it != _states.end()) {
            return it->second;
        }
        return _states.emplace(std::string(a_id), State{}).first->second;
    }

    std::int32_t Disease::Stage(std::string_view a_id)
    {
        return Get(a_id).stage;
    }

    bool Disease::RollDue(std::string_view a_id, float a_perHours)
    {
        auto&       state = Get(a_id);
        const float now = GameDays();
        if (now - state.lastRollDays >= a_perHours / 24.0f) {
            state.lastRollDays = now;
            return true;
        }
        return false;
    }

    void Disease::SetStage(std::string_view a_id, std::int32_t a_stage,
        RE::SpellItem* a_previous, RE::SpellItem* a_next)
    {
        auto* player = Player();
        if (!player) {
            return;
        }

        if (a_previous) {
            player->RemoveSpell(a_previous);
        }
        if (a_next) {
            player->AddSpell(a_next);
            if (!player->HasSpell(a_next)) {
                logger::error("stage spell for {} did not stick", a_id);
            }
        }

        auto& state = Get(a_id);
        state.stage = a_stage;
        if (a_stage <= 0) {
            state.prog = 0.0f;
        }
        logger::info("disease {}: stage -> {}", a_id, a_stage);
    }

    void Disease::ClearStages(std::string_view a_id,
        RE::SpellItem* a_s1, RE::SpellItem* a_s2, RE::SpellItem* a_s3)
    {
        if (auto* player = Player()) {
            for (auto* spell : { a_s1, a_s2, a_s3 }) {
                if (spell) {
                    player->RemoveSpell(spell);
                }
            }
        }

        auto& state = Get(a_id);
        state = State{};
        state.lastRollDays = GameDays();
    }

    std::int32_t Disease::TakeCures(std::string_view a_id)
    {
        auto&              state = Get(a_id);
        const std::int32_t n = state.cures;
        state.cures = 0;
        return n;
    }

    void Disease::AddCure(std::string_view a_id)
    {
        ++Get(a_id).cures;
    }

    void Disease::ResetP(std::string_view a_id)
    {
        Get(a_id).prog = 0.0f;
    }

    void Disease::HalveP(std::string_view a_id)
    {
        Get(a_id).prog *= 0.5f;
    }

    void Disease::AddP(std::string_view a_id, float a_delta)
    {
        auto& state = Get(a_id);
        state.prog = std::clamp(state.prog + a_delta, -100.0f, 100.0f);
    }

    std::int32_t Disease::StepManual(std::string_view a_id, float a_drift, float a_hours)
    {
        auto& state = Get(a_id);

        // Sub-stepped, because the roll is against |P| and P is moving: one big
        // roll over a long span is not the same distribution as many small ones.
        int iterations = 1;
        if (a_hours > 0.25f) {
            iterations = std::clamp(static_cast<int>(a_hours / 0.25f), 1, 120);
        }
        const float sub = a_hours / static_cast<float>(iterations);

        int net = 0;
        for (int i = 0; i < iterations; ++i) {
            state.prog = std::clamp(state.prog + a_drift * sub, -100.0f, 100.0f);

            if (RandomPercent() < std::abs(state.prog) * sub) {
                net += state.prog > 0.0f ? 1 : -1;
                state.prog = 0.0f;
            }
        }

        return std::clamp(net, -1, 1);
    }

    std::string_view AxisBandName(AxisBand a_band)
    {
        switch (a_band) {
        case AxisBand::kWorsen:
            return "worsen"sv;
        case AxisBand::kHeal:
            return "heal"sv;
        default:
            return "hold"sv;
        }
    }

    std::int32_t Disease::Step(std::string_view a_id, AxisBand a_band, float a_hours,
        float a_diseaseResist, float a_worsenHours, float a_recoverHours)
    {
        // Held: the drift stops, the roll does not. P stays where it is and the
        // illness still resolves at roughly |P| percent per game hour, in
        // whichever direction it had already been pushed. Holding means it
        // stops accumulating, not that it stands still - and because a held
        // illness keeps rolling, P cannot be banked and cashed in later, which
        // is the property v0.4.0's comment on the cold insists on ("needs
        // SUSTAINED conditions - can't bank time and cash it out").
        float drift = 0.0f;

        if (a_band == AxisBand::kHeal) {
            drift = (100.0f / std::max(0.01f, a_recoverHours)) *
                    (1.0f + a_diseaseResist * 0.01f);
            // ...and resistance BELOW -100% would otherwise flip healing into
            // worsening: a rested, fed, warm character getting sicker for it.
            // The mirror of the clamp below, which was there from the start
            // while this one was simply forgotten. Reachable in normal play -
            // a log with resist=-100 is what turned it up.
            drift = std::max(drift, 0.0f);
        } else if (a_band == AxisBand::kWorsen) {
            drift = -(100.0f / std::max(0.01f, a_worsenHours)) *
                    (1.0f - a_diseaseResist * 0.01f);
            // Resistance over 100% would otherwise flip worsening into healing.
            drift = std::min(drift, 0.0f);
        }

        return StepManual(a_id, drift, a_hours);
    }

    std::int32_t Disease::StepLinear(std::string_view a_id, float a_drift, float a_hours)
    {
        auto& state = Get(a_id);
        state.prog += a_drift * a_hours;

        if (state.prog >= 100.0f) {
            state.prog = 0.0f;
            return 1;
        }
        if (state.prog <= -100.0f) {
            state.prog = 0.0f;
            return -1;
        }
        return 0;
    }

    void Disease::ScanCureEffects()
    {
        _cureEffects.clear();

        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            logger::error("no data handler - cures cannot be recognised");
            return;
        }

        for (auto* effect : handler->GetFormArray<RE::EffectSetting>()) {
            if (effect && effect->data.archetype == RE::EffectArchetype::kCureDisease) {
                _cureEffects.insert(effect->GetFormID());
            }
        }
        logger::info("{} cure-disease effects recognised", _cureEffects.size());
    }

    bool Disease::IsCureEffect(RE::FormID a_effect)
    {
        return _cureEffects.contains(a_effect);
    }

    void Disease::ApplyCure()
    {
        for (auto& [id, state] : _states) {
            // Hypothermia runs on this engine but is not an illness: it is a
            // state of the body, and no potion, altar or spell talks it out of
            // being cold. Only warmth does.
            if (id == "HY") {
                continue;
            }
            // MEDICINE SETTLES AN EARLY ILLNESS AND ONLY EASES A LATE ONE.
            //
            // At stage 1 a potion, a spell or an altar clears it outright -
            // that is the reward for noticing in time. Past that it no longer
            // cures, but it is not wasted either: it pushes P towards the
            // healing end by fCurePotency, so the draught buys a better chance
            // of the illness turning rather than deepening. The body still has
            // to do the rest, through sleep, food and warmth.
            //
            // fCurePotency defaults to the same 10 a clean linen cloth is worth
            // to a lesion, so "one dose of medicine" means one thing across the
            // mod.
            //
            // Both halves live HERE and not at the point of use: there is one
            // door into the cure count, so there is one place that decides what
            // walking through it means, and every illness gets the same rule -
            // ours, RFAB's or the common cold.
            if (state.stage == 1) {
                ++state.cures;
                logger::info("cure counted for {}", id);
            } else if (state.stage > 1) {
                state.prog = std::clamp(state.prog + Settings::fCurePotency,
                    -100.0f, 100.0f);
                logger::info("cure eased {} - stage {} is past curing, P {:+.1f}",
                    id, state.stage, state.prog);
            }
        }
    }

    void Disease::SyncMarker()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !Forms::dzMarker) {
            return;
        }

        // The switches are read HERE rather than at the call site, so that
        // calling this is always the right thing to do - including from the
        // master-switch teardown, which is the one path that has to take the
        // marker off without any illness having changed.
        bool advanced = Settings::bModEnabled && Settings::bDiseasesEnabled;
        if (advanced) {
            advanced = false;
            for (const auto& [id, state] : _states) {
                // Hypothermia is a state of the body, not an illness, and
                // nobody should remark on it as one.
                if (id != "HY" && state.stage >= 2) {
                    advanced = true;
                    break;
                }
            }
        }

        const bool worn = player->HasSpell(Forms::dzMarker);
        if (advanced && !worn) {
            player->AddSpell(Forms::dzMarker);
            logger::info("disease marker on - something is at stage 2 or worse");
        } else if (!advanced && worn) {
            player->RemoveSpell(Forms::dzMarker);
            logger::info("disease marker off");
        }
    }

    void Disease::Report(std::string_view a_why)
    {
        auto* player = Player();

        std::size_t said = 0;
        for (const auto& [id, state] : _states) {
            // An id with nothing on it is carried because something asked
            // after it once, not because it is an illness anybody has.
            if (state.stage == 0 && state.prog == 0.0f && state.cures == 0) {
                continue;
            }
            if (said == 0) {
                logger::info("illnesses ({}):", a_why);
            }
            ++said;

            auto*       spell = StageSpellOf(id, state.stage);
            const char* worn = state.stage == 0 ? "-"
                               : !spell          ? "NO RECORD"
                               : (player && player->HasSpell(spell)) ? "on the player"
                                                                    : "MISSING";

            logger::info("  {} stage {} P {:+.1f} cures {} - stage spell {}",
                id, state.stage, state.prog, state.cures, worn);
        }

        if (said == 0) {
            logger::info("illnesses ({}): none - {} ids carried, all clear",
                a_why, _states.size());
        }
    }

    void Disease::Clear()
    {
        _states.clear();
    }
}
