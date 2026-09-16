#include "PCH.h"

#include "Core/Trees.h"

#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // How often the sweep runs: expiring cooldowns and noticing a tree that
        // was taken without an activation event reaching us.
        constexpr float SWEEP_SECONDS = 3.0f;

        // How far out to look for trees that were harvested behind our back.
        constexpr float ADOPT_RADIUS = 1500.0f;

        // The harvested bit lives on the reference's record flags, and
        // CommonLibSSE labels it for TESObjectTREE specifically.
        constexpr std::uint32_t HARVESTED = RE::TESObjectREFR::RecordFlags::kHarvested;

        [[nodiscard]] bool IsHarvested(const RE::TESObjectREFR* a_ref)
        {
            return a_ref && (a_ref->GetFormFlags() & HARVESTED) != 0;
        }

        // Flip the bit and tell the engine, which is what SetDelete and the
        // rest of the flag setters do - without AddChange the save does not
        // learn about it.
        void SetHarvested(RE::TESObjectREFR* a_ref, bool a_on)
        {
            if (!a_ref || IsHarvested(a_ref) == a_on) {
                return;
            }
            if (a_on) {
                a_ref->formFlags |= HARVESTED;
            } else {
                a_ref->formFlags &= ~HARVESTED;
            }
            a_ref->AddChange(1);
        }

        [[nodiscard]] std::string Lower(std::string_view a_in)
        {
            std::string out{ a_in };
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        // A real tree, as opposed to the flora that shares the record type.
        //
        // An empty produceItem is the whole test that matters: everything
        // pickable already has one, and filling one in is exactly what we are
        // about to do - so anything that has one is somebody else's and is left
        // alone. The name check on top of it keeps us off the handful of
        // TESObjectTREE records that are neither, like seaweed.
        [[nodiscard]] bool LooksChoppable(RE::TESObjectTREE* a_tree)
        {
            if (!a_tree || a_tree->produceItem) {
                return false;
            }
            const char*       edid = a_tree->GetFormEditorID();
            const std::string name = Lower(edid ? edid : "");
            const std::string model = Lower(a_tree->GetModel() ? a_tree->GetModel() : "");
            return name.find("tree") != std::string::npos ||
                   model.find("tree") != std::string::npos;
        }

        [[nodiscard]] float Now()
        {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }
    }

    Trees& Trees::GetSingleton()
    {
        static Trees singleton;
        return singleton;
    }

    void Trees::Install()
    {
        if (_installed) {
            return;
        }
        _installed = true;

        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            logger::error("trees: no data handler");
            return;
        }

        for (auto* tree : handler->GetFormArray<RE::TESObjectTREE>()) {
            if (!tree) {
                continue;
            }
            if (tree->produceItem) {
                // Already harvestable, so it belongs to somebody else - and it
                // is where the harvest sound comes from, rather than a sound
                // chosen by hand.
                if (!_harvestSound && tree->harvestSound) {
                    _harvestSound = tree->harvestSound;
                }
                continue;
            }
            if (LooksChoppable(tree)) {
                _trees.push_back(tree);
                _bases.insert(tree->GetFormID());
            }
        }

        logger::info("trees: {} bases can be made choppable, harvest sound {}",
            _trees.size(), _harvestSound ? "borrowed" : "not found");
    }

    bool Trees::IsOurs(const RE::TESBoundObject* a_base) const
    {
        return a_base && _bases.contains(a_base->GetFormID());
    }

    void Trees::SetOffered(bool a_on)
    {
        if (_offered == a_on || !Forms::firewood) {
            return;
        }
        _offered = a_on;

        // The name matters as much as the produce. An object with no FULL has
        // nothing to put in an activation prompt, and Skyrim will not offer one
        // - which is why the first attempt, that set the produce and nothing
        // else, left every tree as mute as before. Every harvestable TREE in
        // Skyrim.esm carries FULL; not one forest tree does.
        //
        // The name is the firewood's own, so it is already translated and
        // already says what taking it gives.
        const char* name = Forms::firewood ? Forms::firewood->GetName() : nullptr;

        for (auto* tree : _trees) {
            if (!tree) {
                continue;
            }
            tree->produceItem = a_on ? Forms::firewood : nullptr;
            tree->harvestSound = a_on ? _harvestSound : nullptr;
            // Every season, always. A tree does not stop having wood in winter.
            for (auto& chance : tree->produceChance) {
                chance = a_on ? 100 : 0;
            }
            if (a_on) {
                if (name) {
                    tree->fullName = name;
                }
            } else {
                tree->fullName = "";
            }
        }

        logger::info("trees: firewood {} on {} bases (named \"{}\")",
            a_on ? "offered" : "withdrawn", _trees.size(), name ? name : "");
    }

    bool Trees::CanChop() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        // The perk first, for the axe as well as for the backpack.
        //
        // v0.4.0 let an axe alone do it and this was ported as it stood, but
        // the design does not survive the question the author asked of it: the
        // campfire needs Survival Basics, so wood you cannot light is wood
        // you have no use for. The perk is the gate on the whole of camp life,
        // and firewood is part of camp life.
        if (!Forms::perkSurvivalBasics || !player->HasPerk(Forms::perkSurvivalBasics)) {
            return false;
        }

        return (Forms::woodAxe && player->GetItemCount(Forms::woodAxe) > 0) ||
               (Forms::backpack && player->GetItemCount(Forms::backpack) > 0);
    }

    void Trees::OnHarvested(RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return;
        }

        _cut[a_ref->GetFormID()] = Now();

        // How much wood comes off the tree is not ours to decide.
        //
        // The engine hands over the produce, and RFAB's Survival Basics
        // doubles what any harvest gives - that is what the perk does, across
        // every ingredient in the game, and a tree is now just another harvest.
        // Topping the amount up here fought that: the setting and the perk were
        // two answers to one question, and the player got both.
        logger::info("trees: cut {:08X}", a_ref->GetFormID());
    }

    void Trees::ExpireCooldowns()
    {
        if (_cut.empty()) {
            return;
        }

        const float today = Now();
        const float rested = Settings::fTreeChopCooldownHours / 24.0f;

        for (auto it = _cut.begin(); it != _cut.end();) {
            // A clock that went backwards - an older save loaded - would strand
            // the entry forever, so that counts as due as well.
            if (today - it->second >= rested || today < it->second) {
                if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->first)) {
                    SetHarvested(ref, false);
                }
                it = _cut.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Trees::AdoptHarvestedNearby()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes || !player->Is3DLoaded()) {
            return;
        }

        // Belt and braces. The activation event is what normally stamps a tree,
        // but if one is ever taken without that reaching us, the vanilla flag
        // would stay set until the cell resets - three years away here. Any
        // harvested tree of ours that we have no stamp for gets one now, so the
        // cooldown is always the one we control.
        tes->ForEachReferenceInRange(player, ADOPT_RADIUS,
            [&](RE::TESObjectREFR& a_ref) {
                if (IsHarvested(&a_ref) && IsOurs(a_ref.GetBaseObject()) &&
                    !_cut.contains(a_ref.GetFormID())) {
                    _cut[a_ref.GetFormID()] = Now();
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
    }

    void Trees::Update()
    {
        if (!_installed) {
            return;
        }

        // Offered only to someone carrying the means. Withdrawing it also takes
        // the prompt away, which is the honest way to say "not with your bare
        // hands" - better than offering the option and refusing it.
        const bool want = Settings::bModEnabled && CanChop();
        SetOffered(want);

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - _swept).count() < SWEEP_SECONDS) {
            return;
        }
        _swept = now;

        ExpireCooldowns();
        if (_offered) {
            AdoptHarvestedNearby();
        }
    }

    void Trees::Reset()
    {
        _cut.clear();
    }
}
