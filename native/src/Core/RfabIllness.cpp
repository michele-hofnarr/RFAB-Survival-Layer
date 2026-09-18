#include "PCH.h"

#include "Core/RfabIllness.h"

#include "Core/Player.h"
#include "Settings.h"

#include <spdlog/fmt/fmt.h>

#include <vector>

namespace RSL
{
    namespace
    {
        // Papyrus has Actor.DispelSpell and CommonLibSSE does not bind it, so
        // this is it: end every running effect that came from this spell, and
        // report whether anything was actually there.
        //
        // That return value is not incidental. At stage 2 or 3 our own copy
        // carries RFAB's effect names, so "is RFAB's disease on me" cannot be
        // answered by looking; the only reliable answer is whether dispelling
        // their spell removed something.
        //
        // Collected first and dispelled after, because Dispel mutates the list
        // being walked.
        [[nodiscard]] bool DispelSpell(RE::Actor* a_actor, RE::MagicItem* a_spell)
        {
            auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
            auto* effects = target ? target->GetActiveEffectList() : nullptr;
            if (!a_spell || !effects) {
                return false;
            }

            std::vector<RE::ActiveEffect*> doomed;
            for (auto* active : *effects) {
                if (active && active->spell == a_spell) {
                    doomed.push_back(active);
                }
            }
            for (auto* active : doomed) {
                active->Dispel(true);
            }
            return !doomed.empty();
        }
    }

    DiseaseForms RfabIllness::AsCommon(const RfabDiseaseForms& a_src)
    {
        DiseaseForms out{};
        out.id = a_src.id;

        out.stage[0] = a_src.base;      // RFAB's own record, theirs and untouched
        out.stage[1] = a_src.ours[0];
        out.stage[2] = a_src.ours[1];

        // msg[0] stays null: stage 1 is RFAB's disease and it announces itself.
        // Announce() below skips it rather than pushing an empty message.
        out.msg[1] = a_src.msg[0];      // worse
        out.msg[2] = a_src.msg[1];      // worst

        out.cured = a_src.cured;
        out.ease[0] = a_src.ease[0];
        out.ease[1] = a_src.ease[1];
        return out;
    }

    RfabIllness::RfabIllness(const RfabDiseaseForms& a_src, bool a_blessingFreezes) :
        Illness(AsCommon(a_src)),
        _src(a_src),
        _blessingFreezes(a_blessingFreezes)
    {}

    bool RfabIllness::Ready() const
    {
        return _src.base != nullptr;
    }

    bool RfabIllness::Enabled() const
    {
        return Settings::bModEnabled && Settings::bRfabDiseasesEnabled;
    }

    void RfabIllness::Clear()
    {
        auto*      player = Player();
        const bool wasIll = Stage() > 0;

        // Ours, and only ours - note the nullptr where their record would go.
        Diseases().ClearStages(Id(), nullptr, _src.ours[0], _src.ours[1]);

        if (wasIll && player && _src.base && !player->HasSpell(_src.base)) {
            player->AddSpell(_src.base);
            logger::info("dz {}: handed back to RFAB at its own stage 1", Id());
        }
    }

    void RfabIllness::Observe(const Tick&, std::int32_t a_stage)
    {
        auto* player = Player();
        if (!player) {
            return;
        }

        // Our stage 2/3 copies carry RFAB's marker effect too, so the probe
        // below only means anything while neither of them is on. Without this
        // "afflicted" reads true at every stage, for ever.
        _ourCopyOn = (_src.ours[0] && player->HasSpell(_src.ours[0])) ||
                     (_src.ours[1] && player->HasSpell(_src.ours[1]));

        _afflicted = player->HasSpell(_src.base) ||
                     (_src.mark && !_ourCopyOn &&
                         player->AsMagicTarget()->HasMagicEffect(_src.mark));

        // At stage 2 or 3 a fresh bite lays RFAB's own effects on top of our
        // copy. Dispelling the base removes those and leaves ours alone.
        if (a_stage >= 2 && DispelSpell(player, _src.base)) {
            logger::info("dz {}: dispelled a stray RFAB instance at stage {}", Id(),
                a_stage);
        }
    }

    bool RfabIllness::Contracts(const Tick&)
    {
        // Catching it means adopting the engine-applied disease into the spell
        // list. The marker can linger a tick or two after a cure, so it has to
        // go quiet once before a fresh bite counts - otherwise a cure loops
        // straight back into stage 1.
        if (!_afflicted) {
            _seenClean = true;
            return false;
        }
        if (!_seenClean) {
            return false;
        }

        _seenClean = false;
        logger::info("dz {}: contracted, RFAB's own record adopted", Id());
        return true;
    }

    bool RfabIllness::StageSpellGone(std::int32_t a_stage) const
    {
        // A cure strips whichever Type=Disease spell is currently on. What
        // tells us differs by stage: at 1 the marker goes quiet, at 2 or 3 our
        // own copy vanishes from the spell list like anyone else's.
        if (a_stage == 1) {
            return !_afflicted;
        }
        return Illness::StageSpellGone(a_stage);
    }

    bool RfabIllness::Held(std::int32_t a_stage)
    {
        auto* player = Player();
        if (!_blessingFreezes || a_stage != 1 || !Forms::peryiteBlessing || !player ||
            !player->HasSpell(Forms::peryiteBlessing)) {
            return false;
        }

        if (!_afflicted) {
            player->AddSpell(_src.base);   // a stray cure stripped it
        }
        Diseases().ResetP(Id());
        Diseases().TakeCures(Id());        // discarded: frozen means frozen
        return true;
    }

    void RfabIllness::Announce(std::int32_t a_stage, std::int32_t a_old)
    {
        // Arriving at stage 1 is RFAB's own disease being caught, and their
        // record has already said so. Saying it twice would be this layer
        // taking credit for their illness.
        if (a_stage == 1 && a_stage > a_old) {
            return;
        }
        Illness::Announce(a_stage, a_old);
    }

    void RfabIllness::OnStageChanged(std::int32_t, std::int32_t a_stage)
    {
        if (a_stage == 0) {
            // Cleared entirely, RFAB's own stage included.
            (void)DispelSpell(Player(), _src.base);
        }
    }

    std::string RfabIllness::TraceExtra(const Tick&) const
    {
        return fmt::format("afflicted={} ourCopy={} seenClean={}", _afflicted, _ourCopyOn,
            _seenClean);
    }
}
