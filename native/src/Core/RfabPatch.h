#pragma once

namespace RSL
{
    // Fixes to RFAB's own records that are made in memory, once, rather than by
    // overriding the record in our .esp.
    //
    // RFAB.esp is a moving target and the list of records we override is kept
    // deliberately short and explicit (README, the list of override points).
    // A one-field cosmetic fix does not earn a place on that list: an override
    // would have to be re-derived every time RFAB touches the record, and would
    // silently ship a stale copy of everything else in it if we ever got that
    // wrong.
    struct RfabPatch
    {
        // Runs on kDataLoaded, after Forms::Load.
        static void Apply();
    };
}
