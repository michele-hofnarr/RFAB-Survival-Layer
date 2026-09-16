#include "PCH.h"

#include "Core/Elemental.h"

#include "Core/Disease.h"
#include "Core/Campfire.h"
#include "Core/ElemLesion.h"
#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        enum class Element
        {
            kNone,
            kFrost,
            kFire,
            kShock
        };

        // Vanilla keyword first; then, for a detrimental effect, the resistance
        // actor value on the effect itself. RFAB's destruction effects can drop
        // the MagicDamage* keyword and still resist to the element, so the
        // keyword alone would miss them.
        [[nodiscard]] Element KindOf(const RE::EffectSetting* a_effect)
        {
            if (!a_effect) {
                return Element::kNone;
            }

            auto* effect = const_cast<RE::EffectSetting*>(a_effect);
            if (effect->HasKeywordString("MagicDamageFrost"sv)) {
                return Element::kFrost;
            }
            if (effect->HasKeywordString("MagicDamageFire"sv)) {
                return Element::kFire;
            }
            if (effect->HasKeywordString("MagicDamageShock"sv)) {
                return Element::kShock;
            }

            if (a_effect->IsDetrimental()) {
                switch (a_effect->data.resistVariable) {
                case RE::ActorValue::kResistFrost:
                    return Element::kFrost;
                case RE::ActorValue::kResistFire:
                    return Element::kFire;
                case RE::ActorValue::kResistShock:
                    return Element::kShock;
                default:
                    break;
                }
            }
            return Element::kNone;
        }

        // Which way an element pushes the cold bar, and which resistance
        // softens it. Both were switch statements inside the event handler;
        // Tick() needs the same answers, so they live on their own.
        [[nodiscard]] float SignOf(Element a_kind)
        {
            switch (a_kind) {
            case Element::kFrost:  return -1.0f;   // frost chills
            case Element::kFire:   return 1.0f;    // fire warms, and costs health for it
            default:               return 0.0f;    // shock never touches the bar
            }
        }

        [[nodiscard]] RE::ActorValue ResistOf(Element a_kind)
        {
            switch (a_kind) {
            case Element::kFrost: return RE::ActorValue::kResistFrost;
            case Element::kFire:  return RE::ActorValue::kResistFire;
            default:              return RE::ActorValue::kResistShock;
            }
        }

        // Resistance scales the hit down, and past 100% it stops scaling: a
        // negative factor would turn a resisted hit into its opposite.
        [[nodiscard]] float ResistFactor(RE::ActorValue a_av)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* owner = player ? player->AsActorValueOwner() : nullptr;
            if (!owner) {
                return 1.0f;
            }
            return std::clamp(1.0f - owner->GetActorValue(a_av) * 0.01f, 0.0f, 2.0f);
        }

        // The pool the hit is measured against: the player's maximum health as
        // it stands, not the number on his character sheet. Fortify effects
        // move it, and so do this mod's own penalties, and a hit is only ever
        // "half of what I have" relative to what he actually has.
        [[nodiscard]] float MaxHealth()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* owner = player ? player->AsActorValueOwner() : nullptr;
            return owner ? owner->GetPermanentActorValue(RE::ActorValue::kHealth) : 0.0f;
        }

        // What the engine settled on for this effect, on this player.
        //
        // This is the whole point of reading it rather than using a flat number
        // per hit: a stronger spell IS a bigger number here, and resistances
        // have already been taken off it. A ward that repels the spell is the
        // clearest case of all - the effect is never added to the player, so
        // there is nothing here to find and nothing reaches the bar.
        [[nodiscard]] float LandedMagnitude(const RE::EffectSetting* a_effect)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            // Through MagicTarget: PlayerCharacter inherits it twice over and
            // the name is ambiguous without saying which - the same reason
            // Hypothermia reaches the list this way.
            auto* target = player ? player->AsMagicTarget() : nullptr;
            auto* list = target ? target->GetActiveEffectList() : nullptr;
            if (!list) {
                return 0.0f;
            }

            float worst = 0.0f;
            for (auto* active : *list) {
                if (!active || active->GetBaseObject() != a_effect) {
                    continue;
                }
                if (active->flags.any(RE::ActiveEffect::Flag::kInactive,
                        RE::ActiveEffect::Flag::kDispelled)) {
                    continue;
                }
                worst = std::max(worst, std::abs(active->magnitude));
            }
            return worst;
        }

        class ApplySink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
        {
        public:
            static ApplySink* GetSingleton()
            {
                static ApplySink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESMagicEffectApplyEvent*                a_event,
                RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override;

        private:
            ApplySink() = default;
        };
    }

    Elemental& Elemental::GetSingleton()
    {
        static Elemental singleton;
        return singleton;
    }

    void Elemental::Install()
    {
        if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
            source->AddEventSink<RE::TESMagicEffectApplyEvent>(ApplySink::GetSingleton());
        }
    }

    float Elemental::TakeLesionP()
    {
        return _lesionP.exchange(0.0f);
    }

    void Elemental::NoteBandage()
    {
        if (!Settings::bModEnabled || !Settings::bElemLesionEnabled) {
            return;
        }

        // Nothing to patch. The credit is dropped rather than banked: banking
        // it would let a player eat bandages while unhurt and walk into a
        // blizzard with a counter that cannot be driven down.
        if (!ElemLesion::GetSingleton().Wounded()) {
            logger::info("bandage used, but there is no wound to patch");
            return;
        }

        // Worth exactly what a dose of medicine is worth to an illness. It had
        // a setting of its own that happened to carry the same number, which is
        // two knobs for one idea and a silent disagreement waiting to happen.
        const float held = std::clamp(_lesionP.load() + Settings::fCurePotency,
            -LESION_LIMIT, LESION_LIMIT);
        _lesionP.store(held);
        logger::info("bandage used -> lesion P {:+.1f}", held);
    }

    // The campfire power arrives as a magic effect on the player, and this is
    // already the place that watches those - so it is caught here rather than
    // by standing up a second sink for the same event.
    void Elemental::NoteCampfire(const RE::EffectSetting* a_effect)
    {
        if (a_effect && a_effect == Forms::mgefLightCampfire) {
            Campfire::GetSingleton().Light();
        }
    }

    // Damage into bar, in one place, whichever path found the damage.
    void Elemental::Queue(float a_sign, float a_damage)
    {
        const float maxHealth = MaxHealth();
        if (a_sign == 0.0f || a_damage <= 0.0f || maxHealth <= 0.0f) {
            return;
        }

        const float nudge = a_sign * Settings::fElemDamageShare * (a_damage / maxHealth);
        if (nudge == 0.0f) {
            return;
        }

        const float queued =
            std::clamp(_queued.load() + nudge, -QUEUE_LIMIT, QUEUE_LIMIT);
        _queued.store(queued);

        if (Settings::bDebugLog) {
            logger::info(
                "elemental: {:.1f} damage of {:.0f} max health -> {:+.3f}, queued {:+.3f}",
                a_damage, maxHealth, nudge, queued);
        }
    }

    // Everything that damages over time, integrated against the seconds that
    // actually passed. A concentration effect's magnitude is damage per second,
    // so this is exactly what it was worth - no debounce, and no assumption
    // about how often the engine chooses to re-announce it. A ward that
    // repelled it left nothing in the list to find.
    void Elemental::Tick()
    {
        const auto  now = std::chrono::steady_clock::now();
        const float dt =
            _tickedOnce ? std::chrono::duration<float>(now - _ticked).count() : 0.0f;
        _ticked = now;
        _tickedOnce = true;

        if (!Settings::bModEnabled || dt <= 0.0f || dt > TICK_LIMIT) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* target = player ? player->AsMagicTarget() : nullptr;
        auto* list = target ? target->GetActiveEffectList() : nullptr;
        if (!list) {
            return;
        }

        for (auto* active : *list) {
            if (!active || active->flags.any(RE::ActiveEffect::Flag::kInactive,
                              RE::ActiveEffect::Flag::kDispelled)) {
                continue;
            }
            const auto* base = active->GetBaseObject();
            if (!base ||
                base->data.castingType != RE::MagicSystem::CastingType::kConcentration) {
                continue;
            }

            const float sign = SignOf(KindOf(base));
            if (sign != 0.0f) {
                Queue(sign, std::abs(active->magnitude) * dt);
            }
        }
    }

    void Elemental::Note(const RE::EffectSetting* a_effect)
    {
        if (!Settings::bModEnabled) {
            return;
        }

        const Element kind = KindOf(a_effect);
        if (kind == Element::kNone) {
            return;
        }

        // WHAT MOVES THE BAR IS THE DAMAGE, NOT THE FACT OF BEING HIT.
        //
        // It used to be a flat number per hit, the same for a candle and for a
        // dragon, and it was paid whether or not anything got through - a ward
        // could swallow the spell whole and the bar moved anyway. Now a hit is
        // worth the share of the player's health it actually took.
        //
        // Which is why nothing is debounced for the bar any more: a trap that
        // applied its effect thirty times really did apply it thirty times, and
        // thirty small numbers are the right answer. Damage over time is the one
        // thing not counted here - Tick() integrates it instead.
        if (a_effect->data.castingType != RE::MagicSystem::CastingType::kConcentration) {
            Queue(SignOf(kind), LandedMagnitude(a_effect));
        }

        // THE LESION COUNTER STILL COUNTS HITS, not damage, so it still needs
        // the debounce: one application is one wound there, and a cloak effect
        // would otherwise open thirty a second.
        const auto now = std::chrono::steady_clock::now();
        if (_seenAny &&
            std::chrono::duration<float>(now - _last).count() < EVENT_GAP) {
            return;
        }
        _last = now;
        _seenAny = true;

        if (Settings::bElemLesionEnabled) {
            const float resist = ResistFactor(ResistOf(kind));
            const float damage = Settings::fElemLesionHitP * resist;
            const float held =
                std::clamp(_lesionP.load() + damage, -LESION_LIMIT, LESION_LIMIT);
            _lesionP.store(held);
        }
    }

    float Elemental::TakeQueued()
    {
        return _queued.exchange(0.0f);
    }

    namespace
    {
        RE::BSEventNotifyControl ApplySink::ProcessEvent(
            const RE::TESMagicEffectApplyEvent*                a_event,
            RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
        {
            if (!a_event || a_event->target.get() != RE::PlayerCharacter::GetSingleton()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // Cures ride the same event. Checked before the elemental work
            // because the debounce in there would swallow most of them.
            if (Disease::IsCureEffect(a_event->magicEffect)) {
                Disease::GetSingleton().ApplyCure();
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* form = RE::TESForm::LookupByID(a_event->magicEffect);
            if (auto* effect = form ? form->As<RE::EffectSetting>() : nullptr) {
                Elemental::GetSingleton().NoteCampfire(effect);
                Elemental::GetSingleton().Note(effect);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    }
}
