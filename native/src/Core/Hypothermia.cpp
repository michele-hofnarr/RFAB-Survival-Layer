#include "PCH.h"

#include "Core/Hypothermia.h"

#include "Core/Climate.h"
#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Core/Notify.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view ID = "HY"sv;

        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        // Controls are toggled one flag at a time: UEFlag is a plain enum
        // class here, so the bitwise-or that reads naturally does not compile.
        void ToggleControls(bool a_enable)
        {
            auto* controls = RE::ControlMap::GetSingleton();
            if (!controls) {
                return;
            }
            using UEFlag = RE::ControlMap::UEFlag;
            for (const auto flag : { UEFlag::kMovement, UEFlag::kFighting,
                     UEFlag::kSneaking, UEFlag::kMenu, UEFlag::kActivate }) {
                controls->ToggleControls(flag, a_enable);
            }
        }

        // Rest is blocked the way the engine blocks it: bit 1 of the player's
        // charGen flag, which is what Game.SetInChargen(_, true, _) sets and
        // what v0.4.0 reached for through Papyrus.
        //
        // IT USED TO GO THROUGH PAPYRUS, and that cost a reproducible crash at
        // hypothermia stage one. The call is dispatched, not executed: the flag
        // was measured still at 0x00 immediately after DispatchStaticCall
        // returned and at 0x02 later, so the arguments were being freed while
        // the VM still had them. Two log lines between the call and the delete
        // were enough to hide it, which is how it was found.
        //
        // The bit itself is measured, not guessed: SetInChargen(false, true,
        // false) - the one true being "disable waiting" - moved the flag from
        // 0x00 to 0x02. Setting it directly is synchronous, allocates nothing,
        // races nothing, and leaves the other two bits to whoever else uses
        // them: another mod's "disable saving" is not ours to clear.
        void SetRestBlocked(bool a_blocked)
        {
            if (!Settings::bHypoBlocksRest) {
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }

            // CommonLibSSE names only kHandsBound (1 << 2) of the three, so
            // this one is spelled out with the value the measurement gave.
            constexpr auto DISABLE_WAITING =
                static_cast<RE::PlayerCharacter::ByCharGenFlag>(1 << 1);

            auto& flag = player->GetPlayerRuntimeData().byCharGenFlag;
            if (a_blocked) {
                flag.set(DISABLE_WAITING);
            } else {
                flag.reset(DISABLE_WAITING);
            }
            logger::info("rest block: charGenFlag -> {:#04x}", flag.underlying());
        }

        // Is one of this spell's effects actually running on the player?
        //
        // AddSpell can report success, and HasSpell agree afterwards, while the
        // engine never instantiates the ability - the spell sits in the list
        // doing nothing. That is the state a fast travel arrives in, and it is
        // why the stage abilities showed no active effect and no visuals even
        // though every check said they had applied.
        [[nodiscard]] bool EffectIsActive(RE::SpellItem* a_spell)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            // Reached through MagicTarget: PlayerCharacter inherits it twice
            // over and the name is ambiguous without saying which.
            auto* target = player ? player->AsMagicTarget() : nullptr;
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!a_spell || !effects) {
                return false;
            }
            for (auto* active : *effects) {
                if (active && active->spell == a_spell) {
                    return true;
                }
            }
            return false;
        }

        // Queued rather than shown: several stages can change within a second,
        // and the engine drops a notification whenever one is already pending.
        void Announce(RE::BGSMessage* a_message)
        {
            Notify::GetSingleton().Push(a_message);
        }
    }

    Hypothermia& Hypothermia::GetSingleton()
    {
        static Hypothermia singleton;
        return singleton;
    }

    std::int32_t Hypothermia::Stage() const
    {
        return Disease::GetSingleton().Stage(ID);
    }

    RE::SpellItem* Hypothermia::StageSpell(std::int32_t a_stage)
    {
        switch (a_stage) {
        case 1:
            return Forms::abHypo1;
        case 2:
            return Forms::abHypo2;
        case 3:
            return Forms::abHypo3;
        default:
            return nullptr;
        }
    }

    void Hypothermia::Update(float a_cold, float a_gameHours, float a_realSeconds,
        bool a_undead, float a_warmthSlow)
    {
        if (!Forms::Ready()) {
            return;
        }

        auto&              diseases = Disease::GetSingleton();
        const std::int32_t stage = diseases.Stage(ID);

        if (!Settings::bHypothermiaEnabled || a_undead || !Settings::bModEnabled) {
            if (stage > 0) {
                ClearAll();
            }
            // ...and the rest block is lifted even at stage zero, because it
            // may be ours from a previous session. See the note below.
            SyncRestBlock();
            return;
        }

        // Both settings are reserves, like the axis and like every other
        // threshold - no flip here.
        const float onsetAt = Settings::fHypoThreshold;
        const float recoverAt = Settings::fHypoRecoverThreshold;

        // Onset is instant: crossing the line at stage 0 goes straight to
        // stage 1 rather than starting an accumulator.
        if (stage == 0) {
            if (a_cold <= onsetAt) {
                SetStage(1);
                diseases.ResetP(ID);
                return;
            }

            // NOTHING MAY LEAVE REST BLOCKED FOR EVER.
            //
            // Going down a stage lifts the block through SetStage, so in play
            // this costs one comparison and does nothing. It is here for the
            // way stage zero can be arrived at WITHOUT SetStage: a load resets
            // the illness in memory while the engine's own flag is part of the
            // save, and a co-save record that fails its version check does the
            // same. The mod would then believe there is nothing to lift and the
            // player would never wait again.
            //
            // It cannot fight another mod for the bit either: after the first
            // application SyncRestBlock only acts on a change of its own, and
            // the flag is dropped on load precisely so that the first tick of a
            // new game applies rather than assumes.
            SyncRestBlock();
            return;
        }

        // Stages 1..3: fill towards the next stage while still that cold, drain
        // towards recovery once warm again, and hold still in between.
        float drift = 0.0f;
        if (a_cold <= onsetAt) {
            // WARMTH SLOWS THE ILLNESS BY THE SAME DIVISOR IT SLOWS THE FALL.
            //
            // Same formula, same setting (fWarmthSlowsChill), same reading of
            // the surroundings - Climate::ChillSlow(). Freezing to the bottom
            // in a fur coat and freezing to the bottom naked are not the same
            // situation, and until now the illness could not tell them apart:
            // both went one stage an hour.
            //
            // Only the worsening is divided. Recovery keeps its own rate, for
            // the reason warming keeps its own rate in the climate model - a
            // coat that made you recover MORE slowly would be exactly backwards.
            drift = 100.0f / (std::max(0.01f, Settings::fHypoWorsenHours) *
                                 std::max(0.1f, a_warmthSlow));
        } else if (a_cold >= recoverAt) {
            drift = -(100.0f / std::max(0.01f, Settings::fHypoRecoverHours));
        }

        const auto step = diseases.StepLinear(ID, drift, a_gameHours);
        if (step != 0) {
            const auto target = std::clamp(stage + step, 0, 3);
            if (target != stage) {
                SetStage(target);
            }
        }

        // The same trace the common cold gets next door, and for the same
        // reason: an illness whose accumulator cannot be seen cannot be tuned.
        // Its absence is exactly what made "is hypothermia too slow?" a
        // question that had to be answered by reading the clock in the climate
        // block and doing arithmetic by hand.
        //
        // Throttled rather than gated, and printed unconditionally on the tick
        // that moved a stage, so a quiet run still shows the state behind it.
        if (Settings::bDebugLog) {
            static auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
            const auto  now = std::chrono::steady_clock::now();
            if (step != 0 || now - last >= std::chrono::seconds(5)) {
                last = now;
                logger::info("hypoDz: stage={} P={:.1f} band={} | cold {:.3f} "
                             "(onset {:.2f} recover {:.2f}) | {:.2f} bar/h -> "
                             "{:.2f}h per stage (warmth /{:.2f})",
                    diseases.Stage(ID), diseases.Get(ID).prog,
                    drift > 0.0f ? "worsen" : (drift < 0.0f ? "recover" : "hold"),
                    a_cold, onsetAt, recoverAt, drift,
                    drift > 0.0f ? 100.0f / drift : 0.0f, a_warmthSlow);
            }
        }

        // There was a repair loop here that re-applied the stage ability once a
        // second whenever it had no active effect. It never once succeeded,
        // because the ability could not instantiate at all: the SPEL had no
        // ETYP. Fixing the record removed the thing it was covering for.

        // Stage 3 bleeds current health, quadratically in time spent there.
        if (diseases.Stage(ID) >= 3) {
            auto& state = diseases.Get(ID);
            state.stage3Seconds += a_realSeconds;

            const float k = 1.0f + state.stage3Seconds / std::max(1.0f, Settings::fHypoDrainRamp);
            if (auto* owner = Player() ? Player()->AsActorValueOwner() : nullptr) {
                owner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage,
                    RE::ActorValue::kHealth,
                    -(Settings::fHypoDrainPerSec * a_realSeconds * k * k));
            }
        }

        SyncRestBlock();
    }

    void Hypothermia::SetStage(std::int32_t a_stage)
    {
        auto&              diseases = Disease::GetSingleton();
        const std::int32_t old = diseases.Stage(ID);

        // Announced first, so the message is queued before anything that
        // might stop it being displayed.
        if (a_stage == 0) {
            Announce(Forms::msgHypoCured);
        } else if (a_stage > old) {
            Announce(a_stage == 1   ? Forms::msgHypo1
                     : a_stage == 2 ? Forms::msgHypo2
                                    : Forms::msgHypo3);
        } else if (a_stage < old) {
            Announce(a_stage == 2 ? Forms::msgHypoEase2 : Forms::msgHypoEase1);
        }

        diseases.SetStage(ID, a_stage, StageSpell(old), StageSpell(a_stage));

        if (a_stage >= 3 && old < 3) {
            diseases.Get(ID).stage3Seconds = 0.0f;

            // Locked immediately, as v0.4.0 does. This used to be held back
            // 1500 ms because the stage-three message never appeared when the
            // ragdoll dropped in the same instant - but that was the ETYP
            // fault talking: the ability was not instantiating at all. With
            // the record fixed there is nothing to work around.
            SetLock(true, false);
        } else if (a_stage < 3 && old >= 3) {
            diseases.Get(ID).stage3Seconds = 0.0f;
            SetLock(false, true);
        }

        SyncRestBlock();

        logger::info("hypothermia: stage {} -> {}", old, a_stage);
    }

    void Hypothermia::SetLock(bool a_on, bool a_nudge)
    {
        auto* player = Player();
        if (!player) {
            return;
        }
        _locked = a_on;

        if (auto* owner = player->AsActorValueOwner()) {
            owner->SetActorValue(RE::ActorValue::kParalysis, a_on ? 1.0f : 0.0f);
        }
        ToggleControls(!a_on);

        // The shove is not cosmetic, which is what an earlier pass got wrong:
        // setting Paralysis alone leaves the player standing. It takes an
        // impulse to put the ragdoll on the floor, and PushActorAway is how
        // v0.4.0 delivers it. There is no binding for that, but the same work
        // is done by the process-level knock the explosion path uses.
        if (a_on) {
            if (auto* process = player->GetActorRuntimeData().currentProcess) {
                process->KnockExplosion(player, player->GetPosition(), 3.0f);
            }
        }

        if (!a_on && a_nudge) {
            player->NotifyAnimationGraph("GetUpBegin"sv);
        }
    }

    void Hypothermia::SyncRestBlock()
    {
        auto* player = Player();
        if (!player) {
            return;
        }

        // Blocked from stage 1, except indoors while actually warming up: a
        // warm shelter is where this is meant to be slept off. Re-checked every
        // tick so it follows the player in and out of cover.
        bool wanted = false;
        if (Disease::GetSingleton().Stage(ID) >= 1) {
            const Climate here = Climate::Sample();
            const bool warmingIndoors = here.interior && here.ColdTarget() > 0.0f;
            wanted = !warmingIndoors;
        }

        if (_restKnown && wanted == _restBlocked) {
            return;
        }
        _restBlocked = wanted;
        _restKnown = true;

        SetRestBlocked(wanted);
        logger::info("hypothermia: rest blocked = {}", wanted);
    }

    void Hypothermia::Forget()
    {
        // Not a reset of the illness - that is the disease state, and the
        // co-save carries it. This is only what we believe the ENGINE is doing
        // on our behalf: rest blocked, controls locked. A loaded game brings
        // its own answers to both, so ours are dropped and re-applied by the
        // next tick rather than carried across.
        _restKnown = false;
        _locked = false;
    }

    void Hypothermia::ClearAll()
    {
        auto& diseases = Disease::GetSingleton();
        const bool wasStage3 = diseases.Stage(ID) >= 3;

        diseases.ClearStages(ID, Forms::abHypo1, Forms::abHypo2, Forms::abHypo3);

        if (_locked) {
            SetLock(false, wasStage3);
        }
        SyncRestBlock();
    }
}
