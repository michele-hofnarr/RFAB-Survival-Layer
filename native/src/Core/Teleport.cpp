#include "PCH.h"

#include "Core/Teleport.h"

#include "Core/Forms.h"
#include "Core/Hypothermia.h"
#include "Core/Needs.h"
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

        // Is the player cold enough that the way out is closed?
        //
        // Two states, and they are the two the player can already see: any
        // stage of hypothermia, or the cold penalty being on at all - which is
        // exactly the axis being at or under its safe mark. Nothing new to
        // explain and nothing new to tune.
        [[nodiscard]] bool ColdEnough()
        {
            if (Hypothermia::GetSingleton().Stage() >= 1) {
                return true;
            }
            return Needs::GetSingleton().ColdBase() <= Settings::fColdSafe;
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
                if (Settings::bModEnabled && Settings::bColdBlocksTeleport && a_this &&
                    a_this->GetCasterAsActor() == RE::PlayerCharacter::GetSingleton() &&
                    IsWayOut(a_spell) && ColdEnough()) {

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
                        Notify::GetSingleton().Push(Forms::msgNoTeleport);
                        logger::info("cold blocked {}",
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
