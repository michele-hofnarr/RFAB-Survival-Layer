#include "PCH.h"

#include "Core/Illness.h"

#include "Core/Notify.h"
#include "Core/Player.h"
#include "Core/Random.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // How often an illness repeats its trace while nothing is happening.
        // Per illness rather than global, so one chatty illness cannot starve
        // the rest and a quiet one still reports.
        constexpr auto TRACE_GAP = std::chrono::seconds(5);
    }

    std::int32_t Illness::Stage() const
    {
        return Diseases().Stage(Id());
    }

    float Illness::P() const
    {
        return Diseases().Get(Id()).prog;
    }

    bool Illness::Enabled() const
    {
        return Settings::bModEnabled && Settings::bDiseasesEnabled;
    }

    RE::SpellItem* Illness::StageSpell(std::int32_t a_stage) const
    {
        if (a_stage < 1 || a_stage > 3) {
            return nullptr;
        }
        return _forms.stage[a_stage - 1];
    }

    bool Illness::StageSpellGone(std::int32_t a_stage) const
    {
        auto* spell = StageSpell(a_stage);
        auto* player = Player();
        return !spell || !player || !player->HasSpell(spell);
    }

    void Illness::Announce(std::int32_t a_stage, std::int32_t a_old)
    {
        auto& notify = Notify::GetSingleton();
        if (a_stage == 0) {
            notify.Push(_forms.cured);
        } else if (a_stage > a_old) {
            notify.Push(_forms.msg[a_stage - 1]);
        } else if (a_stage < a_old) {
            // Stepping DOWN said nothing at all until it was noticed: an
            // illness announced every worsening and went quiet the moment it
            // began to lift, which is the one direction a player most wants
            // confirmed.
            notify.Push(_forms.ease[a_stage == 2 ? 0 : 1]);
        }
    }

    void Illness::MoveTo(std::int32_t a_stage, std::int32_t a_old)
    {
        // A new stage is a different spell, so the repair starts over. Without
        // this, three failed attempts at stage 1 left stage 2 unrepairable for
        // the rest of the session.
        _restarts = 0;
        _checked = false;

        // Announced before the stage is applied: nothing a stage does should be
        // able to get in front of its own message.
        Announce(a_stage, a_old);
        Diseases().SetStage(Id(), a_stage, StageSpell(a_old), StageSpell(a_stage));
        OnStageChanged(a_old, a_stage);
    }

    void Illness::Clear()
    {
        Diseases().ClearStages(Id(), _forms.stage[0], _forms.stage[1], _forms.stage[2]);
    }

    void Illness::Contract(const char* a_why)
    {
        if (!Ready() || Stage() > 0) {
            return;   // already ill; a second dose is not a worse dose
        }
        MoveTo(1, 0);
        Diseases().ResetP(Id());
        logger::info("dz {}: contracted {}", Id(), a_why);
    }

    void Illness::Roll(float a_chancePercent, const char* a_why)
    {
        if (!Ready() || !Enabled() || Stage() > 0 || a_chancePercent <= 0.0f) {
            return;
        }

        auto* player = Player();
        if (!player || (Forms::kwUndead && player->HasKeyword(Forms::kwUndead))) {
            return;
        }
        if (RollPercent() >= a_chancePercent) {
            return;
        }

        Contract(a_why);
        logger::info("dz {}: the roll was {:.0f}%", Id(), a_chancePercent);
    }

    std::int32_t Illness::Progress(const Tick& a_tick)
    {
        _band = AxisState(a_tick.sleep, a_tick.hunger, a_tick.cold, a_tick.undead);

        const auto net = Diseases().Step(Id(), _band, a_tick.gameHours, DiseaseResist(),
            Settings::fDiseaseProgressHours, Settings::fDiseaseDecayHours);

        // Disease::Step counts the HEALING end as positive; a stage change
        // counts getting worse as positive. One inversion, here, once.
        return -net;
    }

    void Illness::Update(const Tick& a_tick)
    {
        if (!Ready()) {
            return;
        }

        auto&              diseases = Diseases();
        const std::int32_t stage = diseases.Stage(Id());

        if (!Enabled() || a_tick.undead) {
            if (stage > 0) {
                Clear();
            }
            Idle();
            return;
        }

        Observe(a_tick, stage);

        if (stage == 0) {
            if (Contracts(a_tick)) {
                MoveTo(1, 0);
                diseases.ResetP(Id());
            }
            Trace(a_tick, Stage(), false);
            return;
        }

        // OUR RECORD AND THE PLAYER'S SPELL LIST ARE TWO ACCOUNTS OF ONE FACT,
        // and what their disagreeing means depends on the stage.
        //
        // Stage 1 is a Type=Disease spell. The engine's Cure Disease strips
        // every one of those unconditionally and tells nobody, so the spell
        // being gone IS the engine's record that a cure landed - the only
        // record there is, and the reason this path exists at all.
        //
        // Stages 2 and 3 are abilities precisely so that no cure can reach
        // them. A missing one there says nothing about medicine - only about a
        // console removespell or another mod - so it goes back on and the pass
        // CARRIES ON TO THE PROGRESSION BELOW. Returning here instead froze
        // those stages solid once: P never moved, so the illness could neither
        // worsen nor heal, and no cure reaches an ability.
        if (stage >= 2) {
            if (StageSpellGone(stage)) {
                auto* spell = StageSpell(stage);
                if (auto* player = Player(); spell && player) {
                    player->AddSpell(spell);
                    logger::info("dz {}: stage {} ability put back", Id(), stage);
                }
            }
        } else {
            auto       cures = diseases.TakeCures(Id());
            const bool stripped = cures == 0 && StageSpellGone(stage);
            if (stripped) {
                cures = 1;
            }

            if (cures > 0) {
                const auto target = std::max(0, stage - cures);
                MoveTo(target, stage);
                diseases.HalveP(Id());
                logger::info("dz {}: {} cure(s), stage {} -> {} ({})", Id(), cures, stage,
                    target,
                    stripped ? "the disease spell was stripped, which is what a cure does"
                             : "a cure was counted by the apply sink");
                return;
            }
        }

        RestartDeadEffects(stage);

        const std::int32_t step = Progress(a_tick);

        // Before the move, so the line shows the state that produced it.
        Trace(a_tick, stage, step != 0);

        if (step == 0) {
            return;
        }

        const auto target = std::clamp(stage + step, 0, 3);
        if (target != stage) {
            MoveTo(target, stage);
        }
    }

    void Illness::RestartDeadEffects(std::int32_t a_stage)
    {
        // A STAGE SPELL IN THE LIST WITH NOTHING RUNNING BEHIND IT.
        //
        // An ability's effects are instantiated when the spell is ADDED, and
        // nothing revisits that moment afterwards. So a save written against
        // one set of records and loaded against another keeps the spell and
        // loses the effects: HasSpell says yes, the illness advances, the
        // player has no penalty, no icon and no way to be rid of it. Measured -
        // a tissue stress at stage 1 with 0 of 3 effects running, one of them
        // with nothing whatever competing for its actor value.
        //
        // Nothing else in the mod recovers from this. The repair above only
        // fires when the SPELL is gone, and here it is not.
        //
        // ANY MISSING EFFECT, not only a spell that runs nothing at all.
        //
        // The first version of this asked whether the count was zero, and that
        // was wrong within one run of the game: a green spore came back from
        // the save with its hidden effect running and its VISIBLE one missing -
        // one of two - so it counted as healthy and the player saw no illness.
        // Which effect goes missing is not something this can rank, and the one
        // that carries the name is as likely as any.
        if (!MayRestart(a_stage)) {
            return;
        }

        auto* spell = StageSpell(a_stage);
        auto* player = Player();
        if (!spell || !player || !player->HasSpell(spell)) {
            return;   // the spell itself is missing - somebody else's repair
        }

        // Three attempts, then it is the engine's answer and not a glitch, and
        // saying so once beats saying nothing for ever.
        constexpr int  ATTEMPTS = 3;
        constexpr auto GAP = std::chrono::seconds(2);

        const auto now = std::chrono::steady_clock::now();
        if (_checked && now - _checkedAt < GAP) {
            return;
        }
        _checkedAt = now;
        _checked = true;

        const auto made = Disease::EffectsRunning(spell);

        // A record with no effect entries has nothing to be missing, and
        // WhollyRunning says false for it - so without this the repair would
        // spend its three attempts on a spell there was never anything to fix.
        if (made.carried == 0) {
            _restarts = 0;
            return;
        }

        if (made.WhollyRunning()) {
            _restarts = 0;
            return;
        }

        // The cap is only lifted by a spell that comes back WHOLE, above. An
        // effect the engine will never instantiate would otherwise be retried
        // every couple of seconds for the rest of the session, cycling the ones
        // that do work each time.
        if (_restarts >= ATTEMPTS) {
            return;
        }
        ++_restarts;

        player->RemoveSpell(spell);
        player->AddSpell(spell);

        const auto after = Disease::EffectsRunning(spell);
        if (after.WhollyRunning()) {
            logger::info("dz {}: stage {} was running {} of {} effects - put back, "
                         "all {} now",
                Id(), a_stage, made.running, made.carried, after.carried);
        } else {
            logger::warn("dz {}: stage {} runs {} of {} effects and re-applying made "
                         "it {} (attempt {} of {})",
                Id(), a_stage, made.running, made.carried, after.running, _restarts,
                ATTEMPTS);
        }
    }

    void Illness::Trace(const Tick& a_tick, std::int32_t a_stage, bool a_force) const
    {
        if (!Settings::bDebugLog) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!a_force && _traced && now - _tracedAt < TRACE_GAP) {
            return;
        }
        _tracedAt = now;
        _traced = true;

        // The common cold and hypothermia each had one of these and the other
        // three illnesses had none, so tuning them was reading the clock in the
        // climate block and doing arithmetic by hand. An accumulator that
        // cannot be seen cannot be tuned.
        const auto extra = TraceExtra(a_tick);
        logger::info("dz {}: stage={} P={:+.1f} band={} | sleep {:.2f} hunger {:.2f} "
                     "cold {:.2f} | dt={:.3f}h resist={:.0f}{}{}",
            Id(), a_stage, P(), AxisBandName(_band), a_tick.sleep, a_tick.hunger,
            a_tick.cold, a_tick.gameHours, DiseaseResist(), extra.empty() ? "" : " | ",
            extra);
    }
}
