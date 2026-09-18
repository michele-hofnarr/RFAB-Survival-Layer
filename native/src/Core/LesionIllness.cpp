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
        // scratch settles. v0.4.0's number, and it is what stops the "not quite
        // ill yet" state from lasting forever.
        constexpr float HEALING_SPEEDUP = 3.0f;
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
        auto& state = Diseases().Get(Id());
        state.prog = std::clamp(state.prog + _drift * a_tick.gameHours, -100.0f, 0.0f);

        if (state.prog <= -limit) {
            // TAKEN BEFORE THE CALLER RESETS IT. state is a reference into the
            // table and the contract path zeroes that same object, so reading
            // it afterwards printed "contracted (P 0)" for every contraction
            // there had ever been - a line that cannot tell a threshold that
            // was reached from one that was not.
            logger::info("dz {}: contracted (P {:.0f} of -{:.0f})", Id(), state.prog,
                limit);
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
        // stage. No roll, and no clamp pinning P at the bottom - v0.4.0 is
        // explicit that this one is meant to be predictable.
        const float limit = Limit();

        auto&       state = Diseases().Get(Id());
        const float before = state.prog;
        state.prog += _drift * a_tick.gameHours;

        std::int32_t step = 0;
        if (state.prog <= -limit) {
            step = 1;    // worse
        } else if (state.prog >= limit) {
            step = -1;   // better
        }

        if (step != 0) {
            logger::info("dz {}: P {:+.0f} crossed -+{:.0f}", Id(), before, limit);
            Diseases().ResetP(Id());
        }
        return step;
    }

    std::string LesionIllness::TraceExtra(const Tick&) const
    {
        return fmt::format("drift={:+.1f}/h of -+{:.0f}", _drift, Limit());
    }
}
