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
            // How long a FINISHED take-down stays under observation after its
            // mark. Long enough for a fast travel there and back; short enough
            // that a long session does not carry a list of them. A take-down
            // still owed has no such limit - see Watch().
            constexpr float WATCH_SECONDS = 900.0f;

            struct Pending
            {
                RE::FormID   id{ 0 };
                std::string  what;
                RE::NiPoint3 at{};

                bool disabled{ false };   // the disable has landed on it
                bool marked{ false };     // the delete mark has gone out
                bool restored{ false };   // came back from the co-save
                bool waited{ false };     // "out of reach" has been said once

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

            // Everything this mod ever puts into the world. Wider than Ours()
            // by the bedroll ITEM, which is taken down where it fell the moment
            // it becomes a camp: an item lying on the ground is an item and not
            // a piece of camp nobody knows about, so Inspect must not name it,
            // but a take-down owed on it is as real as any other.
            [[nodiscard]] bool OursToPlace(const RE::TESBoundObject* a_base)
            {
                return Ours(a_base) || (a_base && a_base == Forms::bedrollItem);
            }

            [[nodiscard]] bool Queued(RE::FormID a_id)
            {
                return std::any_of(pending.begin(), pending.end(),
                    [a_id](const Pending& a_entry) { return a_entry.id == a_id; });
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

            // One pass over one take-down. False when the entry is finished
            // with, either way, and can leave the queue.
            bool Step(Pending& a_entry)
            {
                auto* ref = Deref(a_entry.id);

                if (!ref) {
                    if (a_entry.disabled) {
                        // Out of the form table after the disable landed: the
                        // engine collecting it, which is the point. Before the
                        // mark it is worth saying, because then the disable
                        // took the reference with it and there was nothing left
                        // to mark.
                        logger::info("takedown: {} {:08X} {}", a_entry.what, a_entry.id,
                            a_entry.marked ? "is gone" : "went before the mark");
                        return false;
                    }

                    // Never reached at all: its cell is not loaded, and neither
                    // Disable nor Delete processes on a reference the engine
                    // does not currently have. So it waits. Said once - this
                    // runs every frame and the wait can be an hour of play.
                    if (!a_entry.waited) {
                        a_entry.waited = true;
                        logger::info("takedown: {} {:08X} is out of reach, queued",
                            a_entry.what, a_entry.id);
                    }
                    return true;
                }

                if (!a_entry.disabled) {
                    // An entry that has come back from a co-save has carried a
                    // bare number across a save, and dynamic ids are reused.
                    // Only ever take down camp of ours: whatever else now
                    // answers to that number belongs to somebody else.
                    if (a_entry.restored && !OursToPlace(ref->GetBaseObject())) {
                        logger::warn("takedown: {} {:08X} is no longer ours - let go",
                            a_entry.what, a_entry.id);
                        return false;
                    }

                    a_entry.at = ref->GetPosition();
                    if (!ref->IsDisabled()) {
                        ref->Disable();
                    }

                    // Read back rather than assumed. If the flag did not take,
                    // the next pass tries again.
                    a_entry.disabled = ref->IsDisabled();
                    a_entry.waited = false;

                    logger::info("takedown: {} {:08X} at {:.0f},{:.0f},{:.0f} going "
                                 "down - disabled {}, 3D {}",
                        a_entry.what, a_entry.id, a_entry.at.x, a_entry.at.y,
                        a_entry.at.z, ref->IsDisabled(), ref->Is3DLoaded());
                    return true;
                }

                if (!a_entry.marked) {
                    if (!IsDown(ref)) {
                        // Still on its way out. This is the wait Papyrus does
                        // inside Disable, and it is the reason this file
                        // exists; come back next pass.
                        return true;
                    }

                    ref->SetDelete(true);
                    a_entry.marked = true;
                    logger::info("takedown: {} {:08X} is down, marked - deleted {}",
                        a_entry.what, a_entry.id, ref->IsDeleted());
                    return true;
                }

                // Marked and still here. Says so once anything about it moves.
                if (!ref->IsDisabled() || !ref->IsDeleted()) {
                    const auto at = ref->GetPosition();
                    logger::warn("takedown: {} {:08X} changed under us - "
                                 "disabled {}, deleted {}, 3D {}, "
                                 "at {:.0f},{:.0f},{:.0f} (was {:.0f},{:.0f},{:.0f})",
                        a_entry.what, a_entry.id, ref->IsDisabled(), ref->IsDeleted(),
                        ref->Is3DLoaded(), at.x, at.y, at.z, a_entry.at.x, a_entry.at.y,
                        a_entry.at.z);
                    return false;
                }
                return true;
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

        void Now(RE::FormID a_id, std::string_view a_what)
        {
            if (!a_id) {
                return;
            }

            std::erase_if(pending,
                [a_id](const Pending& a_entry) { return a_entry.id == a_id; });

            pending.push_back(Pending{ a_id, std::string(a_what) });
            pending.back().when = std::chrono::steady_clock::now();

            // One attempt this instant, so a reference in hand goes down now
            // and the queue is only ever what happens when it is not.
            if (!Step(pending.back())) {
                pending.pop_back();
            }
        }

        void Now(RE::TESObjectREFR* a_ref, std::string_view a_what)
        {
            if (a_ref) {
                Now(a_ref->GetFormID(), a_what);
            }
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
                if (!Step(entry)) {
                    entry.id = 0;
                }
            }

            std::erase_if(pending, [&](const Pending& a_entry) {
                if (a_entry.id == 0) {
                    return true;
                }
                // Only a FINISHED take-down ages out - past the mark it is
                // being watched, not worked on. One still owed stays for as
                // long as it takes, which may be until the player next walks
                // into that cell: a timer on it would leave the fire burning
                // for ever, which is the bug this queue is for.
                return a_entry.marked &&
                       std::chrono::duration<float>(now - a_entry.when).count() >
                           WATCH_SECONDS;
            });
        }

        std::vector<Entry> Outstanding()
        {
            std::vector<Entry> out;
            for (const auto& entry : pending) {
                // Past the mark nothing is owed: the reference is deleted and
                // will not be in the save to come back to.
                if (!entry.marked) {
                    out.push_back(Entry{ entry.id, entry.what });
                }
            }
            return out;
        }

        void Restore(RE::FormID a_id, std::string_view a_what)
        {
            if (!a_id) {
                return;
            }
            pending.push_back(Pending{ a_id, std::string(a_what) });
            pending.back().restored = true;
            pending.back().when = std::chrono::steady_clock::now();
        }

        void Forget()
        {
            pending.clear();
            named.clear();
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
            if (Tracked(id) || Queued(id)) {
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
