#pragma once

// The portable bedroll, and the tent that comes with it.
//
// Craft it at a tanning rack, drop it and it unrolls in front of you; activate
// it to sleep, grab it to pick it back up. With both RFAB survival perks a tent
// goes up over it.
//
// Two things about v0.4.0 that the first port got wrong, both by inventing a
// rule that was never there.
//
// There is no "one camp at a time". The controller does not track a bedroll at
// all: the script lives on the ITEM, so every dropped bedroll lays its own
// furniture and every piece of furniture answers its own grab. Drop ten, pick
// up ten. The first port kept a single pair of form ids and tore the previous
// camp down on every drop.
//
// And the dropped item is not something to go looking for. In Papyrus,
// OnContainerChanged fires ON the dropped reference - Disable() and Delete()
// there are calls on self, and there is no ambiguity about which reference is
// meant. The port had no such handle: TESContainerChangedEvent carries one, but
// the reference does not exist yet when the event fires and it never resolves.
// Searching a radius for anything of that base was a workaround for the missing
// handle, and it could take a bedroll the player had deliberately left nearby.
// TESInitScriptEvent is the honest equivalent - it hands over the reference the
// moment the engine creates it, which is exactly what the Papyrus script had.

namespace RSL
{
    class Bedroll
    {
    public:
        static Bedroll& GetSingleton();

        // Claim a dropped reference, once.
        //
        // TESInitScriptEvent is not once per reference. It fired twice for the
        // same dropped bedroll, in the same millisecond, and both events had
        // queued their work before either ran - so two camps went down in the
        // same spot and havok threw one of them into the air. That is what the
        // "two copies, both interactive" screenshots were.
        //
        // v0.4.0 never had to think about this: OnContainerChanged fires once
        // per container change, full stop. Here the event means "this reference
        // was initialised", and a reference can be initialised more than once,
        // so turning one into a camp is ours to do exactly once.
        [[nodiscard]] bool ClaimDrop(RE::FormID a_id);

        // A bedroll item has just become a reference in the world. Lays the
        // furniture, raises the tent if the perks are there, and removes the
        // item - the same three steps, on the same reference, as v0.4.0.
        void Place(RE::TESObjectREFR* a_dropped);

        // Called from the tick: takes everything down when the mod is
        // switched off. The grab itself no longer lives here - see OnGrabbed.
        void Update();

        // The player grabbed a reference. If it is one of our bedrolls, that
        // camp comes up - tent and all.
        //
        // WHY AN EVENT AND NOT THE TICK. This used to ask
        // PlayerCharacter::GetGrabbedRef() once a pass, on the stated grounds
        // that "CommonLibSSE has no such event". It has one:
        // RE::TESGrabReleaseEvent, a full source on ScriptEventSourceHolder,
        // and it is the same engine signal Papyrus exposes as OnGrab - which is
        // what v0.4.0 used.
        //
        // The poll was not merely inelegant, it was the bug. Furniture is never
        // really taken into the hands, so the engine sets the grabbed handle and
        // clears it again inside one frame; a pass that does not happen to land
        // in that window sees nothing at all. That is exactly what a bedroll
        // needing three or four seconds of trying - and sometimes refusing
        // outright - looks like from the outside. The event fires on the
        // engine's decision and cannot be missed however briefly the state
        // lasts.
        void OnGrabbed(RE::FormID a_ref);

        // Is the player under a tent of their own? Asked by the cold model,
        // which pairs it with "is there a roof overhead" - the roof is the
        // tent, this says whose it is.
        [[nodiscard]] bool UnderOwnTent() const;

        // Every camp: furniture id -> tent id, 0 when there is no tent.
        [[nodiscard]] auto& Camps() { return _camps; }
        void                Reset() { _camps.clear(); }

    private:
        // Take one camp down. Returns the item to the player when asked.
        void RecoverOne(RE::FormID a_furniture, bool a_giveItemBack);
        void RecoverAll(bool a_giveItemBack);

        std::unordered_map<RE::FormID, RE::FormID> _camps;

        // Dropped references already turned into a camp, and WHEN.
        //
        // IT IS THE TIME THAT MATTERS. This was a set of bare ids that nothing
        // ever emptied, on the stated grounds that the reference is deleted
        // straight afterwards - which is exactly what makes the id come back:
        // Skyrim hands deleted dynamic ids out again. Pitch a bedroll, pack
        // it, drop it a second time, and the new reference can be given the
        // number the old one had; the claim it never let go of then refused
        // it, and the bedroll lay on the ground as an item with nothing
        // whatever in the log.
        //
        // A claim only has to outlive the burst of TESInitScriptEvents for one
        // reference - the same millisecond, measured - so it is kept for
        // seconds and dropped. See CLAIM_LIFETIME in the cpp.
        std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>
                   _claimed;
        std::mutex _claimLock;
    };
}
