#include "PCH.h"

#include "Core/Bedroll.h"

#include "Core/Forms.h"
#include "Core/Notify.h"
#include "Core/Placement.h"
#include "Core/Shelter.h"
#include "Core/TakeDown.h"
#include "Core/Player.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // How far out the camp goes. v0.4.0 used 90 for the bedroll and put the
        // tent exactly on top of it, which left the player standing inside what
        // they had just pitched - the bug this number exists to fix. A tent is
        // roughly two hundred units across, so the whole camp moves out far
        // enough to stand beside rather than in.
        //
        // Worth tuning in game: it is measured off the mesh, not derived.
        constexpr float BEDROLL_ONLY_DISTANCE = 90.0f;
        constexpr float WITH_TENT_DISTANCE = 220.0f;

        // How near a tent of ours has to be to count as the one overhead.
        constexpr float UNDER_TENT = 250.0f;

        // How long a drop stays claimed. Generous against the burst it exists
        // to stop - two events in the same millisecond - and short against the
        // thing it must not outlive, which is the id itself.
        constexpr auto CLAIM_LIFETIME = std::chrono::seconds(10);

        [[nodiscard]] RE::TESObjectREFR* Deref(RE::FormID a_id)
        {
            return a_id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
        }

        [[nodiscard]] RE::TESObjectREFR* Put(RE::TESBoundObject* a_base,
            const Placement::Spot& a_spot, const RE::NiPoint3& a_at)
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
            // teleported a couple of hundred units away.
            //
            // v0.4.0 kept clear of that by placing initially disabled and
            // enabling once it was in place - PlaceAtMe(base, 1, false, true)
            // ... Enable() - and Campfire does the same. CommonLibSSE has no
            // such argument and no Enable at all, so the way out is not to
            // place it in the wrong spot in the first place.
            //
            // Radians, straight from Placement - see the note there.
            const auto handle = handler->CreateReferenceAtLocation(a_base, a_at,
                RE::NiPoint3{ a_spot.pitch, a_spot.roll, a_spot.heading },
                player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr,
                RE::ObjectRefHandle(), false, true);

            auto ref = handle.get();
            if (!ref) {
                return nullptr;
            }

            // The cell handed over is the PLAYER's, and the spot is not where
            // the player stands. SetPosition re-homes a reference to the cell
            // it is actually in; here it moves nothing, because it is already
            // there.
            ref->SetPosition(a_at);
            // Yours, as in v0.4.0's SetActorOwner: what you pitched is not
            // somebody else's bed to be caught sleeping in.
            ref->extraList.SetOwner(player->GetActorBase());
            return ref.get();
        }
    }

    Bedroll& Bedroll::GetSingleton()
    {
        static Bedroll singleton;
        return singleton;
    }

    bool Bedroll::UnderOwnTent() const
    {
        auto* player = Player();
        if (!player || _camps.empty()) {
            return false;
        }

        const auto at = player->GetPosition();
        for (const auto& [furniture, tent] : _camps) {
            if (!tent) {
                continue;
            }
            if (auto* ref = Deref(tent);
                ref && !ref->IsDisabled() &&
                ref->GetPosition().GetDistance(at) <= UNDER_TENT) {
                return true;
            }
        }
        return false;
    }

    bool Bedroll::ClaimDrop(RE::FormID a_id)
    {
        std::scoped_lock lock(_claimLock);

        const auto now = std::chrono::steady_clock::now();
        std::erase_if(_claimed, [now](const auto& a_entry) {
            return now - a_entry.second > CLAIM_LIFETIME;
        });

        return _claimed.emplace(a_id, now).second;
    }

    void Bedroll::Place(RE::TESObjectREFR* a_dropped)
    {
        auto* player = Player();
        if (!player || !Forms::bedrollFurn) {
            return;
        }

        const bool tent = HasCampPerks() && Forms::baseTent;

        // Asked before the item is touched. With a tent the camp reaches out
        // to 220 units, which crosses a wall far more readily than 90 does.
        //
        // On a refusal it goes back into the pack. Leaving it where it fell
        // sounded reasonable and is not: this runs while the dropped thing is
        // still in the air, so what the player would be left with is a bedroll
        // hanging at head height.
        //
        // Put back and removed as two steps, not through PickUpObject. That was
        // tried first, on the grounds that it is the engine's own pick-up, and
        // the log showed it called and the bedroll still hanging there - the
        // reference is a frame old at this point and has no 3D yet, which is
        // the state that path is least prepared for. Disable() then
        // SetDelete(true) is what the success path below already does to this
        // same reference at this same moment, so it is known to work here.
        if (Placement::BlockedInFront(tent ? WITH_TENT_DISTANCE
                                           : BEDROLL_ONLY_DISTANCE)) {
            if (a_dropped) {
                const auto count = std::max(1, a_dropped->extraList.GetCount());
                if (auto* base = a_dropped->GetBaseObject()) {
                    player->AddObjectToContainer(base, nullptr, count, nullptr);
                }
                TakeDown::Now(a_dropped, "dropped bedroll");
                logger::info("bedroll: no room in front - {} back in the pack, "
                             "item {:08X} removed",
                    count, a_dropped->GetFormID());
            } else {
                logger::info("bedroll: no room in front");
            }
            Notify::GetSingleton().Push(Forms::msgNoRoom);
            return;
        }

        // The dropped item goes out of the world BEFORE the spot is found.
        //
        // This is v0.4.0's order - its script calls Disable() first and
        // Delete() last - and the reason it matters here is the raycast. The
        // item is lying on the ground in front of the player, which is exactly
        // where the ray looks: it hit the item, took its rounded surface as the
        // ground, and the bedroll was laid on top of it at the angle of a
        // bedroll-shaped lump. Deleting the item afterwards left the furniture
        // hovering where that lump had been.
        //
        // It only showed without a tent because a tent moves the whole camp out
        // to 220 units, past where the item lands; at 90 the ray was on top of
        // it every time.
        if (a_dropped) {
            TakeDown::Hide(a_dropped, "dropped bedroll");
        }

        const auto spot = Placement::InFront(
            tent ? WITH_TENT_DISTANCE : BEDROLL_ONLY_DISTANCE,
            Settings::fMaxPlacementTilt);

        auto* furn = Put(Forms::bedrollFurn, spot, spot.position);
        if (!furn) {
            // The item was hidden a few lines up so the raycast would not land
            // on it, and returning here left it exactly there: disabled, never
            // deleted, with nothing in the player's pack to show for it. The
            // bedroll was simply gone.
            //
            // The same two steps the no-room path above takes, for the same
            // reason: one back in the pack, the reference taken down.
            std::int32_t back = 0;
            if (a_dropped) {
                back = std::max(1, a_dropped->extraList.GetCount());
                if (auto* base = a_dropped->GetBaseObject()) {
                    player->AddObjectToContainer(base, nullptr, back, nullptr);
                }
                // Counted BEFORE this: the reference is marked for deletion
                // here and is not ours to read afterwards.
                TakeDown::Now(a_dropped, "dropped bedroll");
            }
            logger::error("bedroll: would not place - {} back in the pack", back);
            return;
        }

        RE::FormID tentID = 0;
        if (tent) {
            // Centred on the bedroll on purpose - the tent is meant to be over
            // it. What was wrong before was the distance from the player, not
            // this.
            if (auto* pitched = Put(Forms::baseTent, spot, spot.position)) {
                tentID = pitched->GetFormID();
            }
        }

        _camps[furn->GetFormID()] = tentID;

        // And it is deleted last, as v0.4.0 deletes it last. It was already
        // disabled above.
        if (a_dropped) {
            TakeDown::Now(a_dropped, "dropped bedroll");
        }

        logger::info("bedroll: laid {:08X}{} from item {:08X} "
                     "(grounded {}, {} camp(s) now)",
            furn->GetFormID(), tent ? " with a tent" : "",
            a_dropped ? a_dropped->GetFormID() : 0, spot.grounded, _camps.size());

        // A tent is a roof, and the cached shelter answer predates it.
        Shelter::GetSingleton().Invalidate();
    }

    void Bedroll::RecoverOne(RE::FormID a_furniture, bool a_giveItemBack)
    {
        const auto found = _camps.find(a_furniture);
        if (found == _camps.end()) {
            return;
        }

        const RE::FormID furnID = found->first;
        const RE::FormID tentID = found->second;

        // Both go in one pass. v0.4.0 had the bedroll's own script take the
        // tent away, which meant two removals racing - and a bedroll that is
        // still activatable while being taken is a sleep menu nobody asked for.
        //
        // By id, because the mod going off takes every camp down at once and
        // they are not all in loaded cells. See Core/TakeDown.h.
        if (auto* furn = Deref(furnID)) {
            furn->SetActivationBlocked(true);
        }
        TakeDown::Now(furnID, "bedroll");
        TakeDown::Now(tentID, "tent");

        _camps.erase(found);

        if (a_giveItemBack && Forms::bedrollItem) {
            if (auto* player = Player()) {
                player->AddObjectToContainer(Forms::bedrollItem, nullptr, 1, nullptr);
            }
        }

        logger::info("bedroll: taken up {:08X} ({} camp(s) left)",
            a_furniture, _camps.size());
        Shelter::GetSingleton().Invalidate();
    }

    void Bedroll::RecoverAll(bool a_giveItemBack)
    {
        while (!_camps.empty()) {
            RecoverOne(_camps.begin()->first, a_giveItemBack);
        }
    }

    void Bedroll::Update()
    {
        // One job left here. Switching the mod off has to put every camp back
        // in the player's pack, and nothing else is going to notice that the
        // switch moved. The grab used to be asked about here too; it is an
        // event now, which is why this is all that remains.
        if (!_camps.empty() && !Settings::bModEnabled) {
            RecoverAll(true);
        }
    }

    void Bedroll::OnGrabbed(RE::FormID a_ref)
    {
        if (!a_ref || _camps.empty() || !Settings::bModEnabled) {
            return;
        }

        // The bedroll only, and _camps is keyed by it - so the test for "is
        // this one of ours" is the same lookup RecoverOne already does, and
        // handing the id straight over is the whole of the work.
        //
        // The tent is not a candidate. It is NorTentSmall (Skyrim.esm
        // 0x000800E2) and that record is a STAT: a static has no body to take
        // hold of, so a grab can never name it. This used to test for it
        // anyway. The belief came from the poll era, where a tented camp was
        // the hardest of all to pick up - that was the tent's collision getting
        // in the way of aiming at the bedroll, not the tent being caught in its
        // place.

        // A camp of ours that _camps has no entry for gets named in the log
        // rather than silently ignored. Diagnostic - see TakeDown.h.
        if (!_camps.contains(a_ref)) {
            TakeDown::Inspect(Deref(a_ref));
        }

        RecoverOne(a_ref, true);
    }
}
