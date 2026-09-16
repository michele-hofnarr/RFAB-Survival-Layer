#pragma once

#include "Menu/BarWidget.h"

// The mod's own HUD, as a Scaleform menu the game owns rather than a SkyUI
// widget.
//
// Why not a SkyUI widget any more: SkyUI positions widgets against the physical
// edge of the screen, so the same X/Y lands somewhere else at 21:9 than it does
// at 16:9. The game loads menu movies with ScaleModeType::kShowAll, which scales
// the authored 1280x720 stage uniformly and *centres* it, without clipping what
// falls outside. So there are two coordinate bases available, and only one of
// them is stable:
//
//   GetMovieDef()->GetWidth()/GetHeight()  -> always 1280x720, any resolution
//   GetVisibleFrameRect()                  -> the real viewport; `left` goes
//                                             negative on ultrawide
//
// Everything anchored to the screen is placed against the movie-def size (see
// StageWidth/StageHeight below). The visible frame rect is only correct for
// projecting a world position onto the screen. RFAB's own widgets sit near the
// centre of that fixed 1280x720 stage, which is why they do not drift.

namespace RSL
{
    class RSLMenu : public RE::IMenu
    {
    public:
        static constexpr std::string_view MENU_NAME = "RSL_SurvivalHUD"sv;
        static constexpr std::string_view FILE_NAME = "RSL_SurvivalHUD"sv;

        RSLMenu();

        static void Register();
        static RE::stl::owner<RE::IMenu*> Creator() { return new RSLMenu(); }

        // Opens the menu if it is not already up. Cheap enough to call often;
        // any traffic towards the HUD goes through here first so a menu that
        // somehow closed comes back.
        static void Open();
        static void Close();

        // Keeps the menu reopened after something hides it. See the comment
        // on the heartbeat in the cpp for why this is needed at all.
        static void StartHeartbeat();

        // IMenu
        void                   PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;
        void                   AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override;

        // Queue work to run on the UI thread at the top of the next frame.
        // Scaleform is not thread safe and GFxValue must never be touched from
        // a game or event thread - this is the only sanctioned way across.
        static void AddTask(std::function<void(RSLMenu&)> a_task);

        // The stable 1280x720 basis described above. Use these, not the visible
        // frame rect, for anything anchored to the screen.
        [[nodiscard]] float StageWidth() const;
        [[nodiscard]] float StageHeight() const;

    private:
        void DrainTasks();
        void Simulate(float a_deltaSeconds);

        // Everything that changes game state. Posted to the game thread from
        // Simulate; never called on the render thread.
        void RunGameplay();
        void EnsureWidget();

        // The inventory food preview. Pushed from the UI thread, because the
        // selected item is read out of another menu's movie and Scaleform is
        // touched from nowhere else.
        void PushInvPreview();

        BarWidget     _bars;
        bool          _widgetTried{ false };
        std::uint64_t _frames{ 0 };

        // True once the off-edge teardown has run, so it runs exactly once.
        bool _toreDown{ false };

        // True while a gameplay pass is queued or running, so frames cannot
        // stack them up on a slow game thread.
        std::atomic<bool> _simQueued{ false };

        // Real time, not game time: this is the frame clock, and it keeps
        // running while the game is paused. Gameplay that must respect pause or
        // timescale reads the Calendar instead.
        std::chrono::steady_clock::time_point _lastFrame{ std::chrono::steady_clock::now() };

        // Needs are simulated on a fixed step fed by an accumulator rather than
        // once per frame, so the model does not change behaviour with framerate.
        float _accumulator{ 0.0f };

        static inline std::mutex                                _taskLock;
        static inline std::queue<std::function<void(RSLMenu&)>> _tasks;
    };
}
