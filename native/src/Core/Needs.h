#pragma once

#include "Core/Climate.h"

// The survival axes and the state behind them.
//
// Everything is a reserve in 0..1, where 1 is fully rested, fed and warm and 0
// is critical. The Papyrus version counted the other way - the number went up
// as you got worse - which meant every read had to be inverted before it could
// be shown, and the inversion was duplicated in a dozen places. Storing what
// the bar shows removes that whole class of mistake.
//
// State lives in the co-save through SKSE::SerializationInterface, not in
// StorageUtil, so it costs nothing per tick and cannot desync from the save.

namespace RSL
{
    class Needs
    {
    public:
        static Needs& GetSingleton();

        // Save/load plumbing. Register once from SKSEPlugin_Load.
        static void InstallSerialization();

        // Advances by however much game time has passed since the last call.
        // Safe to call every frame; it does its own bookkeeping.
        void Update();

        // The game said the player slept this many hours. Rest is credited
        // straight away, and the clock advance that reports the same span is
        // then spent without also draining rest for it.
        void NoteSlept(float a_gameHours);

        // The player activated something. If it is furniture that can be slept
        // on, this is where the night is about to happen, so remember whether
        // it is a bedroll.
        //
        // ASKED AT ACTIVATION BECAUSE THERE IS NOWHERE ELSE TO ASK. Skyrim
        // never occupies the bed's furniture - no pose, no lying animation, the
        // screen simply fades - so GetOccupiedFurniture is empty the whole
        // night through and the sit/sleep state never changes. See the note at
        // the top of Events.cpp, which is where three earlier attempts to infer
        // any of this from the Sleep/Wait menu are recorded. v0.4.0 met the
        // same wall for the tent and answered it the same way: note the
        // surroundings when the player lies down, not when they get up.
        void NoteBed(RE::TESObjectREFR* a_ref);

        // The player is asleep from now until told otherwise.
        //
        // WHY THE TICK HAS TO BE TOLD. The clock runs on during the fade, and
        // the tick sees that as ordinary waking time - so the whole night was
        // charged twice over: once by the tick at the waking rate, and once
        // again by NoteSlept, which charges the span explicitly. On one sleep
        // number that hid behind the clamp at a full bar; once the axis grew a
        // second half the bar stopped clamping and it came straight out, as a
        // night in a bedroll that ATE the rest earned in a bed.
        //
        // This is v0.4.0's K_SLEEPING, set at OnSleepStart and checked at the
        // top of AdvanceSleep / AdvanceHunger / AdvanceCold ("Axis is paused
        // during sleep, else the tick also charges the slept hours"). The port
        // kept the a_chargeSleep parameter, which answers a different question,
        // and dropped the flag.
        void SetSleeping(bool a_sleeping);

        // What one item would do to the hunger bar.
        //
        // One function for both the eating and the inventory preview, which is
        // the only way the promise on screen cannot drift from the result -
        // v0.4.0 says exactly this about its HungerAfterEating, and it is right.
        struct Meal
        {
            bool  feeds{ false };     // false: a drink, or not food at all
            bool  rawWeak{ false };   // raw meat on a weak stomach: straight to empty
            bool  special{ false };   // a proper meal: RFAB_SpecialFood
            float restore{ 0.0f };    // what it is worth, as a fraction of a bar
        };

        [[nodiscard]] static Meal Assess(RE::AlchemyItem* a_item);

        // How much bought time one item carries, as a fraction of the bar, or
        // zero. Read off the item's own frost-resistance effect: nothing is
        // marked up in RFAB's records for this, and nothing needs to be.
        //
        // Same discipline as Assess: one function for both the preview and the
        // drinking, so the promise on screen cannot drift from the result.
        [[nodiscard]] static float ColdGift(RE::AlchemyItem* a_item);

        // A draught of frost resistance was drunk. What it buys goes on top of
        // the reserve, and cannot push the bar past full.
        void OnDrankWarm(float a_gift);

        // What the inventory preview should promise, or a negative number if
        // the item would not move the bar at all.
        //
        // Deliberately NOT the same as what eating it does. Raw meat on a weak
        // stomach is worth exactly what the preview says and empties the bar
        // anyway, and the preview says so cheerfully - you find out by eating
        // it. The arithmetic is still shared with the eating path; only this
        // one line differs, on purpose.
        [[nodiscard]] float HungerPreview(RE::AlchemyItem* a_item) const;

        // Food was eaten; a_restore is a fraction of a full bar. a_special
        // decides which half of the bar it goes into - and the bar cannot pass
        // full, so what does not fit is simply lost.
        //
        // A meal displaces fast food out of the bar rather than stacking on
        // top of it, so eating properly converts what was snacked into
        // something that keeps. The sum is unchanged by that; only the split
        // is. Fast food does not displace a meal.
        void OnAte(float a_restore, bool a_special);

        // Raw food on a weak stomach: the bar goes to empty, it is not merely
        // left where it was. v0.4.0's HungerAfterEating returns the maximum
        // deprivation for this case.
        void EmptyHunger();

        // ...and it doubles the player over. The engine's own stagger, driven
        // through the animation graph the same way a hit drives it: set the
        // magnitude, send the event. No form, no idle to install, and nothing
        // that can be left running across a save.
        static void StaggerPlayer(float a_force);

        [[nodiscard]] float Sleep() const
        {
            return std::clamp(_state.sleep + _state.sleepTemp, 0.0f, 1.0f);
        }

        // The part of that bar earned in a bedroll. The widget hatches it;
        // nothing else cares, which is the whole difference from the cold
        // buffer - there the penalties read the base and ignore the top half.
        [[nodiscard]] float SleepTemp() const { return _state.sleepTemp; }

        // The earned half alone. The ILLNESSES read this rather than Sleep():
        // a bar propped up with naps is not a rested body, and P should not
        // heal on it. Penalties and the widget still read the sum - what the
        // player can act on right now is the whole bar.
        [[nodiscard]] float SleepBase() const { return _state.sleep; }
        [[nodiscard]] float Hunger() const
        {
            return std::clamp(_state.hungerSpecial + _state.hungerFast, 0.0f, 1.0f);
        }

        // The part of that bar filled with anything but a proper meal. The
        // widget hatches it; nothing else cares.
        [[nodiscard]] float HungerFast() const { return _state.hungerFast; }

        // The half filled with proper meals, for the same reason as SleepBase:
        // a stomach full of apples does not mend anything.
        [[nodiscard]] float HungerBase() const { return _state.hungerSpecial; }
        // The reserve everything downstream acts on. A bought buffer is
        // not warmth: it does not thaw the crust, cure a stage of hypothermia
        // or unblock a teleport.
        [[nodiscard]] float ColdBase() const { return _state.cold; }

        // What the bar shows: the reserve plus whatever time was bought on top
        // of it. The widget only.
        [[nodiscard]] float Cold() const
        {
            return std::clamp(_state.cold + _state.coldTemp, 0.0f, 1.0f);
        }

        // The bought part of that, hatched at the top of the fill exactly as
        // fast food is. The widget only.
        [[nodiscard]] float ColdTemp() const { return _state.coldTemp; }

        // Vampires and the like neither sleep nor eat. v0.4.0 skips both axes
        // for them and hides both bars; only cold still applies.
        [[nodiscard]] bool Undead() const { return _undead; }

        // Game hours the most recent Update actually advanced. Consumers that
        // need to run on the same clock read this rather than guessing.
        [[nodiscard]] float HoursThisTick() const { return _hoursThisTick; }

        // The last sampled surroundings, for the HUD and for the log. Not
        // saved: it is derived from where the player is standing.
        [[nodiscard]] const Climate& LastClimate() const { return _lastClimate; }

        void Reset();

    private:
        friend struct NeedsSerialization;

        struct State
        {
            // Sleep is one bar made of two halves, exactly as hunger is, and
            // for a matching reason: a night in a bedroll was worth as much as
            // a night at the inn. Now a bedroll still rests you just as much -
            // it simply does not KEEP. Only the drain and the drawing know
            // there are two; everything downstream reads Sleep(), the sum.
            float sleep{ 1.0f };      // earned in a real bed
            float sleepTemp{ 0.0f };  // a nap; invariant: sleep + sleepTemp <= 1

            // Hunger is one bar made of two halves, because Kazan Edition put
            // crates of fruit and veg everywhere and food stopped being a
            // constraint. Cutting what an apple is worth would have been the
            // wrong lever - a character able to eat a ton of apples and stay
            // hungry reads as broken. So an apple fills you just as much; it
            // simply does not last.
            //
            // Everything downstream - penalties, diseases, the widget's value -
            // still sees ONE number, Hunger(). Only the drain and the drawing
            // know there are two.
            float hungerSpecial{ 1.0f };
            float hungerFast{ 0.0f };

            // The cold axis is one bar made of two parts, for the same
            // reason hunger is: a potion of frost resistance had nothing to
            // give it. Raising resist moves the equilibrium, which is worth
            // something on a long walk and nothing at all in a blizzard you
            // are already losing.
            //
            // So a draught buys TIME instead. coldTemp sits on top of cold and
            // is spent first, which holds cold still while it lasts - and
            // holding cold still is the whole of it: penalties, hypothermia,
            // the common cold, lesions, RFAB's own illnesses, the ice crust,
            // the screen and the teleport block all read cold and know nothing
            // about the buffer. Only the bar shows the sum.
            float cold{ 1.0f };
            float coldTemp{ 0.0f };   // invariant: cold + coldTemp <= 1

            // How wet the player is, 0..1. Its own axis rather than a
            // by-product of the weather, because it lags: you stay soaked after
            // the rain stops, and that is the whole mechanic.
            float wetness{ 0.0f };

            // Game time of the last update, in days. Negative means "no reading
            // yet", which is how a fresh game and a just-loaded save both avoid
            // integrating a bogus first delta.
            float lastGameDays{ -1.0f };
        };

        // a_chargeSleep off means these hours were spent asleep: hunger and
        // cold still run, the sleep axis does not.
        // a_coldRateMult scales how fast the cold axis approaches its
        // target for these hours only. It is 1 everywhere except a night
        // slept in the player's own tent, which is the one thing that
        // slows the fall rather than raising the temperature.
        void Advance(float a_gameHours, bool a_chargeSleep,
            float a_coldRateMult = 1.0f);

        State   _state;
        Climate _lastClimate;

        // Hours just slept, waiting to be handed to the illnesses on the next
        // pass. The axes are charged at the event; only the illnesses, which
        // read HoursThisTick, still need to see them. Transient, so not in
        // State.
        float _sleptHoursPending{ 0.0f };

        // What the last activated sleepable furniture was. Transient, so not in
        // State: a save records the night's result, not what the player was
        // standing in front of. Reset() clears it, which is also what a load
        // does.
        // Where the wetness axis stood last pass, for announcing the two
        // edges. Negative means "not read yet", which is how a load avoids
        // announcing a state it merely restored.
        float _wetSeen{ -1.0f };

        bool  _bedKnown{ false };
        bool  _bedIsBedroll{ false };

        // Between the sleep start and stop events. Transient for the same
        // reason: a save taken mid-sleep is not a thing, and a load must not
        // come back believing the player is still in bed.
        bool  _sleeping{ false };

        bool  _undead{ false };
        float _hoursThisTick{ 0.0f };
    };
}
