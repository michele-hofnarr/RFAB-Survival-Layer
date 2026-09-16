#pragma once

// Warming your hands at a fire.
//
// Cosmetic and best-effort, exactly as v0.4.0 has it: after a few seconds of
// standing still near a heat source, facing it, the player plays the vanilla
// warm-hands idle, and the first sign of activity drops it.
//
// One deliberate change from v0.4.0. It used to cancel on any menu at all,
// which meant opening the inventory by a campfire broke the animation for no
// reason. Only the crafting and cooking menus cancel it now - those move the
// player and take over the camera, so the idle cannot survive them anyway.

namespace RSL
{
    class WarmAnim
    {
    public:
        static WarmAnim& GetSingleton();

        // Finds its own fire. The climate sample's nearFire counts a torch in
        // hand, and v0.4.0 is explicit that the idle must not, so this cannot
        // reuse it.
        void Update();

        // Any input, a hit, sleeping, the mod going off.
        void Cancel();

        // The player asked to move, crouch, jump, sprint or draw. Called from
        // the input sink, off the UI thread.
        //
        // This is the only reliable signal there is. The idle locks the player
        // in place, so position never changes while it plays and no amount of
        // measuring movement can notice the attempt - what has to be caught is
        // the intent, not the result.
        void NoteIntent() { _intent.store(true); }

    private:
        [[nodiscard]] bool PlayerIsBusy();

        bool _playing{ false };

        std::atomic<bool> _intent{ false };

        // Where the player was when they last stopped. Movement is measured
        // against this anchor, not against the previous frame - see the note in
        // PlayerIsBusy.
        float _anchorX{ 0.0f };
        float _anchorY{ 0.0f };
        bool  _anchored{ false };

        std::chrono::steady_clock::time_point _lastActive{};
    };
}
