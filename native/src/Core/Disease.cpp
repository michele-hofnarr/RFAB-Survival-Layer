#include "PCH.h"

#include "Core/Disease.h"

#include "Core/Forms.h"
#include "Core/Illness.h"
#include "Core/Hypothermia.h"
#include "Core/Illnesses.h"
#include "Core/Player.h"
#include "Core/Random.h"
#include "Settings.h"

#include <spdlog/fmt/fmt.h>

namespace RSL
{
    namespace
    {
        [[nodiscard]] float GameDays()
        {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        // The stage spell an id and stage ought to put on the player.
        //
        // Written out by hand once - a chain of comparisons against every
        // family of records this build knows - and it was the wrong shape the
        // day it was written: a second list of all the illnesses, kept beside
        // the real one, to be edited whenever the set changed. It asks the
        // registry now, and hypothermia answers for itself because it is not
        // an illness and never was.
        [[nodiscard]] RE::SpellItem* StageSpellOf(std::string_view a_id,
            std::int32_t a_stage)
        {
            if (auto* illness = Illnesses::GetSingleton().Find(a_id)) {
                return illness->StageSpell(a_stage);
            }
            if (a_id == Hypothermia::GetSingleton().Id()) {
                return Hypothermia::GetSingleton().StageSpell(a_stage);
            }
            return nullptr;
        }

        // WHAT THE ENGINE HAS, as against what the spell list says.
        //
        // They are not the same question and this mod has already been bitten
        // by the difference once: e275a60 found hypothermia where AddSpell was
        // accepted, HasSpell stayed true across a save reload, and no
        // ActiveEffect was ever created - the spell sat in the list doing
        // nothing and showing nothing. Hypothermia.cpp still carries the
        // check that found it.
        //
        // An effect that IS instantiated can also be sitting there doing
        // nothing: inactive, dispelled, or with its condition evaluated
        // false. All four states look identical from the spell list and
        // identical to the player - an illness with nothing to show for it.
        // Only this tells them apart.
        struct Running
        {
            int  count{ 0 };
            bool inactive{ false };
            bool dispelled{ false };
            bool conditionFalse{ false };
        };

        [[nodiscard]] std::string ArchetypeName(RE::EffectArchetype a_arch)
        {
            switch (a_arch) {
            case RE::EffectArchetype::kValueModifier:       return "ValueMod";
            case RE::EffectArchetype::kDualValueModifier:   return "DualValueMod";
            case RE::EffectArchetype::kPeakValueModifier:   return "PeakValueMod";
            case RE::EffectArchetype::kAccumulateMagnitude: return "AccumMagnitude";
            default: return fmt::format("archetype {}", static_cast<int>(a_arch));
            }
        }

        // Everything else running on the player that moves this actor value.
        //
        // THIS IS THE COLUMN THAT MATTERS. A peak-value modifier does not stack
        // with another on the same actor value - only the largest applies - so
        // an effect that never arrives while something else already holds its
        // actor value has not failed to instantiate at all. It has been
        // outranked, and from every other angle the two look identical.
        [[nodiscard]] std::string RivalsFor(RE::ActorValue a_av, const RE::Effect* a_skip,
            RE::BSSimpleList<RE::ActiveEffect*>* a_list)
        {
            std::string rivals;
            if (!a_list || a_av == RE::ActorValue::kNone) {
                return rivals;
            }

            for (auto* active : *a_list) {
                if (!active || active->effect == a_skip) {
                    continue;
                }
                const auto* base = active->GetBaseObject();
                if (!base || base->data.primaryAV != a_av) {
                    continue;
                }
                const char* name = base->GetName();
                if (!rivals.empty()) {
                    rivals += ", ";
                }
                rivals += fmt::format("[{:08X}] {} {} mag {:.1f}", base->GetFormID(),
                    (name && *name) ? name : "<unnamed>",
                    ArchetypeName(base->data.archetype), active->magnitude);
            }
            return rivals;
        }

        // EVERYTHING ABOUT AN EFFECT THAT DID NOT ARRIVE, in one place.
        //
        // Written this way after two runs that each answered a third of the
        // question and cost a restart of the game apiece. The first named the
        // culprits "<no editor id>", because Skyrim SE keeps no editor ids for
        // magic effects at runtime; the second would have named them without
        // saying what they are or what might be standing in their way.
        //
        // So: which effect, what kind it is, which actor value it moves, and
        // what else already holds that actor value.
        //
        // The list is taken non-const because BSSimpleList has no const
        // iterator: iterator_base cannot be built from a const node, and the
        // error it gives says nothing about that.
        [[nodiscard]] std::vector<std::string> MissingEffects(RE::SpellItem* a_spell,
            RE::BSSimpleList<RE::ActiveEffect*>* a_list)
        {
            std::vector<std::string> lines;
            if (!a_spell || !a_list) {
                return lines;
            }

            std::size_t carried = 0;
            std::size_t absent = 0;

            for (const auto* effect : a_spell->effects) {
                if (!effect) {
                    continue;
                }
                ++carried;

                bool running = false;
                for (auto* active : *a_list) {
                    if (active && active->effect == effect) {
                        running = true;
                        break;
                    }
                }
                if (running) {
                    continue;
                }
                ++absent;

                const auto* base = effect->baseEffect;
                if (!base) {
                    lines.push_back("      an effect with no base object at all");
                    continue;
                }

                const char* name = base->GetName();
                const auto  rivals = RivalsFor(base->data.primaryAV, effect, a_list);

                lines.push_back(fmt::format(
                    "      [{:08X}] {} - {} on av {}, magnitude {:.1f}{}{}",
                    base->GetFormID(), (name && *name) ? name : "<unnamed>",
                    ArchetypeName(base->data.archetype),
                    static_cast<int>(base->data.primaryAV), effect->effectItem.magnitude,
                    rivals.empty() ? ", and nothing else holds that av" : ", held by: ",
                    rivals));
            }

            if (absent == 0) {
                return {};
            }
            lines.insert(lines.begin(),
                fmt::format("    {} of {} effect(s) NOT RUNNING:", absent, carried));
            return lines;
        }

        [[nodiscard]] Running EffectsOf(RE::SpellItem* a_spell)
        {
            Running out;

            auto* player = Player();
            // Through MagicTarget by name: PlayerCharacter inherits it twice
            // over and the call is ambiguous without saying which.
            auto* target = player ? player->AsMagicTarget() : nullptr;
            auto* list = target ? target->GetActiveEffectList() : nullptr;
            if (!a_spell || !list) {
                return out;
            }

            for (auto* active : *list) {
                if (!active || active->spell != a_spell) {
                    continue;
                }
                ++out.count;
                if (active->flags.any(RE::ActiveEffect::Flag::kInactive)) {
                    out.inactive = true;
                }
                if (active->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
                    out.dispelled = true;
                }
                if (active->conditionStatus.get() ==
                    RE::ActiveEffect::ConditionStatus::kFalse) {
                    out.conditionFalse = true;
                }
            }
            return out;
        }

        // An axis at or below this much of its bar keeps an illness going.
        // Read straight off the bar the widget draws, so what the player sees
        // and what the illness reacts to are the same number.
        constexpr float AXIS_WORSEN_BELOW = 0.5f;
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

            if (RollPercent() < std::abs(state.prog) * sub) {
                net += state.prog > 0.0f ? 1 : -1;
                state.prog = 0.0f;
            }
        }

        return std::clamp(net, -1, 1);
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
        // EVERY ILLNESS, and nothing that is not one. Hypothermia used to be
        // skipped here by comparing its id to the letters "HY" in the middle of
        // the loop - a state of the body, with no potion, altar or spell that
        // talks it out of being cold, excluded from medicine by a string
        // comparison. It is a different type now and simply is not in this
        // list, which is the same fact stated where it can be seen.
        for (const auto& illness : Illnesses::GetSingleton().All()) {
            if (!illness->Ready() || !illness->CuresApply()) {
                continue;
            }
            const auto id = illness->Id();
            auto&      state = Get(id);

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
        auto* player = Player();
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
            // Hypothermia is a state of the body, not an illness, and nobody
            // should remark on it as one. It is absent from this list by being
            // a different kind of thing rather than by name.
            for (const auto& illness : Illnesses::GetSingleton().All()) {
                if (illness->Stage() >= 2) {
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
                               : (player && player->HasSpell(spell)) ? "in the list"
                                                                    : "MISSING";

            const auto running = EffectsOf(spell);

            logger::info("  {} stage {} P {:+.1f} cures {} - stage spell {}, "
                         "{} effect(s) running{}{}{}",
                id, state.stage, state.prog, state.cures, worn, running.count,
                running.inactive ? ", INACTIVE" : "",
                running.dispelled ? ", DISPELLED" : "",
                running.conditionFalse ? ", CONDITION FALSE" : "");

            if (spell && player) {
                auto* target = player->AsMagicTarget();
                for (const auto& line : MissingEffects(spell,
                         target ? target->GetActiveEffectList() : nullptr)) {
                    logger::warn("{}", line);
                }
            }
        }

        if (said == 0) {
            logger::info("illnesses ({}): none - {} ids carried, all clear",
                a_why, _states.size());
        }
    }

    std::size_t Disease::RunningEffects(RE::SpellItem* a_spell)
    {
        // The same walk the report does, so the number the illnesses act on and
        // the number the log prints cannot disagree.
        return static_cast<std::size_t>(EffectsOf(a_spell).count);
    }

    void Disease::Clear()
    {
        _states.clear();
    }
}
