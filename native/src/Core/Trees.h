#pragma once

// Trees you can chop, through the engine's own harvest.
//
// A TESObjectTREE already carries TESProduceForm - the same component that
// makes a snowberry bush pickable. Comparing the two kinds of record in
// Skyrim.esm says exactly what a forest tree is missing:
//
//     harvestable:  EDID OBND MODL MODT  PFIG SNAM  PFPC  FULL  CNAM
//     forest tree:  EDID OBND MODL MODT            PFPC        CNAM
//
// All 32 harvestable TREE records have PFIG, SNAM and FULL; none of the 122
// forest trees has any of them. PFIG is the produce, SNAM the harvest sound -
// and FULL is the name. A nameless object has no activation prompt to show, so
// filling in the produce alone was never going to be enough: the first attempt
// did that and the trees stayed as mute as ever.
//
// So all three are filled in at runtime, and nothing is written to disk - RFAB
// can be updated under this without any of it noticing.
//
// What is NOT taken from the engine is the regrowth. The vanilla harvested flag
// clears when the cell resets, and this pack sets that to three in-game years
// by design, which would make every tree a one-off. The flag is a record flag
// on the reference, so the mod clears it itself once fTreeChopCooldownHours
// have passed and the cell's timer never enters into it.
//
// The produce is offered only to someone with Survival Basics AND either a
// wood axe or the adventurer's backpack. v0.4.0 let the axe alone do it, and
// that was ported as it stood until the author put the obvious question: the
// campfire needs the perk, so wood you cannot light is wood you have no use
// for. The perk gates camp life, and firewood is part of camp life.
//
// Withdrawing the produce takes the prompt away too, which says "not with your
// bare hands" better than offering the option and then refusing it. NPCs do not
// harvest trees, so gating the base form on the player's own kit is enough.

namespace RSL
{
    class Trees
    {
    public:
        static Trees& GetSingleton();

        // Collect the tree bases. Called once, after the forms are resolved.
        void Install();

        // Offer or withdraw the produce as the player's kit changes, and let
        // cut trees come back when their time is up.
        void Update();

        // True if this base is one we made choppable.
        [[nodiscard]] bool IsOurs(const RE::TESBoundObject* a_base) const;

        // The player took one. Stamps the tree and tops the yield up.
        void OnHarvested(RE::TESObjectREFR* a_ref);

        // The survival perk, plus an axe or the backpack.
        [[nodiscard]] bool CanChop() const;

        // Which trees were cut and when, in game days. Lives in the co-save.
        [[nodiscard]] auto& Table() { return _cut; }
        void                Reset();

    private:
        void SetOffered(bool a_on);
        void ExpireCooldowns();
        void AdoptHarvestedNearby();

        std::vector<RE::TESObjectTREE*>       _trees;
        std::unordered_set<RE::FormID>        _bases;
        std::unordered_map<RE::FormID, float> _cut;

        // Borrowed from a tree that is already harvestable, so the sound is the
        // one the game uses for this rather than one picked by hand.
        RE::BGSSoundDescriptorForm* _harvestSound{ nullptr };

        bool _installed{ false };
        bool _offered{ false };

        std::chrono::steady_clock::time_point _swept{};
    };
}
