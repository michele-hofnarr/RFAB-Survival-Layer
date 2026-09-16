#include "PCH.h"

#include "Menu/RSLMenu.h"

#include "Menu/Draw.h"
#include "Core/ColdScreen.h"
#include "Core/ColdVisual.h"
#include "Core/Cough.h"
#include "Core/CommonCold.h"
#include "Core/Disease.h"
#include "Core/Hypothermia.h"
#include "Core/Needs.h"
#include "Core/Notify.h"
#include "Core/Penalties.h"
#include "Core/ElemLesion.h"
#include "Core/RfabDisease.h"
#include "Core/Bedroll.h"
#include "Core/Campfire.h"
#include "Core/WarmAnim.h"
#include "Core/Trees.h"
#include "Core/StagedDisease.h"
#include "Core/TakeDown.h"
#include "Core/Teardown.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // Needs advance on a fixed step so the model does not depend on how
        // fast the machine renders. 1/30 s is far below anything the survival
        // axes can notice and cheap enough to run every frame's worth.
        constexpr float SIM_STEP = 1.0f / 30.0f;
        constexpr float SIM_STEP_SECONDS = SIM_STEP;

        // If the game hitches, do not try to catch up on the whole gap in one
        // frame - that is how a stall turns into a spiral of doom.
        constexpr int MAX_STEPS_PER_FRAME = 4;

        // Two jobs, both of them "a menu did something and we care".
        //
        // Our HUD should be visible exactly when the vanilla HUD is, so it
        // follows HUDMenu open/close rather than deciding on its own.
        //
        // And the settings the player just edited are written by MCM Helper
        // when the journal closes, so that is the moment to read them back.
        // v0.4.0 had OnConfigClose in _RSL_MCM.psc for this; a menu event is
        // the same moment without a Papyrus native binding to keep alive.
        class MenuWatch : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static MenuWatch* GetSingleton()
            {
                static MenuWatch singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::MenuOpenCloseEvent*                a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (a_event->menuName == RE::HUDMenu::MENU_NAME) {
                    // Only ever reopen. Mirroring the close as well is what
                    // TrueHUD does, but it means every loading screen tears our
                    // menu down, and a kAlwaysOpen menu has no reason to be
                    // taken down in the first place.
                    if (a_event->opening) {
                        RSLMenu::Open();
                    }
                } else if (a_event->menuName == RE::JournalMenu::MENU_NAME &&
                           !a_event->opening) {
                    Settings::ReadSettings();
                }

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            MenuWatch() = default;
        };
    }

    namespace
    {
        std::atomic<bool> g_heartbeat{ false };

        // Reopen the HUD whenever the vanilla HUD is up and ours is not.
        //
        // A kAlwaysOpen menu still gets hidden when something asks it to, and
        // RFAB's new-game sequence hides the HUD elements outright. Nothing
        // then brings ours back, because a hidden menu stops receiving
        // AdvanceMovie and so cannot fix itself - which is why the widget only
        // appeared after a save reload.
        //
        // Gating on HUDMenu keeps intent intact: when the game deliberately
        // hides the HUD, ours stays down with it.
        void ReopenIfHidden()
        {
            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->IsMenuOpen(RE::HUDMenu::MENU_NAME) &&
                !ui->IsMenuOpen(RSLMenu::MENU_NAME)) {
                RSLMenu::Open();
            }
        }

        // The beat comes from a sleeping thread, not from the task itself.
        //
        // The first attempt had the UI task re-add itself, on the assumption
        // that a task queued during a drain would run on the next frame. It
        // does not: SKSE drains the queue until it is empty, so a task that
        // requeues itself is drained forever and the frame never ends. That
        // hung the game on the main menu, before any of this had anything to
        // do. One task per beat, posted from outside the drain, cannot.
        void HeartbeatThread()
        {
            using namespace std::chrono_literals;
            while (g_heartbeat) {
                std::this_thread::sleep_for(500ms);
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddUITask(ReopenIfHidden);
                }
            }
        }
    }

    void RSLMenu::StartHeartbeat()
    {
        if (g_heartbeat.exchange(true)) {
            return;
        }
        std::thread(HeartbeatThread).detach();
        logger::info("menu heartbeat started");
    }

    RSLMenu::RSLMenu()
    {
        auto* scaleform = RE::BSScaleformManager::GetSingleton();
        if (!scaleform) {
            logger::error("no BSScaleformManager - the HUD cannot be created");
            return;
        }

        // kShowAll is the game's own default and the reason the movie-def size
        // stays a stable 1280x720 at any resolution. Do not change it without
        // re-reading the note in RSLMenu.h.
        // LoadMovie, not LoadMovieEx. The Ex variant is a CommonLib
        // reimplementation: it skips the SKSE Scaleform hooks and never builds
        // the FxDelegate that the real routine installs. Since this is the path
        // every vanilla menu takes, it is also the one most likely to behave.
        //
        // No GFxLog state is installed either. The movie carries no
        // ActionScript, so there is nothing to trace, and handing SetState a
        // pointer owned by a temporary GPtr is a use-after-free waiting to
        // happen.
        const bool loaded = scaleform->LoadMovie(
            this, uiMovie, FILE_NAME.data(),
            RE::GFxMovieView::ScaleModeType::kShowAll, 0.0F);
        if (!loaded || !uiMovie) {
            logger::error("failed to load Interface/{}.swf - the HUD will not draw", FILE_NAME);
            return;
        }
        logger::info("movie loaded, stage {}x{}", StageWidth(), StageHeight());

        depthPriority = 0;

        // kAlwaysOpen: the UI stack never closes it on its own.
        // kRequiresUpdate: this is what makes the engine call AdvanceMovie every
        //                  frame. Without it the menu simply never ticks, and it
        //                  is the whole reason we get a frame-rate tick without
        //                  hooking anything.
        // kAllowSaving: a HUD must not block saving.
        menuFlags.set(
            RE::UI_MENU_FLAGS::kAlwaysOpen,
            RE::UI_MENU_FLAGS::kRequiresUpdate,
            RE::UI_MENU_FLAGS::kAllowSaving);

        // Claim no input context, so the menu never enters ControlMap's context
        // priority stack and never steals a key from the game.
        inputContext = Context::kNone;

        if (uiMovie) {
            uiMovie->SetMouseCursorCount(0);
        }
    }

    void RSLMenu::Register()
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("no UI singleton - HUD not registered");
            return;
        }
        ui->Register(MENU_NAME, Creator);
        ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatch::GetSingleton());
        logger::info("HUD menu registered as {}", MENU_NAME);
    }

    void RSLMenu::Open()
    {
        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (!ui || !queue || ui->IsMenuOpen(MENU_NAME)) {
            return;
        }
        queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    void RSLMenu::Close()
    {
        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (!ui || !queue || !ui->IsMenuOpen(MENU_NAME)) {
            return;
        }
        queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }

    void RSLMenu::PostCreate()
    {
        logger::info("HUD movie created, stage {}x{}", StageWidth(), StageHeight());
        EnsureWidget();
    }

    void RSLMenu::EnsureWidget()
    {
        // The geometry is being dialled in from the MCM, and the clip tree is
        // built against it. Dropping the tree here is what makes a slider move
        // show up without a reload.
        if (_bars.GeometryStale()) {
            _bars.Destroy();
        }

        if (_bars.Ready() || !uiMovie) {
            return;
        }

        // PostCreate can land before the movie has a usable root, so this is
        // retried from the first frames rather than assumed to work once.
        RE::GFxValue root;
        if (!uiMovie->GetVariable(&root, "_root") || !root.IsDisplayObject()) {
            return;
        }

        if (!_bars.Build(uiMovie.get(), root)) {
            if (!_widgetTried) {
                logger::error("bar widget could not be built");
                _widgetTried = true;
            }
            return;
        }

        // Placement is in pixels of the authored 1280x720 stage, never of the
        // real viewport - see the note at the top of this file.
        _bars.SetPlacement(Settings::fHudX, Settings::fHudY, Settings::fHudScale);
        _bars.SetTempPlacement(
            Settings::fTempIconX, Settings::fTempIconY, Settings::fTempIconScale);
    }

    RE::UI_MESSAGE_RESULTS RSLMenu::ProcessMessage(RE::UIMessage& a_message)
    {
        return RE::IMenu::ProcessMessage(a_message);
    }

    void RSLMenu::AdvanceMovie(float a_interval, std::uint32_t)
    {
        (void)a_interval;
        const auto  now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - _lastFrame).count();
        _lastFrame = now;

        // Whether this runs at all is the first thing worth knowing: no
        // AdvanceMovie means the menu is registered but not ticking, and
        // nothing about the drawing matters until that is true.
        ++_frames;

        EnsureWidget();
        DrainTasks();

        _accumulator += dt;
        for (int i = 0; i < MAX_STEPS_PER_FRAME && _accumulator >= SIM_STEP; ++i) {
            Simulate(SIM_STEP);
            _accumulator -= SIM_STEP;
        }
        if (_accumulator > SIM_STEP) {
            _accumulator = SIM_STEP;  // drop the backlog rather than chase it
        }

        _bars.Advance(dt);
        _bars.Redraw();

        if (uiMovie) {
            uiMovie->Advance(dt);
        }
    }

    void RSLMenu::AddTask(std::function<void(RSLMenu&)> a_task)
    {
        std::scoped_lock lock(_taskLock);
        _tasks.push(std::move(a_task));
    }

    void RSLMenu::DrainTasks()
    {
        // Hold the lock only long enough to take the queue: a task is free to
        // enqueue another one, and running under the lock would deadlock.
        std::queue<std::function<void(RSLMenu&)>> pending;
        {
            std::scoped_lock lock(_taskLock);
            pending.swap(_tasks);
        }
        while (!pending.empty()) {
            pending.front()(*this);
            pending.pop();
        }
    }

    void RSLMenu::Simulate(float)
    {
        // Gameplay runs on the game thread, not here.
        //
        // AdvanceMovie is the render thread. Reading game state from it is
        // fine and is what the HUD is for, but changing it is not: adding an
        // ability from here put the spell in the player's list - HasSpell
        // agreed - while no active effect was ever created, so the
        // hypothermia stages applied invisibly. The magic system expects to be
        // driven from the game thread.
        //
        // One task per frame, and never a second one while the first is
        // outstanding, so a slow frame cannot pile them up.
        if (!_simQueued.exchange(true)) {
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([this]() {
                    RunGameplay();
                    _simQueued.store(false);
                });
            } else {
                _simQueued.store(false);
            }
        }

        // The food preview, before the cover check below: it is the one piece
        // of the widget that is shown only while a menu owns the screen.
        PushInvPreview();

        // Off screen while a menu owns the screen.
        //
        // Ours is its own always-open menu, so nothing hides it the way the
        // vanilla HUD is hidden - it sat on top of the inventory and the map.
        // GameIsPaused covers every menu that stops the world, which is those
        // two and the rest of them; the crafting menus do not pause and hide
        // the HUD anyway, so they are named.
        if (auto* ui = RE::UI::GetSingleton()) {
            // GameIsPaused alone was not enough: the map went away but the
            // inventory did not, because an item menu does not pause the world
            // the way the map does. The engine has its own notion of each of
            // these, which is better than a hand-kept list of menu names that
            // will fall behind.
            const bool covered =
                !ui->IsShowingMenus() ||          // the HUD is off entirely
                ui->GameIsPaused() ||             // map, journal, the Esc menu
                ui->IsItemMenuOpen() ||           // inventory, container, barter
                ui->IsApplicationMenuOpen() ||
                ui->IsModalMenuOpen() ||
                // The Tab menu - skills, map, items, magic. It does not pause
                // and it is not an item menu, so it needs naming.
                ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
                ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) ||
                ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ||
                !ui->IsMenuOpen(RE::HUDMenu::MENU_NAME);
            _bars.SetVisible(!covered);
            if (covered) {
                return;
            }
        }

        // Placement is re-applied every pass, not only when the widget is
        // built. The MCM writes these while the game runs, and applying them
        // once at build time meant moving a slider did nothing until a reload.
        // Setting a clip's position is cheap; not noticing the setting is not.
        _bars.SetPlacement(Settings::fHudX, Settings::fHudY, Settings::fHudScale);
        _bars.SetTempPlacement(
            Settings::fTempIconX, Settings::fTempIconY, Settings::fTempIconScale);

        // Display only. These read what the last gameplay pass left behind.
        auto& needs = Needs::GetSingleton();

        if (!Settings::bModEnabled) {
            for (int i = 0; i < Layout::ROWS; ++i) {
                _bars.SetShown(i, false);
            }
            // The bars go by their own flags; the indicator is a separate clip
            // with its own visibility, and setting it to a neutral step left it
            // sitting on screen with the mod switched off.
            _bars.SetFeelShown(false);
            return;
        }
        _bars.SetFeelShown(true);

        _bars.SetValue(0, needs.Sleep());
        _bars.SetValue(1, needs.Hunger());
        _bars.SetValue(2, needs.Cold());
        _bars.SetFast(2, needs.ColdTemp());
        _bars.SetFast(1, needs.HungerFast());
        _bars.SetFast(0, needs.SleepTemp());

        _bars.SetSafe(0, Settings::fSleepSafe);
        _bars.SetSafe(1, Settings::fHungerSafe);
        _bars.SetSafe(2, Settings::fColdSafe);

        // The undead do not sleep or eat, so those two bars go away entirely
        // rather than sitting there full.
        const bool eats = !needs.Undead();
        _bars.SetShown(0, eats);
        _bars.SetShown(1, eats);
        _bars.SetShown(2, true);

        // What this pass decided, at the point it decided it. The flag it reads
        // is written on the game thread, and the value seen here has disagreed
        // with the value seen a few microseconds later on this one - so it is
        // reported from here rather than inferred from its effect. `ran` is how
        // many passes reached this line since the last report: a small number
        // means the cover check is turning the pass away.
        if (Settings::bDebugLog) {
            static auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
            static int  ran = 0;
            ++ran;
            const auto now = std::chrono::steady_clock::now();
            if (now - last >= std::chrono::seconds(5)) {
                last = now;
                logger::info("display: undead={} -> sleep/hunger shown={} | "
                            "{} passes reached here",
                    needs.Undead(), eats, ran);
                ran = 0;
            }
        }

        // The indicator reads the surroundings, not the axis, so it can warn
        // that it is bitter out here while the bar is still full.
        _bars.SetFeel(needs.LastClimate().Feel());
    }

    void RSLMenu::RunGameplay()
    {
        // Before anything, and whether or not the mod is running: this is what
        // finishes a take-down. A camp half taken down - disabled, never marked
        // - is a bedroll standing in the world that nothing can reach.
        TakeDown::Watch();

        // Switching the mod off has to strip everything it applied, once, on
        // the edge. v0.4.0 calls leaving penalties on the pools the most likely
        // bug report there is, and it is right: nothing else would ever take
        // them off again.
        if (!Settings::bModEnabled) {
            if (!_toreDown) {
                Penalties::GetSingleton().ClearAll();
                Hypothermia::GetSingleton().ClearAll();
                ColdVisual::GetSingleton().ClearAll();
                ColdScreen::GetSingleton().ClearAll();
                CommonCold::GetSingleton().ClearAll();

                // EVERY ILLNESS, not just the common cold. Each of these clears
                // itself at the top of its own Update when the mod is off - but
                // this branch returns before any of them is called, so their
                // stage spells sat on the player with the mod switched off.
                // Twelve of them, at worst.
                for (const auto& dz : Forms::hitDisease) {
                    StagedDisease::Clear(dz);
                }
                RfabDisease::GetSingleton().ClearAll();
                ElemLesion::GetSingleton().ClearAll();

                // The hidden disease marker is a spell on the player like any
                // other, and switching the mod off has to take it with
                // everything else - otherwise strangers go on remarking on an
                // illness this mod is no longer running.
                Disease::GetSingleton().SyncMarker();

                // AND THE CAMP, which is not on the player but standing in the
                // world - the only part of the mod that is. Nothing else would
                // ever come back for it: Bedroll::Update, which puts a camp
                // back in the pack, and Campfire::Update, which burns a fire
                // out and takes the power away with the perk, both sit below
                // this return and stop running the moment the switch goes off.
                // So a bedroll stayed pitched, a fire burned for ever, and the
                // lesser power sat in the magic menu doing nothing - Light()
                // refuses while the mod is off.
                Bedroll::GetSingleton().Update();
                Campfire::GetSingleton().Extinguish();
                Campfire::GetSingleton().Update();

                // LAST, and by origin rather than by name. Everything
                // above works from a list of records this build knows; this
                // takes off anything of ours the list could not name - a
                // record from an older build whose FormID has since moved, or
                // one a bug left applied. See Core/Teardown.h for why it is
                // the difference between a save that survives the uninstall
                // and one that does not.
                Teardown::StripEverythingOfOurs();

                _toreDown = true;
                Notify::GetSingleton().Clear();
                logger::info("mod switched off - everything removed");
            }
            return;
        }
        _toreDown = false;

        // Nothing runs while the player has no 3D.
        //
        // Fast travel turned out to be a reliable way to crash this: during the
        // transition the player reference exists but has no model, and the
        // engine calls that reach into it - applying an effect shader, walking
        // the references in range - are not prepared for that. The pass is
        // simply skipped; game time keeps accruing and is replayed by the next
        // one, so nothing is lost by waiting.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) {
            return;
        }

        // Nor while a load is still on screen. A fast travel reaches the point
        // where the player has a model well before the actor is ready to be
        // given spells or shaders - they are accepted and quietly do nothing -
        // and the loading menu is the engine's own statement that it is not
        // finished.
        if (auto* ui = RE::UI::GetSingleton();
            ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
            return;
        }

        // Step markers, off unless a crash needs locating. They found the fast
        // travel fault in one run, and then buried the log: this pass runs
        // every frame, so seven lines a pass is nineteen thousand lines in two
        // minutes, which drowns everything worth reading.
        const auto step = [](const char* a_what) {
            if (Settings::bTracePass) {
                logger::info("pass: {}", a_what);
            }
        };

        auto& needs = Needs::GetSingleton();
        step("needs");
        needs.Update();

        step("penalties");
        Penalties::GetSingleton().Update(
            needs.Sleep(), needs.Hunger(), needs.ColdBase(), needs.Undead());

        // Hypothermia is driven in real seconds as well as game hours: its
        // stage-3 drain is a wall-clock bleed, not a per-hour one.
        step("hypothermia");
        Hypothermia::GetSingleton().Update(
            needs.ColdBase(), needs.HoursThisTick(), SIM_STEP_SECONDS, needs.Undead(),
            needs.LastClimate().ChillSlow());

        // EVERY ILLNESS READS THE EARNED HALVES, not the bars.
        //
        // Sleep and hunger are each one bar made of two, and the top half is
        // the part that does not last - a nap in a bedroll, a pocketful of
        // apples. It keeps the bar up, and the player can act on it, so the
        // penalties and the widget see the sum. An illness should not: a body
        // running on naps and apples is not mending, and reading the sum let P
        // heal on exactly that. Cold is already the reserve alone, for the same
        // reason - a bought buffer does not thaw anything.
        step("cold disease");
        CommonCold::GetSingleton().Update(needs.SleepBase(), needs.HungerBase(),
            needs.ColdBase(), needs.HoursThisTick(), needs.Undead());

        // The four caught by a hit or a bad meal. Catching them happens on the
        // event; all this does is carry them forward once they are there.
        step("hit diseases");
        for (const auto& dz : Forms::hitDisease) {
            StagedDisease::Advance(dz, needs.SleepBase(), needs.HungerBase(),
                needs.ColdBase(), needs.HoursThisTick(), needs.Undead());
        }

        step("rfab diseases");
        RfabDisease::GetSingleton().Update(needs.SleepBase(), needs.HungerBase(),
            needs.ColdBase(), needs.HoursThisTick(), needs.Undead());

        step("elemental lesions");
        ElemLesion::GetSingleton().Update(needs.SleepBase(), needs.HungerBase(),
            needs.ColdBase(), needs.HoursThisTick(), needs.Undead());

        step("bedroll");
        Bedroll::GetSingleton().Update();

        step("campfire");
        Campfire::GetSingleton().Update();

        step("warm anim");
        WarmAnim::GetSingleton().Update();
        Trees::GetSingleton().Update();

        step("cold visual");
        ColdVisual::GetSingleton().Update(needs.ColdBase());
        ColdScreen::GetSingleton().Update(needs.ColdBase());

        // One queued message at a time, spaced so each is readable.
        // The hidden marker, after every illness has had its say this pass.
        step("cough");
        Cough::GetSingleton().Update(needs.HoursThisTick(), needs.Undead());

        step("disease marker");
        Disease::GetSingleton().SyncMarker();

        step("notify");
        Notify::GetSingleton().Drain();
        step("done");
    }

    namespace
    {
        // What the player has highlighted in the inventory, if it is a potion
        // or food. The path is SkyUI's item list, the same one v0.4.0 read
        // through UI.GetInt - the native route to it is the menu's own movie.
        //
        // UI thread only. GetVariable reaches into another menu's Scaleform
        // state, and nothing but the render thread may do that.
        [[nodiscard]] RE::AlchemyItem* SelectedFood()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return nullptr;
            }
            auto menu = ui->GetMenu<RE::InventoryMenu>();
            if (!menu || !menu->uiMovie) {
                return nullptr;
            }

            RE::GFxValue id;
            if (!menu->uiMovie->GetVariable(
                    &id, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId") ||
                !id.IsNumber()) {
                return nullptr;
            }

            const auto formID = static_cast<RE::FormID>(id.GetNumber());
            auto*      form = formID ? RE::TESForm::LookupByID(formID) : nullptr;
            return form ? form->As<RE::AlchemyItem>() : nullptr;
        }
    }

    void RSLMenu::PushInvPreview()
    {
        auto* ui = RE::UI::GetSingleton();
        auto& needs = Needs::GetSingleton();

        // The inventory only, as in v0.4.0. The same read works for the
        // container, barter and gift lists - they share the list - but a
        // preview of what eating would do belongs where eating happens.
        const bool open = ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) &&
                          Settings::bModEnabled;

        // The projection shares its arithmetic with the eating path, so the
        // number cannot drift - with one deliberate exception, spelled out on
        // Needs::HungerPreview. A negative one means the highlighted thing is
        // not food: a drink, a sword, nothing at all.
        auto*       selected = open ? SelectedFood() : nullptr;
        const float projected =
            (open && !needs.Undead()) ? needs.HungerPreview(selected) : -1.0f;

        // The cold bar answers a different question about the same item: how
        // much time its frost resistance buys. A draught is not food and a meal
        // is not warmth, so the two bars appear and vanish independently - and a
        // RFAB dish that does both shows both.
        //
        // The undead gate is on the food bar alone. A vampire neither sleeps
        // nor eats and those two bars are hidden for them, but cold still
        // applies - so a draught still buys them time, and still says so.
        const float gift = open ? Needs::ColdGift(selected) : 0.0f;
        const float coldProjected =
            gift > 0.0f ? std::min(1.0f, needs.Cold() + gift) : -1.0f;

        // ...and that is also what decides whether each bar is there. They
        // appear when the highlighted item would move them and go away again
        // when it would not, rather than sitting over the whole inventory
        // screen saying nothing.
        _bars.SetInvShown(BarWidget::INV_FOOD, projected >= 0.0f);
        _bars.SetInvShown(BarWidget::INV_COLD, coldProjected >= 0.0f);
        if (projected < 0.0f && coldProjected < 0.0f) {
            return;
        }

        _bars.SetInvPlacement(Layout::INV_X, Layout::INV_Y, Layout::INV_SCALE);

        // Whether the promise is a meal or fast food decides how it is drawn,
        // and it comes from the same Assess the eating path uses.
        if (projected >= 0.0f) {
            _bars.SetInvValue(BarWidget::INV_FOOD, needs.Hunger(), Settings::fHungerSafe,
                projected, needs.HungerFast(), !Needs::Assess(selected).special);
        }

        // Bought time is ALWAYS hatched: unlike food, there is no kind of it
        // that lasts. What it promises sits on top of the buffer already there,
        // which is why the hatch covers both.
        if (coldProjected >= 0.0f) {
            _bars.SetInvValue(BarWidget::INV_COLD, needs.Cold(), Settings::fColdSafe,
                coldProjected, needs.ColdTemp(), true);
        }
    }

    float RSLMenu::StageWidth() const
    {
        const auto* def = uiMovie ? uiMovie->GetMovieDef() : nullptr;
        return def ? def->GetWidth() : 1280.0f;
    }

    float RSLMenu::StageHeight() const
    {
        const auto* def = uiMovie ? uiMovie->GetMovieDef() : nullptr;
        return def ? def->GetHeight() : 720.0f;
    }
}
