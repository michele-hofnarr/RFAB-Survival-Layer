#include "PCH.h"

#include "Core/Penalties.h"

#include "Core/Forms.h"
#include "Core/Player.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        [[nodiscard]] float Clamp(float a_v, float a_cap)
        {
            return std::clamp(a_v, 0.0f, a_cap);
        }

        // Axis load 0..1 on a two-point ramp: nothing up to the grace line,
        // then linear to the maximum.
        //
        // v0.4.0 feeds this a deprivation counter that rises as things get
        // worse. This side stores a reserve that falls, so it is converted
        // here rather than everywhere else.
        [[nodiscard]] float Ramp(float a_reserve, float a_safe)
        {
            const float used = 1.0f - std::clamp(a_reserve, 0.0f, 1.0f);
            const float grace = 1.0f - std::clamp(a_safe, 0.0f, 1.0f);
            if (used <= grace) {
                return 0.0f;
            }
            if (grace >= 1.0f) {
                return 1.0f;
            }
            return std::min(1.0f, (used - grace) / (1.0f - grace));
        }

        [[nodiscard]] float StepSize()
        {
            return std::max(1.0f, Settings::fTierStep);
        }

        // Percent quantised DOWN onto the grid. Used for SpeedMult, whose
        // magnitude is the percentage itself: it is already on RFAB's
        // five-point grid, and rounding up would put the player at -5% speed
        // the instant an axis crossed its grace line.
        [[nodiscard]] float QuantPct(float a_pct, float a_cap)
        {
            const float step = StepSize();
            const float v = Clamp(a_pct, a_cap);
            return static_cast<float>(static_cast<int>(v / step)) * step;
        }

        // Pool penalty in POINTS, on the grid, rounded UP - against the player.
        // Zero stays zero, and the cap is floored onto the grid first so
        // rounding up can never push past it.
        [[nodiscard]] float QuantPoints(float a_pct, float a_base, float a_cap)
        {
            if (a_pct <= 0.0f || a_base <= 0.0f) {
                return 0.0f;
            }
            const float step = StepSize();

            const float ceiling =
                static_cast<float>(static_cast<int>(0.01f * a_cap * a_base / step)) * step;
            if (ceiling <= 0.0f) {
                return 0.0f;
            }

            const float pts = 0.01f * Clamp(a_pct, a_cap) * a_base;
            int         n = static_cast<int>(pts / step);
            if (pts > static_cast<float>(n) * step) {
                ++n;
            }
            return std::min(static_cast<float>(n) * step, ceiling);
        }

        [[nodiscard]] float BaseValue(RE::ActorValue a_av)
        {
            auto* player = Player();
            auto* owner = player ? player->AsActorValueOwner() : nullptr;
            return owner ? owner->GetBaseActorValue(a_av) : 0.0f;
        }
    }

    Penalties& Penalties::GetSingleton()
    {
        static Penalties singleton;
        return singleton;
    }

    void Penalties::Update(float a_sleep, float a_hunger, float a_cold, bool a_undead)
    {
        if (!Forms::Ready()) {
            return;
        }

        const float cap = Settings::fPenaltyCap;
        const float primary = Settings::fPenaltyPrimary;
        const float cross = Settings::fPenaltyCross;
        const float spd = Settings::fPenaltySpeed;

        const float fSleep = a_undead ? 0.0f : Ramp(a_sleep, Settings::fSleepSafe);
        const float fHunger = a_undead ? 0.0f : Ramp(a_hunger, Settings::fHungerSafe);
        const float fCold = Ramp(a_cold, Settings::fColdSafe);

        // Each axis contributes to SpeedMult, and the total is hard-capped:
        // RFAB_RestrictMovementOnZeroMS locks the player in place at
        // SpeedMult <= 0, so this must never get near it.
        float       sSpd = fSleep * spd;
        float       hSpd = fHunger * spd;
        float       cSpd = fCold * spd;
        const float total = sSpd + hSpd + cSpd;
        if (total > Settings::fSpeedCap && total > 0.0f) {
            const float k = Settings::fSpeedCap / total;
            sSpd *= k;
            hSpd *= k;
            cSpd *= k;
        }

        ApplyAxis(Forms::abSleep, _sleep,
            fSleep * cross, fSleep * primary, fSleep * cross, sSpd, cap);
        ApplyAxis(Forms::abHunger, _hunger,
            fHunger * cross, fHunger * cross, fHunger * primary, hSpd, cap);
        ApplyAxis(Forms::abCold, _cold,
            fCold * primary, fCold * cross, fCold * cross, cSpd, cap);

        // Full-bar bonuses. Flat, not ramped: either the axis is within the
        // threshold of full or it is not.
        const bool  on = Settings::bBonusEnabled;
        const float pct = Settings::fBonusRegenPct;

        // A misconfigured zero would put the bonus out of reach entirely.
        float threshold = Settings::fBonusThresholdPct * 0.01f;
        if (threshold <= 0.0f) {
            threshold = 0.10f;
        }
        const float full = 1.0f - threshold;

        SetBonus(Forms::abBonusWarm, _bonusWarm, on && a_cold >= full, pct);
        SetBonus(Forms::abBonusRest, _bonusRest, on && !a_undead && a_sleep >= full, pct);
        SetBonus(Forms::abBonusFed, _bonusFed, on && !a_undead && a_hunger >= full, pct);
    }

    void Penalties::SetBonus(RE::SpellItem* a_ability, int& a_state, bool a_on, float a_pct)
    {
        auto* player = Player();
        if (!a_ability || !player || a_ability->effects.empty()) {
            return;
        }

        const int signature = a_on ? (1 + static_cast<int>(a_pct) * 2) : 0;
        if (signature == a_state) {
            return;
        }
        a_state = signature;

        a_ability->effects[0]->effectItem.magnitude = a_pct;
        player->RemoveSpell(a_ability);
        if (a_on) {
            player->AddSpell(a_ability);
        }

        if (Settings::bDebugLog) {
            logger::info("bonus {}: {}",
                a_ability->GetFormEditorID() ? a_ability->GetFormEditorID() : "?",
                a_on ? "on" : "off");
        }
    }

    void Penalties::ApplyAxis(RE::SpellItem* a_ability, AxisState& a_state,
        float a_pctHealth, float a_pctMagicka, float a_pctStamina, float a_pctSpeed,
        float a_cap)
    {
        auto* player = Player();
        if (!a_ability || !player || a_ability->effects.size() < 4) {
            return;
        }

        const float qH = QuantPoints(a_pctHealth, BaseValue(RE::ActorValue::kHealth), a_cap);
        const float qM = QuantPoints(a_pctMagicka, BaseValue(RE::ActorValue::kMagicka), a_cap);
        const float qS = QuantPoints(a_pctStamina, BaseValue(RE::ActorValue::kStamina), a_cap);
        const float qSpd = QuantPct(a_pctSpeed, a_cap);

        // Refresh only when a quantised value moves. Text, not a packed
        // integer: point magnitudes scale with the pool, so a big-magicka build
        // would overflow any fixed-width packing and stick on a stale penalty.
        const auto signature = std::format("{}|{}|{}|{}", qH, qM, qS, qSpd);
        if (signature == a_state.signature) {
            return;
        }
        a_state.signature = signature;

        if (Settings::bDebugLog) {
            logger::info("penalty {}: H={} M={} S={} Spd={}",
                a_ability->GetFormEditorID() ? a_ability->GetFormEditorID() : "?",
                qH, qM, qS, qSpd);
        }

        a_ability->effects[0]->effectItem.magnitude = qH;
        a_ability->effects[1]->effectItem.magnitude = qM;
        a_ability->effects[2]->effectItem.magnitude = qS;
        a_ability->effects[3]->effectItem.magnitude = qSpd;

        // Without remove and re-add the new magnitude does not take.
        player->RemoveSpell(a_ability);
        const bool any = qH > 0.0f || qM > 0.0f || qS > 0.0f || qSpd > 0.0f;
        if (any) {
            player->AddSpell(a_ability);
        }

        // SpeedMult does not recompute after add or remove - the engine updates
        // speed only on a weight change - so a CarryWeight nudge forces it.
        if (qSpd > 0.0f || a_state.hadSpeed) {
            if (auto* owner = player->AsActorValueOwner()) {
                owner->RestoreActorValue(
                    RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kCarryWeight, -0.1f);
                owner->RestoreActorValue(
                    RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kCarryWeight, 0.1f);
            }
        }
        a_state.hadSpeed = qSpd > 0.0f;
    }

    void Penalties::ClearAll()
    {
        auto* player = Player();
        if (!player || !Forms::Ready()) {
            return;
        }

        // Without this, switching the mod off leaves penalties sitting on the
        // pools - v0.4.0 calls that the most likely bug report there is.
        for (auto* ability : { Forms::abSleep, Forms::abHunger, Forms::abCold,
                 Forms::abBonusWarm, Forms::abBonusRest, Forms::abBonusFed }) {
            if (ability) {
                player->RemoveSpell(ability);
            }
        }
        _sleep = AxisState{};
        _hunger = AxisState{};
        _cold = AxisState{};
        _bonusWarm = _bonusRest = _bonusFed = -1;
    }
}
