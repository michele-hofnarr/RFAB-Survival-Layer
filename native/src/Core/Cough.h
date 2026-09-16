#pragma once

// The sick sound: a cough from the player, now and then, while an illness runs.
//
// WHY IT EXISTS. Every illness in this mod announces itself once, in a corner
// message, and is then silent for as long as it lasts - hours of it. This is
// the one thing that keeps saying so, and it says it in the character's own
// voice rather than in text.
//
// WHICH ILLNESSES. All of them except the two that are not illnesses: not
// hypothermia, which is a state of the body, and not the tissue stress, which
// is a wound. Neither makes anyone cough.
//
// HOW OFTEN. Once a game hour at stage 1, twice at stage 2, four times at
// stage 3 - taken from the WORST illness the player is carrying, not summed
// over them, so catching a second cold does not double the noise.
//
// NOT IN COMBAT. A cough is a quiet-moment sound; in a fight it would land
// under the swings and read as a bug.

namespace RSL
{
    class Cough
    {
    public:
        static Cough& GetSingleton();

        // a_gameHours is the span this tick covers, as everywhere else.
        void Update(float a_gameHours, bool a_undead);

    private:
        // Stops anything still playing and forgets it. A load, or the switch.
        void Silence();

        RE::BSSoundHandle _handle{};
        bool              _playing{ false };
    };
}
