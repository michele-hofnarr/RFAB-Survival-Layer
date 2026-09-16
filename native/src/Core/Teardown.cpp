#include "PCH.h"

#include "Core/Teardown.h"

#include "Core/Forms.h"

namespace RSL::Teardown
{
    namespace
    {
        // A spell can be on the player twice over: in the added-spell list, and
        // as a running active effect. Removing the spell ends its effects, but
        // not every effect of ours arrived as a spell on that list - a disease
        // applied on a hit is fire-and-forget - so both are swept.
        [[nodiscard]] std::size_t StripSpells(RE::Actor* a_player)
        {
            auto& added = a_player->GetActorRuntimeData().addedSpells;

            // Collected first, removed after. RemoveSpell edits the very array
            // being walked, and the first version of this skipped every second
            // leftover because of it.
            std::vector<RE::SpellItem*> ours;
            for (auto* spell : added) {
                if (Forms::Ours(spell)) {
                    ours.push_back(spell);
                }
            }

            std::size_t gone = 0;
            for (auto* spell : ours) {
                const char* name = spell->GetFormEditorID();
                logger::info("teardown: removing spell {:08X} {}", spell->GetFormID(),
                    (name && *name) ? name : "<no editor id>");
                if (a_player->RemoveSpell(spell)) {
                    ++gone;
                }
            }
            return gone;
        }

        [[nodiscard]] std::size_t StripEffects(RE::Actor* a_player)
        {
            auto* target = a_player->AsMagicTarget();
            if (!target) {
                return 0;
            }
            auto* effects = target->GetActiveEffectList();
            if (!effects) {
                return 0;
            }

            std::size_t gone = 0;
            for (auto* effect : *effects) {
                if (!effect) {
                    continue;
                }
                // Either half can be ours on its own: a vanilla spell can carry
                // one of our effects, and one of our spells can carry a vanilla
                // effect. Either way the form belongs to a plugin that is about
                // to disappear.
                const bool mine = Forms::Ours(effect->spell) ||
                                  Forms::Ours(effect->GetBaseObject());
                if (!mine) {
                    continue;
                }
                logger::info("teardown: dispelling effect of {:08X}",
                    effect->spell ? effect->spell->GetFormID() : 0);
                effect->Dispel(true);
                ++gone;
            }
            return gone;
        }

        // The favourites list, which is where this mod's uninstall actually
        // broke.
        //
        // MagicFavorites is a singleton of its own holding raw TESForm*, and
        // nothing connects it to whether the player still knows the spell:
        // RemoveSpell takes the lesser power off the player and the favourites
        // list goes on holding it. The save stores that list, so it comes back
        // as a form id - and once the plugin is gone that id resolves to
        // nothing. Every crash dump from this fault has MagicFavorites pinned
        // in a register while the menu walks it.
        //
        // The campfire power is the one record of ours that can be in there,
        // and it is the one the player has every reason to favourite.
        [[nodiscard]] std::size_t StripFavorites()
        {
            auto* favorites = RE::MagicFavorites::GetSingleton();
            if (!favorites) {
                return 0;
            }

            // Collected before anything is removed, for the reason StripSpells
            // gives: RemoveFavorite edits the arrays being walked. A form can
            // sit in both lists at once, hence the set.
            std::vector<RE::TESForm*>         ours;
            std::unordered_set<RE::TESForm*>  seen;
            const auto gather = [&ours, &seen](const auto& a_list) {
                for (auto* form : a_list) {
                    // hotkeys is a fixed set of slots with holes in it, so a
                    // null here is an empty slot, not a fault.
                    if (form && Forms::Ours(form) && seen.insert(form).second) {
                        ours.push_back(form);
                    }
                }
            };
            gather(favorites->spells);
            gather(favorites->hotkeys);

            for (auto* form : ours) {
                const char* name = form->GetFormEditorID();
                logger::info("teardown: unfavouriting {:08X} {}", form->GetFormID(),
                    (name && *name) ? name : "<no editor id>");
                favorites->RemoveFavorite(form);
            }
            return ours.size();
        }

        // And what the player is carrying. The bedroll is put back in the pack
        // on the way out - Bedroll::Update does it moments before this runs -
        // so the teardown itself hands the player an item that is about to
        // lose its base object. An item can be favourited too, and that
        // favourite lives on the inventory entry, so it goes with the item.
        [[nodiscard]] std::size_t StripInventory(RE::Actor* a_player)
        {
            std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> ours;
            for (const auto& [object, stack] : a_player->GetInventory()) {
                if (object && stack.first > 0 && Forms::Ours(object)) {
                    ours.emplace_back(object, stack.first);
                }
            }

            for (const auto& [object, count] : ours) {
                const char* name = object->GetFormEditorID();
                logger::info("teardown: taking {} x {:08X} {} out of the pack",
                    count, object->GetFormID(),
                    (name && *name) ? name : "<no editor id>");
                a_player->RemoveItem(object, count, RE::ITEM_REMOVE_REASON::kRemove,
                    nullptr, nullptr);
            }
            return ours.size();
        }
    }

    std::size_t StripEverythingOfOurs()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return 0;
        }

        const std::size_t spells = StripSpells(player);
        const std::size_t effects = StripEffects(player);
        const std::size_t favorites = StripFavorites();
        const std::size_t carried = StripInventory(player);
        const std::size_t total = spells + effects + favorites + carried;

        // Said out loud either way. "Nothing left" is the claim the whole
        // uninstall rests on, and it is worth being able to read it back off a
        // log rather than take on trust.
        if (total == 0) {
            logger::info("teardown: nothing of ours left on the player");
        } else {
            logger::info("teardown: swept {} spell(s), {} effect(s), "
                         "{} favourite(s) and {} carried item(s) the named "
                         "teardown could not reach",
                spells, effects, favorites, carried);
        }
        return total;
    }
}
