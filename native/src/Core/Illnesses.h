#pragma once

#include "Core/Illness.h"

#include <memory>
#include <vector>

// Every illness the mod runs, in one place that owns them.
//
// This exists because three separate pieces of code were each keeping their own
// idea of "all the illnesses", and each one had to be edited whenever the set
// changed:
//
//   * the tick, as four hand-written blocks in a row;
//   * a table of ids to stage spells, written out by hand, which is what made
//     the standing-state report possible and was already the wrong shape the
//     day it was written;
//   * Disease::ApplyCure and Disease::SyncMarker, which walked the co-save's
//     state map and special-cased hypothermia BY COMPARING ITS ID TO A STRING -
//     an illness being a different kind of thing should be a different type,
//     not a two-letter exception buried in a loop.
//
// Built after the records are resolved, and rebuilt if they ever are again.

namespace RSL
{
    class LesionIllness;

    class Illnesses
    {
    public:
        static Illnesses& GetSingleton();

        // Create the instances from the resolved records. Called at
        // kDataLoaded, immediately after Forms::Load, and safe to call again -
        // each illness copies the records it needs, so a second resolve has to
        // be followed by a second build.
        void Build();

        // One pass over every illness, in the order the tick used to run them:
        // the cold, the four caught by a hit, RFAB's seven, the lesions.
        void Update(const Tick& a_tick);

        // Take every one of them off the player. The master switch, and only
        // it - this is what makes the mod removable.
        void ClearAll();

        [[nodiscard]] Illness* Find(std::string_view a_id);

        [[nodiscard]] const std::vector<std::unique_ptr<Illness>>& All() const
        {
            return _all;
        }

        // The lesions are asked about from outside the tick - a bandage wants
        // to know whether there is a wound to patch - so they get a name.
        [[nodiscard]] LesionIllness* Lesion() const { return _lesion; }

    private:
        std::vector<std::unique_ptr<Illness>> _all;

        // Points into _all. Valid for as long as the vector holds the object,
        // which is the lifetime of the build.
        LesionIllness* _lesion{ nullptr };
    };
}
