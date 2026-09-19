#include "PCH.h"

#include "Core/Hypothermia.h"

#include "Core/Climate.h"
#include "Core/Player.h"
#include "Settings.h"

#include <spdlog/fmt/fmt.h>

namespace RSL
{
    namespace
    {
        // Controls are toggled one flag at a time: UEFlag is a plain enum
        // class here, so the bitwise-or that reads naturally does not compile.
        //
        // WHAT IS DELIBERATELY LEFT ALONE, and why each one:
        //
        //   kMenu     Escape, and the quick save and load keys with it - the
        //             vanilla control map files all three under this one flag.
        //             Taking it away made stage three feel like a hang rather
        //             than a death, and it also shut the door on the one way
        //             out of a stuck lockdown: the mod's own switch lives in
        //             a menu.
        //   kLooking  the camera. Never in this list; if it will not turn,
        //             that is the ragdoll and not the control map.
        //
        // What is taken is what a body that has stopped obeying cannot do:
        // walk, fight, sneak, reach for things.
        void ToggleControls(bool a_enable)
        {
            auto* controls = RE::ControlMap::GetSingleton();
            if (!controls) {
                return;
            }
            using UEFlag = RE::ControlMap::UEFlag;
            for (const auto flag : { UEFlag::kMovement, UEFlag::kFighting,
                     UEFlag::kSneaking, UEFlag::kActivate }) {
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
        //
        // NO SETTING IS READ HERE, deliberately. It used to refuse to act at
        // all while bHypoBlocksRest was off, which meant turning that switch
        // off while hypothermic left waiting disabled for the rest of the
        // save: the flag was already set, the caller had nothing new to say,
        // and the one call that could have cleared it returned at the door.
        // The setting decides what is WANTED - see SyncRestBlock - and this
        // only ever carries the answer out.
        void SetRestBlocked(bool a_blocked)
        {
            auto* player = Player();
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
    }

    Hypothermia& Hypothermia::GetSingleton()
    {
        static Hypothermia singleton;
        return singleton;
    }

    bool Hypothermia::Enabled() const
    {
        return Settings::bModEnabled && Settings::bHypothermiaEnabled;
    }

    void Hypothermia::Update(const Tick& a_tick, float a_realSeconds, float a_warmthSlow)
    {
        _warmthSlow = a_warmthSlow;
        Run(a_tick, a_realSeconds);
    }

    bool Hypothermia::Onsets(const Tick& a_tick)
    {
        // Both settings are reserves, like the axis and like every other
        // threshold - no flip here. Onset is instant: crossing the line at
        // stage 0 goes straight to stage 1 rather than starting an accumulator.
        return a_tick.cold <= Settings::fHypoThreshold;
    }

    std::int32_t Hypothermia::Progress(const Tick& a_tick, float)
    {
        const float onsetAt = Settings::fHypoThreshold;
        const float recoverAt = Settings::fHypoRecoverThreshold;

        // Fill towards the next stage while still that cold, drain towards
        // recovery once warm again, and hold still in between.
        float drift = 0.0f;
        if (a_tick.cold <= onsetAt) {
            // WARMTH SLOWS THE ILLNESS BY THE SAME DIVISOR IT SLOWS THE FALL.
            //
            // Same formula, same setting (fWarmthSlowsChill), same reading of
            // the surroundings - Climate::ChillSlow(). Freezing to the bottom
            // in a fur coat and freezing to the bottom naked are not the same
            // situation, and until this was added the condition could not tell
            // them apart: both went one stage an hour.
            //
            // Only the worsening is divided. Recovery keeps its own rate, for
            // the reason warming keeps its own rate in the climate model - a
            // coat that made you recover MORE slowly would be exactly
            // backwards.
            _drive = "worsen"sv;
            drift = 100.0f / (std::max(0.01f, Settings::fHypoWorsenHours) *
                                 std::max(0.1f, _warmthSlow));
        } else if (a_tick.cold >= recoverAt) {
            _drive = "recover"sv;
            drift = -(100.0f / std::max(0.01f, Settings::fHypoRecoverHours));
        } else {
            _drive = "hold"sv;
        }

        return Diseases().StepLinear(Id(), drift, a_tick.gameHours);
    }

    void Hypothermia::Maintain(const Tick&, float a_realSeconds)
    {
        // Stage 3 bleeds current health, quadratically in time spent there.
        if (Stage() >= 3) {
            auto& state = Diseases().Get(Id());
            state.stage3Seconds += a_realSeconds;

            const float k =
                1.0f + state.stage3Seconds / std::max(1.0f, Settings::fHypoDrainRamp);
            if (auto* player = Player()) {
                if (auto* owner = player->AsActorValueOwner()) {
                    owner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage,
                        RE::ActorValue::kHealth,
                        -(Settings::fHypoDrainPerSec * a_realSeconds * k * k));
                }
            }
        }

        // NOTHING MAY LEAVE REST BLOCKED FOR EVER, which is why this runs on
        // every pass at every stage - including one that ends with the mod
        // switched off. Stage 0 can be arrived at WITHOUT a transition: a load
        // resets the stage in memory while the engine's own flag is part of the
        // save, and a co-save record that fails its version check does the
        // same. The mod would then believe there is nothing to lift and the
        // player would never wait again.
        //
        // Every word of that is true of the LOCKDOWN as well, and it is the
        // more serious of the two: a player who cannot wait is inconvenienced,
        // a player left paralysed cannot play at all.
        SyncLock();
        SyncRestBlock();
    }

    void Hypothermia::OnStageChanged(std::int32_t a_old, std::int32_t a_stage)
    {
        if ((a_stage >= 3) != (a_old >= 3)) {
            Diseases().Get(Id()).stage3Seconds = 0.0f;
        }

        // LEAVING STAGE THREE IS A LOCK COMING OFF, whether or not this session
        // is the one that put it on. The stage came out of our own co-save, so
        // after a load it is the only record there is that the paralysis and
        // the disabled controls this save carries are ours to lift.
        if (a_old >= 3 && a_stage < 3) {
            _lockKnown = true;
            _locked = true;
        }

        // Locked immediately, as v0.4.0 does. This used to be held back
        // 1500 ms because the stage-three message never appeared when the
        // ragdoll dropped in the same instant - but that was the ETYP fault
        // talking: the ability was not instantiating at all. With the record
        // fixed there is nothing to work around.
        //
        // Through the sync rather than by hand: the transition is no longer
        // the only moment the lock can be wrong, so it is no longer the only
        // moment it is decided.
        SyncLock();
        SyncRestBlock();
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

    void Hypothermia::SyncLock()
    {
        if (!Player()) {
            return;
        }

        // Stage three and nothing else. Read off the stage every pass rather
        // than remembered from the transition that set it.
        const bool wanted = Stage() >= 3;
        if (_lockKnown && wanted == _locked) {
            return;
        }

        // NOTHING IS UNLOCKED ON THE STRENGTH OF NOT KNOWING.
        //
        // Paralysis is an actor value and the control map is the whole game's,
        // not ours - a spell, a quest, a cutscene and another mod all use the
        // same two. So the first pass of a session, which knows nothing, may
        // only ever take a lock ON: that is asked for by a stage three we read
        // out of our own co-save, and it is evidence. Taking one OFF needs the
        // same kind of evidence, and "we have not locked anything yet" is the
        // opposite of it.
        //
        // The cases where the lock IS ours but this flag has just been dropped
        // by a load are both handled where the evidence exists: Clear() reads
        // the stage before it drops it, and OnStageChanged is told which stage
        // is being left.
        if (!wanted && !_lockKnown) {
            _lockKnown = true;
            _locked = false;
            return;
        }

        // The shove up only when a lock is coming off. Setting Paralysis back
        // to zero leaves the player lying there; it takes the animation graph
        // to get them up.
        const bool nudge = !wanted && _locked;

        _lockKnown = true;
        SetLock(wanted, nudge);
    }

    void Hypothermia::SyncRestBlock()
    {
        if (!Player()) {
            return;
        }

        // Blocked from stage 1, except indoors while actually warming up: a
        // warm shelter is where this is meant to be slept off. Re-checked every
        // pass so it follows the player in and out of cover - and the setting
        // is read HERE, so that turning it off is an answer of "not blocked"
        // rather than a refusal to answer.
        bool wanted = false;
        if (Settings::bHypoBlocksRest && Stage() >= 1) {
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
        // Not a reset of the condition - that is the disease state, and the
        // co-save carries it. This is only what we believe the ENGINE is doing
        // on our behalf: rest blocked, controls locked. A loaded game brings
        // its own answers to both, so ours are dropped and re-applied by the
        // next pass rather than carried across.
        _restKnown = false;
        _lockKnown = false;
    }

    void Hypothermia::Clear()
    {
        // READ BEFORE IT IS DROPPED. Once BodyCondition::Clear has taken the
        // stage to 0 there is nothing left anywhere that says this was a stage
        // that locks - and that stage is the evidence, because it came out of
        // our own co-save.
        //
        // It used to be `if (_locked)` alone, which is a belief a load
        // invalidates: a game saved at stage three comes back with the
        // paralysis and the disabled controls in it while Forget() has just
        // dropped our answer to false. Switching the mod off then left the
        // player on the floor with nothing able to get them up.
        const bool wasStage3 = Stage() >= 3;

        BodyCondition::Clear();

        if (_locked || wasStage3) {
            SetLock(false, true);
        }
        _lockKnown = true;

        SyncRestBlock();
    }

    std::string Hypothermia::TraceExtra(const Tick&) const
    {
        return fmt::format("onset {:.2f} recover {:.2f} | warmth /{:.2f} | locked {}",
            Settings::fHypoThreshold, Settings::fHypoRecoverThreshold, _warmthSlow,
            _locked);
    }
}
