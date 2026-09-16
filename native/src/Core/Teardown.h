#pragma once

// Taking the mod off a save without leaving anything of it behind.
//
// The master switch already strips what the mod REMEMBERS handing out: the
// three axis abilities, the full-bar bonuses, twelve illnesses, the RFAB
// wrappers, the lesion, the hidden marker, the camp. That is a list, and a
// list is only as good as the day it was written. Three things are never on
// it:
//
//   * a record we forgot to add to it;
//   * a record from an OLDER BUILD of this mod, still on the player under a
//     FormID that has since moved - RemoveSpell(the id we hold now) does not
//     touch the spell sitting there under the id we used to hold;
//   * anything a bug left applied.
//
// None of that matters while the plugin is loaded: an unknown spell of ours is
// inert. It matters the moment the plugin is gone. The save then holds a spell
// whose form no longer resolves, and any code that walks the player's spells
// or active effects without checking for null walks straight into it. That is
// not hypothetical - it is what a crash dump from this pack shows, a null
// TESForm read at +0x14 while a menu enumerated forms.
//
// So the sweep below asks a different question. Not "is this one of the
// records I am holding" but "did this come out of my plugin at all". The
// answer is in the FormID, and it is right for records this build has never
// heard of.

namespace RSL::Teardown
{
    // Strip every spell and dispel every active effect that came out of this
    // mod's plugin, known to this build or not.
    //
    // Returns how many were taken off. Call it after the named teardown, so
    // the modules that keep their own state clear it themselves first and this
    // only ever finds what they could not reach.
    std::size_t StripEverythingOfOurs();
}
