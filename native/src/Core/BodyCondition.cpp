#include "PCH.h"

#include "Core/BodyCondition.h"

#include "Core/Notify.h"
#include "Core/Player.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr auto TRACE_GAP = std::chrono::seconds(5);
    }

    std::int32_t BodyCondition::Stage() const
    {
        return Diseases().Stage(Id());
    }

    float BodyCondition::P() const
    {
        return Diseases().Get(Id()).prog;
    }

    RE::SpellItem* BodyCondition::StageSpell(std::int32_t a_stage) const
    {
        if (a_stage < 1 || a_stage > 3) {
            return nullptr;
        }
        return _forms.stage[a_stage - 1];
    }

    void BodyCondition::Announce(std::int32_t a_stage, std::int32_t a_old) const
    {
        auto& notify = Notify::GetSingleton();
        if (a_stage == 0) {
            notify.Push(_forms.cured);
        } else if (a_stage > a_old) {
            notify.Push(_forms.msg[a_stage - 1]);
        } else if (a_stage < a_old) {
            notify.Push(_forms.ease[a_stage == 2 ? 0 : 1]);
        }
    }

    void BodyCondition::SetStage(std::int32_t a_stage)
    {
        const std::int32_t old = Stage();

        // Announced first, so the message is queued before anything a stage
        // does can stop it being displayed - and at stage three that is not
        // hypothetical: it drops the player on the floor.
        Announce(a_stage, old);

        Diseases().SetStage(Id(), a_stage, StageSpell(old), StageSpell(a_stage));
        OnStageChanged(old, a_stage);

        logger::info("{}: stage {} -> {}", Id(), old, a_stage);
    }

    void BodyCondition::Clear()
    {
        Diseases().ClearStages(Id(), _forms.stage[0], _forms.stage[1], _forms.stage[2]);
    }

    void BodyCondition::Run(const Tick& a_tick, float a_realSeconds)
    {
        if (!Ready()) {
            return;
        }

        const std::int32_t stage = Stage();

        if (!Enabled() || a_tick.undead) {
            if (stage > 0) {
                Clear();
            }
            // ...and Maintain still runs. Whatever was toggled on the engine's
            // side has to come back off even when the condition itself is not
            // running, or it stays on for ever.
            Maintain(a_tick, a_realSeconds);
            return;
        }

        if (stage == 0) {
            if (Onsets(a_tick)) {
                SetStage(1);
                Diseases().ResetP(Id());
                return;   // SetStage has already told the engine what changed
            }
            Trace(a_tick, stage, false);
            Maintain(a_tick, a_realSeconds);
            return;
        }

        RestartDeadEffects(stage);

        const std::int32_t step = Progress(a_tick, a_realSeconds);

        Trace(a_tick, stage, step != 0);

        if (step != 0) {
            const auto target = std::clamp(stage + step, 0, 3);
            if (target != stage) {
                SetStage(target);
            }
        }

        Maintain(a_tick, a_realSeconds);
    }

    void BodyCondition::RestartDeadEffects(std::int32_t a_stage)
    {
        // Same fault, same repair, same reasoning as Illness - see the note
        // there. Written twice rather than shared because the two bases have
        // no common ancestor, and inventing one to hold six lines would put
        // hypothermia back under the illnesses it was just taken out of.
        auto* spell = StageSpell(a_stage);
        auto* player = Player();
        if (!spell || !player || !player->HasSpell(spell)) {
            return;
        }

        constexpr int  ATTEMPTS = 3;
        constexpr auto GAP = std::chrono::seconds(2);

        const auto now = std::chrono::steady_clock::now();
        if (_checked && now - _checkedAt < GAP) {
            return;
        }
        _checkedAt = now;
        _checked = true;

        if (Disease::RunningEffects(spell) > 0) {
            _restarts = 0;
            return;
        }

        if (_restarts >= ATTEMPTS) {
            return;
        }
        ++_restarts;

        player->RemoveSpell(spell);
        player->AddSpell(spell);

        const auto nowRunning = Disease::RunningEffects(spell);
        if (nowRunning > 0) {
            logger::info("{}: stage {} had no effects running - put back, {} now",
                Id(), a_stage, nowRunning);
        } else {
            logger::warn("{}: stage {} has no effects running and re-applying "
                         "changed nothing (attempt {} of {})",
                Id(), a_stage, _restarts, ATTEMPTS);
        }
    }

    void BodyCondition::Trace(const Tick& a_tick, std::int32_t a_stage, bool a_force) const
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

        const auto extra = TraceExtra(a_tick);
        logger::info("st {}: stage={} P={:+.1f} {} | cold {:.3f} | dt={:.3f}h{}{}", Id(),
            a_stage, P(), _drive, a_tick.cold, a_tick.gameHours,
            extra.empty() ? "" : " | ", extra);
    }
}
