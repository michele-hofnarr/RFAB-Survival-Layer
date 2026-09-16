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

namespace RSL::TakeDown
{
    // Disable now; the delete mark follows once the reference is down.
    void Now(RE::TESObjectREFR* a_ref, std::string_view a_what);

    // Disable only, for the old fire that has to stop being something a
    // downward ray can hit while the new one is still being sited. It is
    // handed to Now() afterwards.
    void Hide(RE::TESObjectREFR* a_ref, std::string_view a_what);

    // From the tick, before anything else and whether or not the mod is
    // running: this is what finishes every take-down, and a camp left half
    // taken down is the bug above.
    void Watch();

    // The player reached for something. If it is a piece of camp of ours that
    // nothing of ours knows about, name it - once.
    void Inspect(const RE::TESObjectREFR* a_ref);
}
