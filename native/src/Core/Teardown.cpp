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
    }

    std::size_t StripEverythingOfOurs()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return 0;
        }

        const std::size_t spells = StripSpells(player);
        const std::size_t effects = StripEffects(player);
        const std::size_t total = spells + effects;

        // Said out loud either way. "Nothing left" is the claim the whole
        // uninstall rests on, and it is worth being able to read it back off a
        // log rather than take on trust.
        if (total == 0) {
            logger::info("teardown: nothing of ours left on the player");
        } else {
            logger::info("teardown: swept {} spell(s) and {} effect(s) the named "
                         "teardown could not reach - an older build's records, "
                         "most likely",
                spells, effects);
        }
        return total;
    }
}
