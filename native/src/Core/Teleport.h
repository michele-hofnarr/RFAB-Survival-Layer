#pragma once

// Cold takes teleportation away.
//
// Kazan Edition made the Teleport and Mark-and-Recall scrolls cheap and put
// them everywhere. Fast travel is off in this pack, and those scrolls were the
// way out of almost any trouble the cold could make - so the cold model lost
// its teeth to an inventory item.
//
// WHERE THIS SITS, and why here rather than anywhere else:
//
// The game already refuses to cast Teleport indoors. That refusal is not a
// script and not a special case - it is a condition on the magic effect, and
// what evaluates it is CheckCast, the engine's own gate BEFORE the cast
// commits. Nothing is spent when it says no: no magicka, and - the part that
// matters here - no scroll. So this hooks that same gate one level up, adds its
// own reason to say no, and leaves everything else alone.
//
// A condition on the record would have been the closer copy, and it was the
// first idea. Two things rule it out: a condition carries no text, so there
// would be no way to say WHY, and adding one means overriding RFAB's magic
// effects - records that carry their own scripts, on a plugin that updates
// constantly. See the note on RfabPatch.
//
// WHAT IS BLOCKED is two magic effects rather than the spells and scrolls
// themselves, because RFAB gives the spell and the scroll of each pair the same
// effect. Two entries cover four records, and a new scroll built on the same
// effect is covered the day it appears.

namespace RSL
{
    struct Teleport
    {
        // Writes the vtable hook. Once, from kDataLoaded, after Forms::Load.
        static void Install();
    };
}
