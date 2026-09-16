#pragma once

// Is there something over the player's head?
//
// A ray straight up from the head. A hit means a roof: an overhang, a bridge,
// a rock shelf, a tree canopy, a tent. This is the question v0.4.0 could only
// answer as "are you in an interior", and answering it properly is what lets
// rain, snow and shelter mean anything - and what would let lighting a fire in
// the open in a downpour fail on its own terms rather than by a special case.
//
// From Papyrus this cannot be asked at all: with no raycast, the only way is
// to fire a projectile upwards and catch its impact with a reporter activator.
// We get the answer directly and far cheaper, which is the whole reason this
// lives on the native side.
//
// It is still not free, so it is not asked every frame: the answer is cached
// and refreshed on a timer, and immediately whenever the cell changes, because
// walking through a doorway is exactly when it is wrong.

namespace RSL
{
    class Shelter
    {
    public:
        static Shelter& GetSingleton();

        // The cached answer, refreshing it if it is stale.
        [[nodiscard]] bool Overhead();

        // Force the next Overhead() to look again - on a cell change, or after
        // something has just been placed over the player.
        void Invalidate();

    private:
        [[nodiscard]] static bool CastUp();

        // How high to look. A shallow rock overhang counts; the sky does not.
        static constexpr float REACH = 1200.0f;

        // Roughly eye level, so the ray starts clear of the player's own body.
        static constexpr float HEAD = 120.0f;

        static constexpr float REFRESH_SECONDS = 2.0f;

        bool                                  _overhead{ false };
        bool                                  _valid{ false };
        std::chrono::steady_clock::time_point _taken{};
        RE::TESObjectCELL*                    _cell{ nullptr };
    };
}
