#include "PCH.h"

#include "Core/WarmAnim.h"

#include "Core/Climate.h"
#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // How far round the player may be turned from the fire and still be
        // "facing" it. v0.4.0's number.
        constexpr float FACING_DEGREES = 45.0f;

        // How far the player may drift from where they stopped and still count
        // as standing still.
        constexpr float STILL_TOLERANCE = 8.0f;

        constexpr float PI = 3.14159265358979323846f;

        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        // Actor.PlayIdle is not bound in CommonLibSSE, so it goes through
        // Papyrus - the same boundary EffectShader.Play and Game.SetInChargen
        // use. One argument, checked against Actor.psc rather than assumed.
        void PlayIdle(RE::TESIdleForm* a_idle)
        {
            auto* vm = RE::SkyrimVM::GetSingleton();
            auto* player = Player();
            if (!vm || !vm->impl || !player || !a_idle) {
                return;
            }

            auto  impl = vm->impl;
            auto* policy = impl->GetObjectHandlePolicy();
            if (!policy) {
                return;
            }

            const auto handle = policy->GetHandleForObject(
                static_cast<RE::VMTypeID>(RE::PlayerCharacter::FORMTYPE), player);
            if (handle == policy->EmptyHandle()) {
                return;
            }

            auto* args = RE::MakeFunctionArguments(std::move(a_idle));
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            impl->DispatchMethodCall2(handle, "Actor"sv, "PlayIdle"sv, args, callback);
            delete args;
        }

        // The crafting and cooking menus, and nothing else. Opening the
        // inventory beside a fire used to cancel this for no reason.
        //
        // Sleeping and waiting are in here too, but as a menu rather than as a
        // flag: the native core has no "currently asleep" state because sleep
        // arrives as an event after the fact.
        [[nodiscard]] bool BlockingMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && (ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) ||
                          ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME));
        }
    }

    WarmAnim& WarmAnim::GetSingleton()
    {
        static WarmAnim singleton;
        return singleton;
    }

    void WarmAnim::Cancel()
    {
        if (!_playing) {
            return;
        }
        _playing = false;
        if (auto* player = Player()) {
            // What Debug.SendAnimationEvent does, without the round trip.
            player->NotifyAnimationGraph("IdleForceDefaultState"sv);
        }
    }

    bool WarmAnim::PlayerIsBusy()
    {
        auto* player = Player();
        if (!player) {
            return true;
        }

        // Intent first, because movement cannot be observed.
        //
        // The idle locks the player where they stand: pressing W while it plays
        // moves nothing at all, so every attempt to detect movement after the
        // fact was doomed. Measuring drift between frames failed, and so did
        // actorState1's movement flags. The input sink reports the key press
        // itself, and that is what cancels.
        if (_intent.exchange(false)) {
            return true;
        }

        // Drift is still measured, as a backstop for movement the player did
        // not ask for - being shoved, carried, or moved by a script. Against a
        // fixed anchor rather than the previous frame, so it accumulates.
        const auto pos = player->GetPosition();
        if (!_anchored) {
            _anchorX = pos.x;
            _anchorY = pos.y;
            _anchored = true;
        }
        const float dx = pos.x - _anchorX;
        const float dy = pos.y - _anchorY;
        const bool  moved = std::sqrt(dx * dx + dy * dy) > STILL_TOLERANCE;
        if (moved) {
            _anchorX = pos.x;
            _anchorY = pos.y;
        }

        return moved ||
               BlockingMenuOpen() ||
               player->IsInCombat() ||
               player->AsActorState()->IsWeaponDrawn() ||
               player->AsActorState()->IsSneaking() ||
               player->AsActorState()->IsSwimming() ||
               player->AsActorState()->IsSprinting();
    }

    void WarmAnim::Update()
    {
        if (!Settings::bModEnabled || !Settings::bWarmAnim || !Forms::idleWarm) {
            Cancel();
            return;
        }

        if (PlayerIsBusy()) {
            _lastActive = std::chrono::steady_clock::now();
            Cancel();
            return;
        }

        if (_playing) {
            return;
        }

        // The fire is found here rather than taken from the climate sample.
        // That sample counts a torch in hand as a fire, and v0.4.0 is explicit
        // that the idle must not: "Severity() counts the torch; the warm-hands
        // idle does not". It also has its own reach.
        auto* fire = Climate::NearestFire(Settings::fWarmAnimRadius);

        // Facing the fire is part of being ready, not a last check.
        //
        // Putting it after the delay meant the clock ran while the player stood
        // with their back to the fire, so turning round started the idle at
        // once. Turning to face it is what starts the five seconds.
        bool ready = fire != nullptr;
        if (ready) {
            auto*      player = Player();
            const auto from = player->GetPosition();
            const auto at = fire->GetPosition();

            // Heading 0 faces +Y, so the bearing is atan2(dx, dy), not (dy, dx).
            const float bearing = std::atan2(at.x - from.x, at.y - from.y);

            // remainder() puts the difference into [-PI, PI] in one step.
            // It replaces two "take 2PI off until it fits" loops, which had
            // no end if an angle ever came back as infinity - the same shape
            // of fault as the replay loop in Needs::Update.
            const float diff = std::remainder(bearing - player->GetAngleZ(),
                2.0f * PI);
            ready = std::abs(diff) <= FACING_DEGREES * PI / 180.0f;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!ready) {
            _lastActive = now;
            return;
        }

        if (std::chrono::duration<float>(now - _lastActive).count() <
            Settings::fWarmAnimDelay) {
            return;
        }

        PlayIdle(Forms::idleWarm);
        _playing = true;
        logger::info("warming hands by the fire");
    }
}
