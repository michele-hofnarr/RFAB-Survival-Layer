#pragma once

// The player's own campfire.
//
// Cast the lesser power, spend firewood, and a fire goes down in front of you
// for a set number of game hours. With RFAB's Chef perk it comes with a
// cooking spit and pot. Only one at a time: lighting a new one retires the old.
//
// Two things about it are not obvious and are load-bearing.
//
// The old set is retired only AFTER the new one is standing. Disabling a
// reference is not instant, and taking the old fire down first leaves a moment
// with no fire at all - which the cold model would notice.
//
// Its lifetime is game time, not real time. A fire lit and then slept beside
// has to burn down over those hours the same as one you sat next to, so the
// deadline is a game-time stamp rather than a countdown.

namespace RSL
{
    class Campfire
    {
    public:
        static Campfire& GetSingleton();

        // The power was cast. Does every check and either lights one or says
        // why not.
        void Light();

        // Called from the tick: burns the fire out when its hours are up, and
        // grants or revokes the power with the perk.
        void Update();

        // Put it out now - the mod went off, or the player said yes below.
        void Extinguish();

        // The player activated something. If it was their own fire, ask whether
        // they meant to put it out - the cook pot sits right on top of it and is
        // easy to hit by mistake - and act on the answer.
        void OnActivated(const RE::TESObjectREFR* a_target);

        // Is the player standing at their OWN fire? This is what the shelter
        // rules ask; any old fire in the world warms you, but only your own
        // counts as a camp.
        [[nodiscard]] bool PlayerAtOwnFire() const;

        [[nodiscard]] RE::TESObjectREFR* Fire() const;

        // State lives in the co-save with everything else.
        struct State
        {
            RE::FormID fire{ 0 };
            RE::FormID spit{ 0 };
            RE::FormID pot{ 0 };
            float      burnsUntilDays{ 0.0f };   // game time, not a countdown
        };

        [[nodiscard]] State& Data() { return _state; }
        void                 Reset() { _state = State{}; }

    private:
        // Take the old set out of the world without deleting it - it has to
        // stop being something a raycast can hit before the new spot is found.
        void Hide(const State& a_old);
        void Retire(const State& a_old);

        State _state{};

        // The cast arrives twice - once from the magic effect and once from the
        // apply event that backs it up - so a short real-time gate de-dupes
        // them, and doubles as the rate limit on the "no fuel" notice.
        std::chrono::steady_clock::time_point _lastCast{};
        bool                                  _castOnce{ false };
    };
}
