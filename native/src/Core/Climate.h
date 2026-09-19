#pragma once

// Where the player is, thermally.
//
// The Papyrus version had no temperature. It multiplied a stack of percentages
// together - hold severity x weather x night x swimming x fire - which meant no
// single factor could be tuned without moving every other one, and there was
// nothing meaningful to show the player. Here everything lands on one number in
// degrees, and the axis is driven from that.
//
// The place comes from the map rather than from the player's location. Holds
// were the first answer and they were the wrong one twice over: most of
// Skyrim's outdoors belongs to no location at all, and the nine per-hold floor
// heights were invented. What answers now is a temperature baked into Skyrim's
// own terrain grid, a vertex every 128 units - Core/ClimateMap.h for how it is
// read, docs/CLIMATE_MAP.md for how it is built.

namespace RSL
{
    struct Climate
    {
        float temperature{ 10.0f };  // degrees, the number everything reduces to
        float wetness{ 0.0f };       // 0..1
        float warmth{ 0.0f };        // points of insulation from gear and resist
        bool  sheltered{ false };    // interior, or something overhead
        bool  soaking{ false };      // rain on you with no cover, or swimming
        bool  inWater{ false };      // swimming: soaks at once, not at rain's pace
        bool  ownFire{ false };      // at a fire of one's own. For the log:
                                     // it is worth what any fire is worth
        bool  interior{ false };
        bool  nearFire{ false };     // a world fire in range, or a lit torch

        // What answered: "the map", "interior", "cold interior" or "off the
        // map". For the log only - nothing reads it back.
        const char* placeName{ "?" };
        float       worldZ{ 0.0f };

        // What the map said: the ground's own temperature at this spot, and
        // how far above that ground the player is standing. When `baked` is
        // false the map had no answer here and fTempUnknown was used.
        bool        baked{ false };
        float       surfaceTemp{ 0.0f };
        float       airAbove{ 0.0f };

        // Every term of the temperature, kept separately so the debug log can
        // print how the number was assembled rather than only the number. This
        // is not decoration: the constants will be retuned repeatedly, and a
        // single total cannot say which term is wrong.
        float       baseTemp{ 0.0f };       // the place's own degrees
        float       weatherOffset{ 0.0f };
        float       dayOffset{ 0.0f };      // the 24-hour curve, never positive
        float       fireOffset{ 0.0f };     // any fire, including that one

        // The sky is crossfaded by the engine, so the offset is a mix of two
        // weathers: `weatherName` is the one coming in, `weatherFrom` the one
        // going out, `weatherBlend` how far along (1.0 when nothing is in
        // transition, and then the two names are the same).
        const char* weatherName{ "clear" };
        const char* weatherFrom{ "clear" };
        float       weatherBlend{ 1.0f };
        float       hour{ 12.0f };

        // The two halves of warmth, for the same reason.
        int   slots{ 0 };
        float resist{ 0.0f };  // percent, as the actor value reads it

        // Sampled fresh; nothing here is cached, because in C++ the whole
        // sample is cheaper than the StorageUtil round trip the old cache
        // needed to avoid.
        static Climate Sample();

        // The level the cold axis is heading for in this situation. NOT
        // clamped: a target of -0.9 and a target of 0.0 are both "you will
        // freeze", but the first crosses zero far sooner, and that difference
        // is what makes the target table's times come out. The clamp belongs on
        // the axis, and lives in Needs::Advance.
        [[nodiscard]] float ColdTarget() const;

        // How fast the axis moves towards that target, in bar per game hour,
        // unsigned. See Climate.cpp: the speed is set by the SITUATION rather
        // than by the distance left to travel.
        [[nodiscard]] float ColdSpeed(float a_current) const;

        // The falling half on its own, without the warming branch in front of
        // it. Bought time is spent at this speed whichever way the bar is
        // travelling, so it has to ask for the chill specifically - and asking
        // through here is what stops the two drifting apart, which is what
        // they did while the formula was written out twice.
        [[nodiscard]] float ChillSpeed(float a_current) const;

        // The net load this situation puts on the player, unclamped. Positive
        // means losing heat. This is the analogue of v0.4.0's
        // Severity - Mitigation, and it is what the temperature indicator
        // reads: it describes the surroundings rather than the axis, so the
        // icon can say "it is bitter out here" while the bar is still full.
        [[nodiscard]] float ColdLoad() const;

        // Warmth as it actually insulates right now: what is worn, less
        // what being soaked takes off it. Both the load and the chilling
        // speed are built on this, and they must be built on the same
        // number - so it is written once, here.
        [[nodiscard]] float DryWarmth() const;

        // What warmth divides the chilling speed by, never below 0.1. Public
        // because the log prints it; the cold buffer asks ChillSpeed for the
        // whole figure rather than rebuilding it out of this.
        [[nodiscard]] float ChillSlow() const;

        // 0 freezing .. 4 warm, on v0.4.0's five-step scale.
        [[nodiscard]] int Feel() const;

        // The nearest world fire in range, or nothing. Separate from the
        // nearFire flag because the warm-hands idle needs to know WHERE the
        // fire is - v0.4.0 only played it if the player was already facing one.
        [[nodiscard]] static RE::TESObjectREFR* NearestFire(float a_radius);

        // Is rain falling on the player right now - outdoors, with nothing
        // overhead? The same two questions the wetness term asks, exposed
        // because lighting a fire needs the same answer and must not ask it a
        // second way.
        [[nodiscard]] static bool RainOnPlayer();
    };
}
