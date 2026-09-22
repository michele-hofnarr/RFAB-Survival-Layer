#include "PCH.h"

#include "Core/LesionIllness.h"

#include "Core/Elemental.h"
#include "Core/Player.h"
#include "Core/Random.h"
#include "Settings.h"

#include <spdlog/fmt/fmt.h>

namespace RSL
{
    namespace
    {
        // An established lesion closes about three times faster than a fresh
        // scratch settles. v0.4.0's number.
        constexpr float HEALING_SPEEDUP = 3.0f;

        // How long a scratch that never set in takes to fade, out of combat:
        // twenty game minutes from the floor to nothing.
        //
        // AT STAGE 0 THE AXES DO NOT GATE IT. Everywhere else recovery needs
        // all three above their safe mark, and that is right for an illness
        // somebody has. This is the state of not having one - a counter that
        // crept up from a few sparks - and making it wait on being rested, fed
        // and warm left it sitting just under the threshold for hours, so the
        // next stray hit caught a lesion that had no business being caught.
        constexpr float CALM_HOURS = 20.0f / 60.0f;

        [[nodiscard]] bool InCombat()
        {
            auto* player = Player();
            return player && player->IsInCombat();
        }
    }

    float LesionIllness::Limit()
    {
        return std::min(100.0f, Settings::fElemLesionContractP);
    }

    bool LesionIllness::Enabled() const
    {
        return Illness::Enabled() && Settings::bElemLesionEnabled;
    }

    bool LesionIllness::Wounded() const
    {
        return Stage() > 0 || P() < 0.0f;
    }

    void LesionIllness::Idle()
    {
        // Drop what was queued rather than banking it. Banked damage would
        // arrive all at once the moment the switch came back.
        Elemental::GetSingleton().TakeLesionP();
    }

    void LesionIllness::Observe(const Tick& a_tick, std::int32_t a_stage)
    {
        // Fold in what the hits and bandages did since the last pass, BEFORE
        // the drift, so a burst of damage counts even if the tick that follows
        // it is a healing one.
        if (const float pending = Elemental::GetSingleton().TakeLesionP();
            pending != 0.0f) {
            Diseases().AddP(Id(), pending);
            if (Settings::bDebugLog) {
                logger::info("dz {}: {:+.1f} folded in, P {:+.1f} of -{:.0f} (stage {})",
                    Id(), pending, P(), Limit(), a_stage);
            }
        }

        // Worked out here and used by both paths below. _band is set with it so
        // the shared trace says something true about an illness that does not
        // run on bands at all: deep cold is its own worsening condition, and
        // anything short of a clear character merely holds it.
        const bool coldDeep = a_tick.cold <= Settings::fElemLesionColdAt;

        _drift = 0.0f;
        if (coldDeep) {
            _band = AxisBand::kWorsen;
            _drift = -(100.0f / std::max(0.01f, Settings::fDiseaseProgressHours));
        } else if (a_stage == 0) {
            // Not ill yet, and out of a fight: the counter simply goes back
            // down. See CALM_HOURS for why the axes have no say here.
            if (InCombat()) {
                _band = AxisBand::kHold;
            } else {
                _band = AxisBand::kHeal;
                _drift = 100.0f / CALM_HOURS;
            }
        } else if (AxisState(a_tick.sleep, a_tick.hunger, a_tick.cold, a_tick.undead) ==
                   AxisBand::kHeal) {
            _band = AxisBand::kHeal;
            _drift = (100.0f / std::max(0.01f, Settings::fDiseaseDecayHours)) *
                     (1.0f + DiseaseResist() * 0.01f);
            if (a_stage >= 1) {
                _drift *= HEALING_SPEEDUP;
            }
        } else {
            _band = AxisBand::kHold;
        }
    }

    bool LesionIllness::Contracts(const Tick& a_tick)
    {
        const float limit = Limit();

        // Not ill yet: P only ever sits at or below zero, and reaching the
        // threshold is what catches it.
        //
        // BOTH SIDES OF THE DRIFT ARE TESTED, and that is not belt and braces -
        // see the note on Progress. The value the hits left can be at the
        // floor while the drift is lifting it off again, and the floor IS the
        // threshold now.
        auto&       state = Diseases().Get(Id());
        const float afterHits = state.prog;
        state.prog = std::clamp(state.prog + _drift * a_tick.gameHours, -100.0f, 0.0f);

        if (afterHits <= -limit || state.prog <= -limit) {
            // TAKEN BEFORE THE CALLER RESETS IT. state is a reference into the
            // table and the contract path zeroes that same object, so reading
            // it afterwards printed "contracted (P 0)" for every contraction
            // there had ever been - a line that cannot tell a threshold that
            // was reached from one that was not.
            logger::info("dz {}: contracted (P {:.0f} of -{:.0f})", Id(),
                std::min(afterHits, state.prog), limit);
            return true;
        }

        // The other way in. Deep cold by itself is too brief a window -
        // hypothermia resolves it quickly one way or the other - so serious
        // hypothermia rolls for it instead, once per game hour.
        if (Diseases().Stage("HY"sv) >= 2 && Diseases().RollDue(Id(), 1.0f) &&
            RollPercent() < Settings::fElemLesionHypoChance) {
            logger::info("dz {}: contracted from hypothermia", Id());
            return true;
        }
        return false;
    }

    std::int32_t LesionIllness::Progress(const Tick& a_tick)
    {
        // Deterministic, and crossing the same threshold either way moves a
        // stage. No roll: v0.4.0 is explicit that this one is meant to be
        // predictable.
        //
        // THE WORSENING IS TESTED ON THE VALUE THE HITS LEFT, before the drift
        // is added, and v0.4.0 says why in as many words: "test the post-hit
        // value BEFORE drift, so a maxed-out barrage (P slammed to the -100
        // clamp) still trips it instead of drift nudging it back". The port
        // dropped that ordering and got away with it only because the
        // threshold was 70 and the floor -100, so the barrage overshot the
        // test by thirty points.
        //
        // At a threshold of 100 the floor IS the test. A healing drift - every
        // axis clear - lifts P off -100 by a fraction of a point each tick, the
        // comparison misses by that fraction, and a character standing in fire
        // never advances a stage at all. Which is the bug that number was
        // hiding.
        const float limit = Limit();

        auto&       state = Diseases().Get(Id());
        // The value the hits left: what the worsening is tested on, and what
        // the log prints as the value that crossed.
        const float afterHits = state.prog;
        state.prog = std::clamp(state.prog + _drift * a_tick.gameHours,
            -100.0f, 100.0f);

        // BOTH ENDS TAKE BOTH VALUES. The healing side has the same shape as
        // the worsening one and for the same reason: a clean linen cloth is a
        // saturating input too, and AddP stops it at +100 exactly. Bandage a
        // deep lesion and step into deep cold on the same tick, and the drift
        // pulls P off +100 before the post-drift test ever sees it. Rarer than
        // the barrage - which saturated every tick under fire - but the same
        // fault, so it is closed the same way.
        std::int32_t step = 0;
        if (afterHits <= -limit || state.prog <= -limit) {
            step = 1;    // worse
        } else if (afterHits >= limit || state.prog >= limit) {
            step = -1;   // better
        }

        if (step != 0) {
            // BOTH ENDS OF THE STEP. This printed `before` alone, which is the
            // one number that by definition has NOT crossed anything - so the
            // line said "P -66 crossed -+70" and read as a threshold test that
            // had fired early.
            logger::info("dz {}: P {:+.0f} -> {:+.0f} crossed -+{:.0f}", Id(),
                afterHits, state.prog, limit);
            Diseases().ResetP(Id());
        }
        return step;
    }

    std::string LesionIllness::TraceExtra(const Tick&) const
    {
        return fmt::format("drift={:+.1f}/h of -+{:.0f}", _drift, Limit());
    }
}
