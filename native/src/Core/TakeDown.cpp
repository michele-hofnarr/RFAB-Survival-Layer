#include "PCH.h"

#include "Core/TakeDown.h"

#include "Core/Bedroll.h"
#include "Core/Campfire.h"
#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace TakeDown
    {
        namespace
        {
            // How long a reference stays under observation after its mark. Long
            // enough for a fast travel there and back; short enough that a long
            // session does not carry a list of them.
            constexpr float WATCH_SECONDS = 900.0f;

            struct Pending
            {
                RE::FormID   id{ 0 };
                std::string  what;
                RE::NiPoint3 at{};
                bool         marked{ false };   // the delete mark has gone out

                std::chrono::steady_clock::time_point when{};
            };

            // Everything here runs on the game thread: the take-downs arrive
            // through the task interface and the watch comes off the tick.
            std::vector<Pending> pending;

            // One stray line per reference, however many times it is pushed on.
            std::vector<RE::FormID> named;

            [[nodiscard]] RE::TESObjectREFR* Deref(RE::FormID a_id)
            {
                return a_id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
            }

            // Is the reference down? This is the condition the latent Disable
            // waits on before it lets Delete run: switched off, and out of the
            // scene.
            [[nodiscard]] bool IsDown(const RE::TESObjectREFR* a_ref)
            {
                return a_ref->IsDisabled() && !a_ref->Is3DLoaded();
            }

            [[nodiscard]] bool Ours(const RE::TESBoundObject* a_base)
            {
                return a_base &&
                       (a_base == Forms::bedrollFurn || a_base == Forms::baseTent ||
                           a_base == Forms::campfireLit || a_base == Forms::baseCampfire ||
                           a_base == Forms::baseCookSpit || a_base == Forms::baseCookPot);
            }

            [[nodiscard]] bool Tracked(RE::FormID a_id)
            {
                const auto& fire = Campfire::GetSingleton().Data();
                if (a_id == fire.fire || a_id == fire.spit || a_id == fire.pot) {
                    return true;
                }
                for (const auto& [furniture, tent] : Bedroll::GetSingleton().Camps()) {
                    if (a_id == furniture || a_id == tent) {
                        return true;
                    }
                }
                return false;
            }
        }

        void Hide(RE::TESObjectREFR* a_ref, std::string_view a_what)
        {
            if (!a_ref || a_ref->IsDisabled()) {
                return;
            }

            a_ref->Disable();

            if (Settings::bDebugLog) {
                logger::info("takedown: hid {} {:08X} - disabled {}, 3D {}", a_what,
                    a_ref->GetFormID(), a_ref->IsDisabled(), a_ref->Is3DLoaded());
            }
        }

        void Now(RE::TESObjectREFR* a_ref, std::string_view a_what)
        {
            if (!a_ref) {
                return;
            }

            const auto id = a_ref->GetFormID();
            const auto at = a_ref->GetPosition();

            // The disable, and nothing else in this breath. The mark waits for
            // it to land - see the note in the header.
            if (!a_ref->IsDisabled()) {
                a_ref->Disable();
            }

            std::erase_if(pending, [id](const Pending& a_entry) { return a_entry.id == id; });
            pending.push_back(Pending{ id, std::string(a_what), at, false,
                std::chrono::steady_clock::now() });

            logger::info("takedown: {} {:08X} at {:.0f},{:.0f},{:.0f} going down - "
                         "disabled {}, 3D {}",
                a_what, id, at.x, at.y, at.z, a_ref->IsDisabled(), a_ref->Is3DLoaded());
        }

        void Watch()
        {
            if (pending.empty()) {
                return;
            }

            // Not while a load is on screen. The rest of the tick refuses to
            // touch anything then and this is no different: a reference caught
            // mid-transition is exactly what this code exists to stop doing.
            if (auto* ui = RE::UI::GetSingleton();
                ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();

            for (auto& entry : pending) {
                auto* ref = Deref(entry.id);

                if (!ref) {
                    // Out of the form table. After the mark that is the engine
                    // collecting it, which is the whole point; before the mark
                    // it is worth saying, because then the disable took the
                    // reference with it and there was nothing left to mark.
                    logger::info("takedown: {} {:08X} {}", entry.what, entry.id,
                        entry.marked ? "is gone" : "went before the mark");
                    entry.id = 0;
                    continue;
                }

                if (!entry.marked) {
                    if (!IsDown(ref)) {
                        // Still on its way out. This is the wait Papyrus does
                        // inside Disable, and it is the reason this file
                        // exists; come back next pass.
                        continue;
                    }

                    ref->SetDelete(true);
                    entry.marked = true;
                    logger::info("takedown: {} {:08X} is down, marked - deleted {}",
                        entry.what, entry.id, ref->IsDeleted());
                    continue;
                }

                // Marked and still here. Says so once anything about it moves.
                if (!ref->IsDisabled() || !ref->IsDeleted()) {
                    const auto at = ref->GetPosition();
                    logger::warn("takedown: {} {:08X} changed under us - "
                                 "disabled {}, deleted {}, 3D {}, "
                                 "at {:.0f},{:.0f},{:.0f} (was {:.0f},{:.0f},{:.0f})",
                        entry.what, entry.id, ref->IsDisabled(), ref->IsDeleted(),
                        ref->Is3DLoaded(), at.x, at.y, at.z, entry.at.x, entry.at.y,
                        entry.at.z);
                    entry.id = 0;
                }
            }

            std::erase_if(pending, [&](const Pending& a_entry) {
                return a_entry.id == 0 ||
                       std::chrono::duration<float>(now - a_entry.when).count() >
                           WATCH_SECONDS;
            });
        }

        void Inspect(const RE::TESObjectREFR* a_ref)
        {
            if (!a_ref) {
                return;
            }
            auto* base = a_ref->GetBaseObject();
            if (!Ours(base)) {
                return;
            }

            const auto id = a_ref->GetFormID();
            if (Tracked(id)) {
                return;
            }
            if (std::find(named.begin(), named.end(), id) != named.end()) {
                return;
            }
            named.push_back(id);

            const auto  at = a_ref->GetPosition();
            const char* edid = base->GetFormEditorID();
            logger::warn("takedown: stray {} {:08X} of base [{:08X}] {} "
                         "at {:.0f},{:.0f},{:.0f} - dynamic {}, disabled {}, deleted {}",
                (edid && *edid) ? edid : "<no editor id>", id, base->GetFormID(),
                RE::FormTypeToString(base->GetFormType()), at.x, at.y, at.z,
                a_ref->IsDynamicForm(), a_ref->IsDisabled(), a_ref->IsDeleted());
        }
    }
}
