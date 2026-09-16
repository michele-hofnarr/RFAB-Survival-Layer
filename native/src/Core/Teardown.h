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
// inert. It matters the moment the plugin is gone. The save then holds a form
// id that resolves to nothing, and any code that walks those forms without
// checking for null walks straight into it.
//
// So the sweep below asks a different question. Not "is this one of the
// records I am holding" but "did this come out of my plugin at all". The
// answer is in the FormID, and it is right for records this build has never
// heard of.
//
// FOUR PLACES, not one. The player's spell list and running effects were the
// obvious two, and sweeping only those did not fix the crash - proved by a
// dump taken minutes after a sweep that reported the player clean. The game
// keeps forms in two more places that no amount of RemoveSpell reaches:
//
//   * MagicFavorites, a singleton holding the favourites and the hotkeys as
//     raw form pointers. Unfavouriting is not implied by unlearning, and the
//     save keeps that list. This is the one that crashed: the campfire power
//     is a lesser power, the player favourites it, and the menu that walks
//     favourites is the menu that went down.
//   * the player's inventory, where the bedroll ends up - put there by the
//     teardown itself, one step earlier.

namespace RSL::Teardown
{
    // Strip every spell, effect, favourite and carried item that came out of
    // this mod's plugin, known to this build or not.
    //
    // Returns how many were taken off. Call it after the named teardown, so
    // the modules that keep their own state clear it themselves first and this
    // only ever finds what they could not reach.
    std::size_t StripEverythingOfOurs();
}
