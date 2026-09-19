#pragma once

// Stat penalties from the three needs axes.
//
// A straight port of ApplyPenalties/ApplyAxis from v0.4.0, including the parts
// that look odd until you know why:
//
//   - The penalty is carried by three ability spells whose effect magnitudes
//     are rewritten in place. ModActorValue is deliberately not used - v0.4.0
//     records that it desyncs across save and load - and nothing about that
//     changes in C++, because it is the same engine and the same forms.
//
//   - Magnitudes are POSITIVE. The effects are flagged Detrimental, so the
//     engine subtracts them.
//
//   - Everything is computed off the BASE actor value, not the current maximum,
//     so penalties do not compound with each other.
//
//   - Effect order is fixed by the generator: 0 Health, 1 Magicka, 2 Stamina,
//     3 SpeedMult. Changing it there silently breaks this.
//
//   - Pool penalties are quantised in POINTS and rounded UP, against the
//     player; SpeedMult is quantised in percent and rounded DOWN. RFAB's whole
//     stat sheet is built on multiples of five, so what the player reads has to
//     be one too - and quantising the percentage would not achieve it, because
//     ten percent of a base of 120 is twelve points.
//
//   - The spell is only refreshed when a quantised value actually moves.
//     Rewriting a spell's magnitude is a form change that lands in the save, so
//     doing it every tick would churn the save and the log both.

namespace RSL
{
    class Penalties
    {
    public:
        static Penalties& GetSingleton();

        // Recomputes and applies penalties and bonuses. Cheap when nothing
        // moved: both refresh only when their quantised state changes.
        void Update(float a_sleep, float a_hunger, float a_cold, bool a_undead);

        // Removes everything. For the mod being switched off.
        void ClearAll();

        // A load: what we last applied was applied to the game that has gone.
        //
        // Both refreshes are deduped on a signature of the quantised values,
        // and the values come from the needs - so a save loaded at much the
        // same reserves produces the same signature and the refresh is skipped
        // entirely, on a player who has none of these abilities. The bonus is
        // the visible half: a character loaded into a full cold bar kept no
        // regeneration at all until the axis dipped and came back.
        //
        // Nothing is taken off the player here. The next pass applies whatever
        // it decides, which is the one answer that is certainly right.
        void Forget();

    private:
        struct AxisState
        {
            std::string signature;   // last applied, as text - see the note below
            bool        hadSpeed{ false };
        };

        void ApplyAxis(RE::SpellItem* a_ability, AxisState& a_state,
            float a_pctHealth, float a_pctMagicka, float a_pctStamina, float a_pctSpeed,
            float a_cap);

        // One bonus ability, added while its axis is nearly full. Deduped on
        // (active, percent) so the form is only touched on a real change.
        void SetBonus(RE::SpellItem* a_ability, int& a_state, bool a_on, float a_pct);

        int _bonusWarm{ -1 };
        int _bonusRest{ -1 };
        int _bonusFed{ -1 };

        AxisState _sleep;
        AxisState _hunger;
        AxisState _cold;

    };
}
