#pragma once

// Corner notifications that actually arrive.
//
// The engine's DebugNotification drops a message whenever one is already
// queued - that is what its third argument means, and it defaults to true - and
// anything sent while the HUD is not up is lost outright. State changes in this
// mod tend to come in bursts, several within a second, which is precisely the
// case that gets swallowed. v0.4.0 had the same problem through Papyrus's
// Message.Show, and the player noticed it there too.
//
// So messages are queued here and released one at a time, spaced far enough
// apart to be read, and only while the HUD is actually on screen.

namespace RSL
{
    class Notify
    {
    public:
        static Notify& GetSingleton();

        // Queue a message. Safe from any thread.
        void Push(std::string a_text);
        void Push(RE::BGSMessage* a_message);

        // Release at most one queued message. Call from the game thread.
        void Drain();

        void Clear();

    private:
        // Long enough that two in a row are both read, short enough that a
        // burst does not feel delayed.
        // Nothing paces the queue any more - the HUD stacks four at a time on
        // its own. What remains is the hold below, which is not pacing: a
        // message sent while the HUD is down is not shown late, it is never
        // shown at all.

        std::mutex                            _lock;
        std::deque<std::string>               _queue;
    };
}
