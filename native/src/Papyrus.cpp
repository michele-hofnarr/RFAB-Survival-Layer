#include "PCH.h"

#include "Papyrus.h"

#include "Core/Illnesses.h"

namespace RSL::Papyrus
{
    namespace
    {
        // The script's own name, and the names it declares. Both have to match
        // _RSL_MCM.psc exactly - the generator writes that file, so a rename
        // there is a rename here.
        constexpr auto SCRIPT = "_RSL_MCM";
        constexpr auto RESET_ILLNESSES = "ResetIllnessesNative";

        void ResetIllnesses(RE::StaticFunctionTag*)
        {
            // Straight through. Everything a reset means is in one place, and
            // this is not it.
            ResetAllIllnesses();
        }

        bool Bind(RE::BSScript::IVirtualMachine* a_vm)
        {
            if (!a_vm) {
                logger::error("papyrus: no virtual machine to bind to");
                return false;
            }

            a_vm->RegisterFunction(RESET_ILLNESSES, SCRIPT, ResetIllnesses);
            logger::info("papyrus: {}.{} bound", SCRIPT, RESET_ILLNESSES);
            return true;
        }
    }

    void Install()
    {
        auto* papyrus = SKSE::GetPapyrusInterface();
        if (!papyrus || !papyrus->Register(Bind)) {
            // Not fatal: everything else runs off the tick and the engine's
            // events. What is lost is the menu button, and the log says so
            // rather than leaving a button that quietly does nothing.
            logger::error("papyrus: could not register - the menu's reset button "
                          "will not work");
            return;
        }
        logger::info("papyrus registration queued");
    }
}
