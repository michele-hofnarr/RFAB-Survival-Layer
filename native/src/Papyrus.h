#pragma once

// The one place a script can reach into this plugin.
//
// Almost nothing here needs it: the mod runs off its own tick and the engine's
// events, and v0.4.0's whole Papyrus controller is exactly what this rewrite
// exists to replace. The menu is the exception. MCM Helper drives a button by
// calling a function on a script, and the illnesses it has to reset live here -
// stage, accumulator and spells all native, with nothing for a script to take
// hold of.
//
// So the script forwards and does not decide. _RSL_MCM.ResetIllnesses() calls
// ResetIllnessesNative(), which is bound below; the rule for what a reset means
// stays in one place, in Core/Illnesses.

namespace RSL::Papyrus
{
    // Register the native functions with the VM.
    //
    // At PLUGIN LOAD, not at kDataLoaded: the interface queues the callback and
    // runs it when the VM comes up, and that is earlier than the data-loaded
    // message. Registering later means the script finds no native behind its
    // declaration and the button throws instead of doing nothing.
    void Install();
}
