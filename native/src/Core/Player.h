#pragma once

// The player, and the two or three readings of him that half this mod asks for.
//
// Written out twelve times before this file existed. Every module that wanted
// the singleton carried its own three-line Player(), and five files carried a
// DiseaseResist() identical to the character - four illnesses and the hit sink
// in Events. None of them was wrong; there were simply five of them, which is
// four places for the next change to miss.

namespace RSL
{
    [[nodiscard]] RE::PlayerCharacter* Player();

    // An actor value off the player as it stands now, fortifications and this
    // mod's own penalties included. Zero when there is no player yet, which is
    // the right answer at every call site here - nothing is ill before the
    // game has one.
    [[nodiscard]] float PlayerAV(RE::ActorValue a_value);

    // The same, but the value the character sheet shows rather than the one
    // the moment has: what a permanent pool is, before anything temporary.
    [[nodiscard]] float PlayerBaseAV(RE::ActorValue a_value);

    // Disease resistance, as a PERCENTAGE the way the game states it: 25 means
    // a quarter off. Every caller scales by (1 - resist * 0.01), so the unit
    // stays the game's rather than becoming a fraction somewhere and a
    // percentage somewhere else.
    [[nodiscard]] float DiseaseResist();
}
