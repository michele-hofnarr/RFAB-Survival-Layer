#include "PCH.h"

#include "Core/Campfire.h"

#include "Core/Climate.h"
#include "Core/Forms.h"
#include "Core/Notify.h"
#include "Core/Placement.h"
#include "Core/Shelter.h"
#include "Core/TakeDown.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // How far in front the fire goes down. v0.4.0's number.
        constexpr float DISTANCE = 110.0f;

        // The cooking pot's mesh pivot is off centre, so it needs nudging back
        // and down to sit on the spit. Measured in v0.4.0, not derived.
        constexpr float POT_FORWARD = -47.0f;
        constexpr float POT_UP = -13.3f;

        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        // The yes/no box the player gets for putting their own fire out.
        //
        // Both the question and the two button labels are read off the MESG in
        // the plugin, so the text still lives in one place - the generator - and
        // is still translated by the same strings file. Only the asking moved
        // from Papyrus to here.
        class ConfirmOut : public RE::IMessageBoxCallback
        {
        public:
            void Run(RE::IMessageBoxCallback::Message a_message) override
            {
                // The engine hands back the button index in the low byte.
                if (static_cast<std::uint32_t>(a_message) == 0) {
                    Campfire::GetSingleton().Extinguish();
                }
            }
        };

        void AskToPutOut()
        {
            auto* message = Forms::msgCampConfirm;
            if (!message) {
                // No question to ask is not a reason to make the fire
                // unputoutable.
                Campfire::GetSingleton().Extinguish();
                return;
            }

            auto* data = static_cast<RE::MessageBoxData*>(
                RE::MessageDataFactoryManager::GetSingleton()
                    ->GetCreator<RE::MessageBoxData>(
                        RE::InterfaceStrings::GetSingleton()->messageBoxData)
                    ->Create());
            if (!data) {
                logger::error("campfire: no message box to ask with");
                return;
            }

            // GetDescription fills a BSString and needs the owning form -
            // there is no plain getter.
            message->GetDescription(data->bodyText, message);
            for (const auto* button : message->menuButtons) {
                if (button) {
                    data->buttonText.push_back(button->text.c_str());
                }
            }
            data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>{ new ConfirmOut };
            data->QueueMessage();
        }

        [[nodiscard]] RE::TESObjectREFR* Deref(RE::FormID a_id)
        {
            return a_id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
        }

        // Place one piece: create it disabled, aim it, put it where it goes,
        // hand it to the player so nobody calls it stealing, then show it.
        [[nodiscard]] RE::TESObjectREFR* Put(RE::TESBoundObject* a_base,
            const RE::NiPoint3& a_at, float a_pitch, float a_roll, float a_heading)
        {
            auto* player = Player();
            auto* handler = RE::TESDataHandler::GetSingleton();
            if (!player || !a_base || !handler) {
                return nullptr;
            }

            // CREATED WHERE IT GOES, not created here and dragged there.
            //
            // PlaceObjectAtMe is this same call with the PLAYER's position and
            // angle - TESObjectREFR::PlaceObjectAtMe is one line and that line
            // is CreateReferenceAtLocation(base, GetPosition(), GetAngle(),
            // ...). So it built the object at the player's feet, with its model
            // and everything the model brings with it, and only then was it
            // teleported a hundred units away.
            //
            // v0.4.0 kept clear of that by placing initially disabled and
            // enabling once it was in place - PlaceAtMe(base, 1, false, true)
            // ... Enable() - and Campfire does the same. CommonLibSSE has no
            // such argument and no Enable at all, so the way out is not to
            // place it in the wrong spot in the first place.
            //
            // Radians, and in the engine's own order - Placement hands them
            // over that way for exactly this reason.
            const auto handle = handler->CreateReferenceAtLocation(a_base, a_at,
                RE::NiPoint3{ a_pitch, a_roll, a_heading }, player->GetParentCell(),
                player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(),
                false, true);

            auto ref = handle.get();
            if (!ref) {
                return nullptr;
            }

            // The cell handed over is the PLAYER's, and the spot is a hundred
            // units from the player - which is over the boundary whenever he
            // stands near one. SetPosition is what re-homes a reference to the
            // cell it is actually in; here it moves nothing, because it is
            // already there.
            ref->SetPosition(a_at);
            // Yours, as in v0.4.0's SetActorOwner.
            ref->extraList.SetOwner(player->GetActorBase());
            return ref.get();
        }
    }

    Campfire& Campfire::GetSingleton()
    {
        static Campfire singleton;
        return singleton;
    }

    RE::TESObjectREFR* Campfire::Fire() const
    {
        return Deref(_state.fire);
    }

    bool Campfire::PlayerAtOwnFire() const
    {
        auto* fire = Fire();
        auto* player = Player();
        if (!fire || !player) {
            return false;
        }
        return player->GetPosition().GetDistance(fire->GetPosition()) <=
               Settings::fFireRadius;
    }

    void Campfire::Hide(const State& a_old)
    {
        TakeDown::Hide(Deref(a_old.fire), "fire");
        TakeDown::Hide(Deref(a_old.spit), "spit");
        TakeDown::Hide(Deref(a_old.pot), "pot");
    }

    void Campfire::Retire(const State& a_old)
    {
        // The ID, not the pointer. A fire burns out on the clock, and the
        // clock does not care which cell the player is standing in: at the
        // moment its hours are up the reference may be one the engine does
        // not currently have, and Deref answers null for it. That null used
        // to end the take-down silently - the fire was announced as out, its
        // ids were dropped, and it went on burning where nothing could reach
        // it. TakeDown holds the number until the reference turns up.
        TakeDown::Now(a_old.fire, "fire");
        TakeDown::Now(a_old.spit, "spit");
        TakeDown::Now(a_old.pot, "pot");
    }

    void Campfire::Extinguish()
    {
        if (!_state.fire) {
            return;
        }
        Retire(_state);
        _state = State{};
        Notify::GetSingleton().Push(Forms::msgCampOut);
        logger::info("campfire: out");
    }

    void Campfire::Light()
    {
        auto* player = Player();
        if (!player || !Settings::bModEnabled) {
            return;
        }

        // The cast reaches here twice, from the effect and from the apply event
        // that backs it up. One gate handles both that and the rate of the
        // "no fuel" notice.
        const auto now = std::chrono::steady_clock::now();
        if (_castOnce &&
            std::chrono::duration<float>(now - _lastCast).count() <
                Settings::fCampfireCooldown) {
            return;
        }
        _lastCast = now;
        _castOnce = true;

        if (!Forms::perkSurvivalBasics || !player->HasPerk(Forms::perkSurvivalBasics)) {
            Notify::GetSingleton().Push(Forms::msgCampNoPerk);
            return;
        }

        // Rain puts a fire out faster than anyone can build one, and there
        // is no roof here. Asked before the firewood is counted so the refusal
        // says what is actually wrong - being told "you need a log" while
        // standing in a downpour holding six of them is the wrong answer.
        if (Climate::RainOnPlayer()) {
            Notify::GetSingleton().Push(Forms::msgCampRain);
            logger::info("campfire: rain, no roof");
            return;
        }

        const auto need = static_cast<std::int32_t>(Settings::fCampfireFuel);
        if (!Forms::firewood || player->GetItemCount(Forms::firewood) < need) {
            Notify::GetSingleton().Push(Forms::msgCampNoFuel);
            return;
        }

        // Facing a rock, the spot is INSIDE the rock: the downward probe goes
        // through its face and the fire is built in the middle of it. Asked
        // here, before the firewood is spent and before the old fire is put
        // away, so a refusal costs nothing and undoes nothing.
        if (Placement::BlockedInFront(DISTANCE)) {
            Notify::GetSingleton().Push(Forms::msgNoRoom);
            logger::info("campfire: no room in front");
            return;
        }

        auto* base = Forms::campfireLit ? Forms::campfireLit : Forms::baseCampfire;
        if (!base) {
            logger::error("campfire: no base object to place");
            return;
        }

        player->RemoveItem(Forms::firewood, need, RE::ITEM_REMOVE_REASON::kRemove,
            nullptr, nullptr);

        const State old = _state;

        // The old fire goes out of the way BEFORE the spot is found.
        //
        // v0.4.0 could leave it standing while the new one was placed, because
        // it had no raycast - it dropped everything at a fixed offset from the
        // player. Here the spot comes from a ray straight down at the place the
        // new fire will go, and the fire already standing there is what the ray
        // hits: the second fire is laid on the first one's stones, at the angle
        // of whatever the ray struck, and the third on the second. The first
        // one always looked right and none of the others did.
        //
        // Disable only. The references stay until the new fire is standing,
        // which is v0.4.0's rule - a disabled reference has no collision, so
        // that is enough to get it out of the ray's way.
        Hide(old);

        const auto spot = Placement::InFront(DISTANCE, Settings::fMaxPlacementTilt);

        auto* fire = Put(base, spot.position, spot.pitch, spot.roll, spot.heading);
        if (!fire) {
            logger::error("campfire: the fire would not place");
            return;
        }

        RE::TESObjectREFR* spit = nullptr;
        RE::TESObjectREFR* pot = nullptr;

        // The Chef perk says you know how; the kettle is what you cook in.
        // It is never consumed - carrying it is the whole cost, and the same
        // kettle is what the cookpot water recipe asks for, so one item turns
        // a fire into a kitchen in both places rather than being a token.
        const bool hasKettle = Forms::kettle && player->GetItemCount(Forms::kettle) > 0;
        if (Forms::perkCook && player->HasPerk(Forms::perkCook) &&
            Forms::baseCookSpit && Forms::baseCookPot && hasKettle) {
            // Both are lifted by the same amount. They are already aligned
            // to each other; what they are not aligned to is the fire, and
            // they sit below it. The number is measured off the mesh in game.
            const RE::NiPoint3 spitAt{
                spot.position.x, spot.position.y,
                spot.position.z + Settings::fCookGearZ
            };
            spit = Put(Forms::baseCookSpit, spitAt, spot.pitch, spot.roll,
                spot.heading);

            const float fx = std::sin(spot.heading);
            const float fy = std::cos(spot.heading);
            const RE::NiPoint3 potAt{
                spot.position.x + fx * POT_FORWARD,
                spot.position.y + fy * POT_FORWARD,
                spot.position.z + POT_UP + Settings::fCookGearZ
            };
            pot = Put(Forms::baseCookPot, potAt, spot.pitch, spot.roll, spot.heading);
        }

        _state.fire = fire->GetFormID();
        _state.spit = spit ? spit->GetFormID() : 0;
        _state.pot = pot ? pot->GetFormID() : 0;

        if (auto* calendar = RE::Calendar::GetSingleton()) {
            const float hours = HasCampPerks() ? Settings::fCampfireBurnHoursPerk
                                               : Settings::fCampfireBurnHours;
            _state.burnsUntilDays = calendar->GetCurrentGameTime() + hours / 24.0f;
        }

        Retire(old);

        Notify::GetSingleton().Push(Forms::msgCampLit);
        logger::info("campfire: lit, burns until day {:.3f} (grounded {}, kettle {})",
            _state.burnsUntilDays, spot.grounded, hasKettle);

        // Something now stands over the spot; the cached answer is stale.
        Shelter::GetSingleton().Invalidate();
    }

    void Campfire::OnActivated(const RE::TESObjectREFR* a_target)
    {
        if (!a_target || !_state.fire || a_target->GetFormID() != _state.fire) {
            return;
        }
        AskToPutOut();
    }

    void Campfire::Update()
    {
        auto* player = Player();
        if (!player) {
            return;
        }

        // The power comes and goes with the perk, so it is never sitting in the
        // menu unusable.
        if (Forms::powerCampfire) {
            const bool earned = Settings::bModEnabled && Forms::perkSurvivalBasics &&
                                player->HasPerk(Forms::perkSurvivalBasics);
            const bool has = player->HasSpell(Forms::powerCampfire);
            if (earned && !has) {
                player->AddSpell(Forms::powerCampfire);
            } else if (!earned && has) {
                player->RemoveSpell(Forms::powerCampfire);
            }
        }

        if (!_state.fire) {
            return;
        }

        auto* calendar = RE::Calendar::GetSingleton();
        if (calendar && calendar->GetCurrentGameTime() >= _state.burnsUntilDays) {
            logger::info("campfire: burned out");
            Extinguish();
        }
    }
}
