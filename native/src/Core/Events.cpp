#include "PCH.h"

#include "Core/Events.h"

#include "Core/Elemental.h"
#include "Core/Needs.h"
#include "RE/TESSleepStartEvent.h"
#include "Core/Bedroll.h"
#include "Core/Campfire.h"
#include "Core/Trees.h"
#include "Core/WarmAnim.h"
#include "Core/StagedDisease.h"
#include "Core/TakeDown.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // Sleeping is announced by the game, start and end, and the start event
        // carries the span. This is what Papyrus exposes as
        // OnSleepStart(afSleepStartTime, afDesiredSleepEndTime), and what the
        // Papyrus version of this mod used.
        //
        // Three earlier attempts tried to infer sleep from the Sleep/Wait menu
        // instead, and all three failed for the same underlying reason: the
        // player never changes state when sleeping in Skyrim. There is no pose,
        // no lying animation, and the bed's furniture is never occupied - the
        // screen simply fades. So the sit/sleep state, the occupied furniture
        // and the timing of the menu say nothing, and no refinement of them was
        // ever going to.
        class SleepSink :
            public RE::BSTEventSink<RE::TESSleepStartEvent>,
            public RE::BSTEventSink<RE::TESSleepStopEvent>
        {
        public:
            static SleepSink* GetSingleton()
            {
                static SleepSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESSleepStartEvent*                a_event,
                RE::BSTEventSource<RE::TESSleepStartEvent>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                _startedAt = a_event->sleepStartTime;

                // From here the clock is not the tick's to charge: it runs on
                // through the fade, and the whole span is settled at the stop
                // event instead.
                Needs::GetSingleton().SetSleeping(true);

                // The struct layout is inferred from the Papyrus signature
                // rather than declared by CommonLibSSE, so the numbers get
                // logged: game time is a small count of days, and anything else
                // means the guess is wrong.
                logger::info("sleep started: {:.4f} -> {:.4f} (days)",
                    a_event->sleepStartTime, a_event->desiredSleepEndTime);

                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESSleepStopEvent*                a_event,
                RE::BSTEventSource<RE::TESSleepStopEvent>*) override
            {
                // Cleared before anything can return early: a flag left set
                // would stop the tick for good.
                Needs::GetSingleton().SetSleeping(false);

                auto* calendar = RE::Calendar::GetSingleton();
                if (!calendar || _startedAt < 0.0f) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Measured from the clock rather than taken from the desired end
                // time, so an interrupted night counts only the hours actually
                // slept.
                const float hours = (calendar->GetCurrentGameTime() - _startedAt) * 24.0f;
                _startedAt = -1.0f;

                logger::info("sleep ended after {:.1f}h{}", hours,
                    (a_event && a_event->interrupted) ? " (interrupted)" : "");

                if (hours > 0.0f) {
                    Needs::GetSingleton().NoteSlept(hours);
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            SleepSink() = default;

            float _startedAt{ -1.0f };
        };

        class EatSink : public RE::BSTEventSink<RE::TESEquipEvent>
        {
        public:
            static EatSink* GetSingleton()
            {
                static EatSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESEquipEvent*                a_event,
                RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                if (!a_event || !a_event->equipped) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (a_event->actor.get() != RE::PlayerCharacter::GetSingleton()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* form = RE::TESForm::LookupByID(a_event->baseObject);
                auto* alchemy = form ? form->As<RE::AlchemyItem>() : nullptr;
                if (!alchemy) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // A clean linen cloth patches frostbite and burns. It is a
                // potion and not a Food, so it has to be caught BEFORE the
                // IsFood gate - v0.4.0 says as much at this exact spot, and
                // putting it after the gate is why the feature was quietly
                // missing from the port.
                //
                // By form, not by editor id: the native GetFormEditorID comes
                // back empty for most records (Climate.cpp learned this once
                // already), so the string compare never matched and the bandage
                // would have gone on doing nothing.
                if (Forms::alchBandage && alchemy == Forms::alchBandage) {
                    Elemental::GetSingleton().NoteBandage();
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Frost resistance buys time on the cold bar, and it has to
                // be taken BEFORE the meal gate for the same reason the bandage
                // does: a potion is not food, so the gate below would drop it.
                // A SpecialFood that also resists frost passes through here and
                // then goes on to feed, which is correct - it does both.
                if (const float gift = Needs::ColdGift(alchemy); gift > 0.0f) {
                    Needs::GetSingleton().OnDrankWarm(gift);
                }

                // What this does to the bar is worked out in one place, so
                // the number the inventory preview shows and the number that
                // lands cannot drift apart. Everything that is not arithmetic -
                // the poison roll, the stagger - stays here, because a preview
                // must not do any of it.
                const auto meal = Needs::Assess(alchemy);
                if (!meal.feeds) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (meal.rawWeak) {
                    // The roll is deliberately flat - v0.4.0 applies disease
                    // resistance to a hit and not to this.
                    StagedDisease::Roll(Forms::hitDisease[3], Settings::fFoodPoisonChance,
                        "from raw food");
                    // Straight to empty, not merely no nourishment. v0.4.0's
                    // HungerAfterEating returns the maximum for raw food on a
                    // weak stomach - "return mx ; straight to empty, not a
                    // reduction" - and the first port read that as a zero
                    // restore, which made eating it almost free.
                    Needs::GetSingleton().EmptyHunger();
                    if (Settings::bRawFoodStagger) {
                        Needs::StaggerPlayer(Settings::fRawFoodStaggerForce);
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }

                Needs::GetSingleton().OnAte(meal.restore, meal.special);
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            EatSink() = default;
        };

        // A bedroll item becoming a reference in the world.
        //
        // This is the native equivalent of v0.4.0's script, which lived ON the
        // bedroll item: its OnContainerChanged fired on the dropped reference,
        // so Disable() and Delete() there were calls on self and there was
        // never any question which reference was meant.
        //
        // TESContainerChangedEvent looks like the match, and it was tried
        // first, but it is not: it carries a handle to the reference the item
        // is about to become, and that reference does not exist yet when the
        // event fires. The handle never resolved once in a whole session's log.
        // Searching a radius for anything of that base papered over it and
        // could have taken a bedroll the player had deliberately left nearby.
        //
        // TESInitScriptEvent hands over the reference the moment the engine
        // creates it, which is precisely what the Papyrus script had.
        class BedrollSink : public RE::BSTEventSink<RE::TESInitScriptEvent>
        {
        public:
            static BedrollSink* GetSingleton()
            {
                static BedrollSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESInitScriptEvent*                a_event,
                RE::BSTEventSource<RE::TESInitScriptEvent>*) override
            {
                if (!a_event || !Settings::bModEnabled || !Forms::bedrollItem) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* ref = a_event->objectInitialized.get();
                if (!ref || ref->GetBaseObject() != Forms::bedrollItem) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Claimed here, not in the task: both events are delivered
                // before either task runs, so anything decided on the game
                // thread decides it twice.
                if (!Bedroll::GetSingleton().ClaimDrop(ref->GetFormID())) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto handle = ref->CreateRefHandle();
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([handle]() {
                        if (auto dropped = handle.get()) {
                            Bedroll::GetSingleton().Place(dropped.get());
                        }
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            BedrollSink() = default;
        };

        // Anything that says the player wants to move.
        //
        // The warm-hands idle LOCKS the player in place: pressing W while it
        // plays moves nothing at all, so there is no movement to observe
        // afterwards. The key press is the only evidence there is.
        class InputSink : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static InputSink* GetSingleton()
            {
                static InputSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                RE::InputEvent* const*                a_event,
                RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!a_event || !Settings::bModEnabled) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* ui = RE::UI::GetSingleton();
                if (ui && ui->GameIsPaused()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* events = RE::UserEvents::GetSingleton();
                if (!events) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                for (auto* event = *a_event; event; event = event->next) {
                    // A gamepad stick is not a button, and nudging it is as
                    // much an attempt to move as tapping W.
                    if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                        auto* stick = static_cast<RE::ThumbstickEvent*>(event);
                        if (stick->IsLeft() &&
                            (std::abs(stick->xValue) > 0.1f ||
                                std::abs(stick->yValue) > 0.1f)) {
                            WarmAnim::GetSingleton().NoteIntent();
                        }
                        continue;
                    }

                    auto* button = event->AsButtonEvent();
                    if (!button) {
                        continue;
                    }

                    const auto& control = button->QUserEvent();

                    // IsPressed, not IsDown: holding a movement key has to keep
                    // the idle down, and IsDown is true for one frame only.
                    if (button->IsPressed() &&
                        (control == events->forward || control == events->back ||
                            control == events->strafeLeft ||
                            control == events->strafeRight ||
                            control == events->jump || control == events->sprint ||
                            control == events->sneak || control == events->readyWeapon ||
                            control == events->autoMove)) {
                        WarmAnim::GetSingleton().NoteIntent();
                    }

                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            InputSink() = default;
        };

        // Activating your own campfire puts it out, after a confirmation.
        //
        // v0.4.0 did this with an OnActivate script on the placed activator
        // (_RSL_CampfirePlaced). The activator itself has no behaviour of its
        // own, so nothing needs blocking here - the event is a notification and
        // the fire is inert either way.
        class ActivateSink : public RE::BSTEventSink<RE::TESActivateEvent>
        {
        public:
            static ActivateSink* GetSingleton()
            {
                static ActivateSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESActivateEvent*                  a_event,
                RE::BSTEventSource<RE::TESActivateEvent>*) override
            {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!a_event || !player || !Settings::bModEnabled) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (a_event->actionRef.get() != player) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto target = a_event->objectActivated;
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([target]() {
                        auto* ref = target.get();
                        Campfire::GetSingleton().OnActivated(ref);
                        // Diagnostic - see TakeDown.h.
                        TakeDown::Inspect(ref);
                        // Sleepable furniture: this is where the night is about
                        // to happen, and the only moment it can be asked about.
                        Needs::GetSingleton().NoteBed(ref);
                        // A tree we made choppable: the engine has just handed
                        // over its one log, so stamp the cooldown and top the
                        // yield up.
                        if (ref && Trees::GetSingleton().IsOurs(ref->GetBaseObject())) {
                            Trees::GetSingleton().OnHarvested(ref);
                        }
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            ActivateSink() = default;
        };

        // The grab: hold the activate key on a camp of ours and it comes up.
        //
        // The engine's own signal, the same one Papyrus calls OnGrab. It used
        // to be a poll on PlayerCharacter::GetGrabbedRef once a tick, and that
        // poll is why picking a bedroll up took several tries - see the note on
        // Bedroll::OnGrabbed.
        class GrabSink : public RE::BSTEventSink<RE::TESGrabReleaseEvent>
        {
        public:
            static GrabSink* GetSingleton()
            {
                static GrabSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESGrabReleaseEvent*                  a_event,
                RE::BSTEventSource<RE::TESGrabReleaseEvent>*) override
            {
                // The start of the grab, not its end. By the release the camp
                // is meant to be gone already.
                if (!a_event || !a_event->grabbed || !a_event->ref) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // The id, not the pointer: the work is deferred a frame and a
                // reference is not ours to hold across one.
                const auto id = a_event->ref->GetFormID();
                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([id]() { Bedroll::GetSingleton().OnGrabbed(id); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            GrabSink() = default;
        };

        // Who is hitting you, and what they leave behind.
        //
        // Draugr and slaughterfish are matched by race, trolls by keyword,
        // because troll variants are several races and all carry
        // ActorTypeTroll. Projectiles are excluded: v0.4.0 wants this to be a
        // melee contact, not an arrow from across the room.
        class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
        {
        public:
            static HitSink* GetSingleton()
            {
                static HitSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESHitEvent*                  a_event,
                RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                if (!a_event || !Settings::bModEnabled || !Settings::bDiseasesEnabled) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || a_event->target.get() != player) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (a_event->projectile != 0) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* aggressor = a_event->cause ? a_event->cause->As<RE::Actor>() : nullptr;
                auto* race = aggressor ? aggressor->GetRace() : nullptr;
                if (!race) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const DiseaseForms* caught = nullptr;
                if (race == Forms::raceDraugr) {
                    caught = &Forms::hitDisease[0];        // brown rot
                } else if (Forms::kwTroll && race->HasKeyword(Forms::kwTroll)) {
                    caught = &Forms::hitDisease[1];        // gutworm
                } else if (race == Forms::raceSlaughterfish) {
                    caught = &Forms::hitDisease[2];        // green spore
                }
                if (!caught) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Asked here, after the carrier is known, so the log says what
                // the shield actually stopped rather than merely that a hit
                // landed. kHitBlocked is the same HitData::kBlocked bit RFAB's
                // own plugin reads, so a block means the same thing on both
                // sides of the pack.
                if (Settings::bBlockStopsDisease &&
                    a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
                    logger::info("hit: blocked, no {} caught", caught->id);
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* owner = player->AsActorValueOwner();
                const float resist =
                    owner ? owner->GetActorValue(RE::ActorValue::kResistDisease) : 0.0f;
                StagedDisease::Roll(*caught,
                    Settings::fDiseaseHitChance * (1.0f - resist * 0.01f), "on a hit");

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            HitSink() = default;
        };
    }

    void Events::Install()
    {
        auto* source = RE::ScriptEventSourceHolder::GetSingleton();
        if (!source) {
            logger::error("no script event source holder - events will not arrive");
            return;
        }

        source->AddEventSink<RE::TESEquipEvent>(EatSink::GetSingleton());
        source->AddEventSink<RE::TESHitEvent>(HitSink::GetSingleton());
        source->AddEventSink<RE::TESInitScriptEvent>(BedrollSink::GetSingleton());
        source->AddEventSink<RE::TESSleepStopEvent>(SleepSink::GetSingleton());
        source->AddEventSink<RE::TESActivateEvent>(ActivateSink::GetSingleton());
        source->AddEventSink<RE::TESGrabReleaseEvent>(GrabSink::GetSingleton());

        // TESSleepStartEvent has a source in the holder but no header in
        // CommonLibSSE, so the templated helper cannot resolve it; the source is
        // taken through the base class directly instead.
        static_cast<RE::BSTEventSource<RE::TESSleepStartEvent>*>(source)
            ->AddEventSink(SleepSink::GetSingleton());

        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(InputSink::GetSingleton());
        } else {
            logger::error("no input device manager - the warm-hands idle will not "
                          "respond to input");
        }

        Elemental::Install();

        logger::info("event sinks installed");
    }
}
