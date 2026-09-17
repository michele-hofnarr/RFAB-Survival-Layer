#pragma once

// Taking a placed reference out of the world.
//
// WHY THIS IS NOT TWO CALLS IN A ROW.
//
// v0.4.0 took a camp down from Papyrus, and so does Campfire itself
// (_CampInternal.TryToDisableAndDeleteRef): Disable(false), then Delete().
// The port wrote that as ref->Disable() followed immediately by
// ref->SetDelete(true), and that is NOT the same thing. ObjectReference.Disable
// is declared
//
//     void function Disable(bool abFadeOut = false) Native
//
// and it is LATENT - "this function is latent and will wait for the fade out
// and/or disable to happen". The script stops there. Delete() runs on the next
// line only once the engine says the reference is down. Two C++ calls in the
// same instant hand the delete mark to a reference that is still on its way
// out.
//
// What that cost, measured rather than guessed: the take-down log shows both
// flags landing and the 3D detaching, and seconds later the form is no longer
// in the global table - and yet the object is still standing where it was put,
// and the console can still name it. Every route we have back to it goes
// through TESForm::LookupByID, which is a plain lookup in that table, so once
// it is out of there the fire cannot be put out and the bedroll cannot be
// picked up. Only the console can still reach it.
//
// So the wait comes back. Disable goes out now; the delete mark waits for the
// reference to actually report itself down, which is checked on the next pass
// of the tick. There is no completion callback to subscribe to - the VM gets
// one and we do not - so the next pass is the wait.
//
// WHY THE TAKE-DOWN OUTLIVES THE CALL THAT ASKED FOR IT.
//
// A take-down is asked for at the moment the fire's hours run out, and that
// moment has nothing to do with where the player is standing. LookupByID
// answers for a reference the engine currently has; a reference whose cell is
// not loaded is not one of them, so the take-down reached a null pointer and
// did nothing at all - silently, because "no reference" read as "nothing to
// do". The fire was announced as out, its ids were dropped, and it went on
// burning in a cell nobody was in, no longer known to anything that could put
// it out. Measured: campfire out at 22:28:06.853 with not one take-down line
// logged, and the same fire found standing four minutes later, disabled false,
// deleted false.
//
// v0.4.0 had already met this and said so, in _RSL_Controller.DropCampRef:
//
//     Queue a placed ref for deletion instead of deleting it inline:
//     Disable/Delete do NOT process on a ref whose cell is unloaded (light a
//     campfire outside, go into a cave, light another - the outdoor one would
//     never die). CampfireGC retries every tick and clears entries once the
//     cell loads and the ref is gone.
//
// Its queue was a FormList in StorageUtil on the player, which is to say in the
// save; this one is a list in the co-save. Same mechanism: the id is taken on
// trust, retried every pass, and let go only once the reference has actually
// been reached and taken down.

namespace RSL::TakeDown
{
    // One reference still owed a take-down. This is what goes in the co-save.
    struct Entry
    {
        RE::FormID  id{ 0 };
        std::string what;
    };

    // Take it down: disable now if it can be reached, and keep the id until
    // that has actually happened. The delete mark follows once it is down.
    void Now(RE::FormID a_id, std::string_view a_what);
    void Now(RE::TESObjectREFR* a_ref, std::string_view a_what);

    // Disable only, for the old fire that has to stop being something a
    // downward ray can hit while the new one is still being sited. It is
    // handed to Now() afterwards. Best effort by design: a reference that
    // cannot be reached is not in the ray's way either.
    void Hide(RE::TESObjectREFR* a_ref, std::string_view a_what);

    // From the tick, before anything else and whether or not the mod is
    // running: this is what finishes every take-down, and a camp left half
    // taken down is the bug above.
    void Watch();

    // The player reached for something. If it is a piece of camp of ours that
    // nothing of ours knows about, name it - once.
    void Inspect(const RE::TESObjectREFR* a_ref);

    // The co-save. Outstanding() is every take-down still owed; Restore() puts
    // one back on load; Forget() drops the lot, for the revert that precedes
    // a load.
    [[nodiscard]] std::vector<Entry> Outstanding();
    void                             Restore(RE::FormID a_id, std::string_view a_what);
    void                             Forget();
}
