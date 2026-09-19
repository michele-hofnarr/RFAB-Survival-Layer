#include "PCH.h"

#include "Core/ColdIllness.h"

#include "Core/Player.h"
#include "Core/Random.h"
#include "Settings.h"

namespace RSL
{
    float ColdIllness::ContractChance(float a_cold)
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

    bool ColdIllness::Contracts(const Tick& a_tick)
    {
        // Once a game hour, not once a frame. RollDue stamps on success, so
        // this reads as "if an hour's worth of chance is owed, spend it".
        if (!Diseases().RollDue(Id(), 1.0f)) {
            return false;
        }

        const float chance = ContractChance(a_tick.cold);
        if (chance <= 0.0f || RollPercent() >= chance) {
            return false;
        }

        // THE ONLY ILLNESS THAT CAUGHT ITSELF IN SILENCE. Every other one says
        // why: the lesions print the threshold they crossed, the RFAB wrappers
        // that they adopted a record, and a hit or a bad meal goes through
        // Illness::Contract, which logs. The cold came through this hook, and
        // this hook printed nothing - so a caught cold appeared in the log as
        // a bare "disease CC: stage -> 1" with no reading behind it.
        logger::info("dz {}: contracted from the cold ({:.2f} left, {:.0f}%/h)", Id(),
            a_tick.cold, chance);
        return true;
    }

    std::string ColdIllness::TraceExtra(const Tick& a_tick) const
    {
        return fmt::format("chance/h={:.1f}", ContractChance(a_tick.cold));
    }
}
