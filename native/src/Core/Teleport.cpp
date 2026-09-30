#include "PCH.h"

#include "Core/Teleport.h"

#include "Core/Forms.h"
#include "Core/Hypothermia.h"
#include "Core/Notify.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // CheckCast runs while a spell is readied, not once per press, so the
        // refusal has to be told only now and then or it would fill the corner.
        constexpr float TOLD_EVERY = 2.0f;

        std::chrono::steady_clock::time_point g_lastTold{};

        // Is the way out closed, and what closed it? The message to show, or
        // null when nothing does.
        //
        // Two states, and both are status effects the player can see: any
        // stage of hypothermia, or the Cold penalty on them. Each has its own
        // line, so the refusal names the thing the player can look up.
        // Hypothermia wins when both are on: it is the graver of the two, and
        // it outlasts Cold - a player who has warmed back past the penalty can
        // still be carrying a stage of it.
        //
        // THE ABILITY ITSELF, NOT THE BAR. This used to test the cold axis
        // against fColdSafe - the threshold Penalties puts Cold on at, so the
        // two agreed in practice, but it was a second copy of the rule, and
        // the rule is the status effect. Whatever decides when Cold goes on -
        // the threshold, the rounding, anything added later - now decides this
        // too, because it is the same question.
        [[nodiscard]] RE::BGSMessage* BlockedBy()
        {
            if (Hypothermia::GetSingleton().Stage() >= 1) {
                return Forms::msgHypoNoTeleport;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && Forms::abCold && player->HasSpell(Forms::abCold)) {
                return Forms::msgNoTeleport;
            }
            return nullptr;
        }

        [[nodiscard]] bool IsWayOut(RE::MagicItem* a_spell)
        {
            if (!a_spell) {
                return false;
            }
            for (const auto* effect : a_spell->effects) {
                const auto* base = effect ? effect->baseEffect : nullptr;
                if (!base) {
                    continue;
                }
                if (base == Forms::mgefTeleport || base == Forms::mgefMarkRecall) {
                    return true;
                }
            }
            return false;
        }

        struct CheckCastHook
        {
            static bool Thunk(RE::MagicCaster* a_this, RE::MagicItem* a_spell,
                bool a_dualCast, float* a_alchStrength,
                RE::MagicSystem::CannotCastReason* a_reason, bool a_useBaseValueForCost)
            {
                RE::BGSMessage* why = nullptr;
                if (Settings::bModEnabled && Settings::bColdBlocksTeleport && a_this &&
                    a_this->GetCasterAsActor() == RE::PlayerCharacter::GetSingleton() &&
                    IsWayOut(a_spell) && (why = BlockedBy()) != nullptr) {

                    // kOK, and false anyway. The reason is what the HUD turns
                    // into its own line ("not enough magicka" and the like), and
                    // none of them is true here - our own message says what is
                    // true. If the engine prints something over the top of it,
                    // this is the value to change.
                    if (a_reason) {
                        *a_reason = RE::MagicSystem::CannotCastReason::kOK;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration<float>(now - g_lastTold).count() >= TOLD_EVERY) {
                        g_lastTold = now;
                        Notify::GetSingleton().Push(why);
                        logger::info("{} blocked {}",
                            why == Forms::msgHypoNoTeleport ? "hypothermia" : "cold",
                            a_spell->GetFullName() ? a_spell->GetFullName() : "a teleport");
                    }
                    return false;
                }

                return _Thunk(a_this, a_spell, a_dualCast, a_alchStrength, a_reason,
                    a_useBaseValueForCost);
            }

            static inline REL::Relocation<decltype(Thunk)> _Thunk;
        };
    }

    void Teleport::Install()
    {
        if (!Forms::mgefTeleport && !Forms::mgefMarkRecall) {
            logger::warn("neither teleport effect resolved - the cold does not block it");
            return;
        }

        // Index 0x0A of the MagicCaster vtable. ActorMagicCaster has three
        // vtables because of its bases; [0] is the MagicCaster one, which is
        // where CheckCast lives.
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ActorMagicCaster[0] };
        CheckCastHook::_Thunk = vtbl.write_vfunc(0x0A, CheckCastHook::Thunk);
        logger::info("cold blocks teleportation (hooked CheckCast)");
    }
}
