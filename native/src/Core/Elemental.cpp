#include "PCH.h"

#include "Core/Elemental.h"

#include "Core/Disease.h"
#include "Core/Campfire.h"
#include "Core/Illnesses.h"
#include "Core/LesionIllness.h"
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
        // softens the lesion counter.
        [[nodiscard]] float SignOf(Element a_kind)
        {
            switch (a_kind) {
            case Element::kFrost:  return -1.0f;   // frost chills
            case Element::kFire:   return 1.0f;    // fire warms, and costs health for it
            default:               return 0.0f;    // shock never touches the bar
            }
        }

        [[nodiscard]] std::string_view NameOf(Element a_kind)
        {
            switch (a_kind) {
            case Element::kFrost: return "frost"sv;
            case Element::kFire:  return "fire"sv;
            case Element::kShock: return "shock"sv;
            default:              return "nothing"sv;
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
        // negative factor would turn a resisted hit into its opposite. Used by
        // the LESION counter only - the cold bar is charged what the engine
        // actually took, which has been through resistance already.
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

        // ModifyActorValue, index 0x20 of the ValueModifierEffect vtable: the
        // one place an effect of that family changes an actor value, whether it
        // does it once at the start or every second it lasts.
        //
        // EVERY CLASS IN THE FAMILY, not just the two that override it. A
        // vtable belongs to a class, not to a hierarchy: a subclass that
        // inherits ModifyActorValue still has its own table with its own copy
        // of the pointer, and patching the base leaves every one of them
        // untouched. Hooking ValueModifierEffect and DualValueModifierEffect
        // alone was heard by nothing at all - the hooks installed, the lesion
        // counter recorded the hit, and not one damage line was written.
        struct Slot
        {
            const char*    name;
            REL::VariantID vtable;
        };

        constexpr std::array SLOTS{
            Slot{ "ValueModifier", RE::VTABLE_ValueModifierEffect[0] },
            Slot{ "DualValueModifier", RE::VTABLE_DualValueModifierEffect[0] },
            Slot{ "PeakValueModifier", RE::VTABLE_PeakValueModifierEffect[0] },
            Slot{ "TargetValueModifier", RE::VTABLE_TargetValueModifierEffect[0] },
            Slot{ "AccumulatingValueModifier",
                RE::VTABLE_AccumulatingValueModifierEffect[0] },
            Slot{ "ValueAndConditions", RE::VTABLE_ValueAndConditionsEffect[0] },
            Slot{ "Absorb", RE::VTABLE_AbsorbEffect[0] },
            Slot{ "Paralysis", RE::VTABLE_ParalysisEffect[0] },
        };

        // One thunk per slot, each keeping its own original: two tables can
        // hold the same pointer, and they still have to be called back through
        // the entry they came from.
        template <std::size_t I>
        struct ModifyHook
        {
            static void Thunk(RE::ValueModifierEffect* a_this, RE::Actor* a_actor,
                float a_value, RE::ActorValue a_actorValue)
            {
                Elemental::GetSingleton().NoteDamage(a_this, a_actor, a_value,
                    a_actorValue, SLOTS[I].name);
                _Thunk(a_this, a_actor, a_value, a_actorValue);
            }

            static inline REL::Relocation<decltype(Thunk)> _Thunk;
        };

        template <std::size_t I>
        void HookOne()
        {
            REL::Relocation<std::uintptr_t> vtbl{ SLOTS[I].vtable };

            // What the slot held before the swap, as an offset into the game.
            // Two classes that both inherit the same implementation show the
            // same number here, and a number that is the same everywhere would
            // mean the index is wrong rather than that nobody overrides it.
            const auto was = *reinterpret_cast<std::uintptr_t*>(
                vtbl.address() + 0x20 * sizeof(void*));

            ModifyHook<I>::_Thunk = vtbl.write_vfunc(0x20, ModifyHook<I>::Thunk);
            logger::info("elemental: hooked {:<26} slot held +{:X}", SLOTS[I].name,
                was - REL::Module::get().base());
        }

        template <std::size_t... I>
        void HookAll(std::index_sequence<I...>)
        {
            (HookOne<I>(), ...);
        }
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

        HookAll(std::make_index_sequence<SLOTS.size()>{});
        logger::info("elemental damage read at the source ({} vtables)", SLOTS.size());
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
        auto* lesion = Illnesses::GetSingleton().Lesion();
        if (!lesion || !lesion->Wounded()) {
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

    // The campfire power arrives as a magic effect on the player, and the apply
    // sink is already the place that watches those - so it is caught there
    // rather than by standing up a second sink for the same event.
    void Elemental::NoteCampfire(const RE::EffectSetting* a_effect)
    {
        if (a_effect && a_effect == Forms::mgefLightCampfire) {
            Campfire::GetSingleton().Light();
        }
    }

    // Damage into bar.
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

    void Elemental::NoteDamage(const RE::ValueModifierEffect* a_effect,
        const RE::Actor* a_actor, float a_value, RE::ActorValue a_actorValue,
        const char* a_from)
    {
        // This runs for every value-modifier effect on every actor in the
        // world, so the cheap tests come first.
        if (!Settings::bModEnabled) {
            return;
        }
        if (!a_effect || a_actor != RE::PlayerCharacter::GetSingleton()) {
            return;
        }

        const float sign = SignOf(KindOf(a_effect->GetBaseObject()));
        if (sign == 0.0f) {
            return;
        }

        // kNone IS NOT "no actor value". It means "the one this effect
        // carries", and the caller passes it nearly every time - which is why
        // filtering on it threw every hit away.
        //
        // The engine resolves it in the first instructions of the function
        // this hook sits in front of:
        //
        //     +00567AC6  cmp  r9d, -1
        //     +00567ACA  jne  +00567AD2
        //     +00567ACC  mov  esi, dword ptr [rcx + 0x90]
        //
        // 0x90 is ValueModifierEffect::actorValue, and the header agrees with
        // the binary about that offset. So this reads it the same way.
        const RE::ActorValue actorValue = a_actorValue != RE::ActorValue::kNone
                                              ? a_actorValue
                                              : a_effect->actorValue;

        if (Settings::bDebugLog) {
            const auto* base = a_effect->GetBaseObject();
            const char* edid = base ? base->GetFormEditorID() : nullptr;
            logger::info("elemental: {} [{:08X}] through {} - {:+.2f} to av {} "
                         "(asked for {})",
                (edid && *edid) ? edid : "<no editor id>",
                base ? base->GetFormID() : 0, a_from, a_value,
                static_cast<int>(actorValue), static_cast<int>(a_actorValue));
        }

        // HEALTH ONLY, deliberately. A frost spell takes stamina in the same
        // breath - the dual effect calls the engine a second time for it, with
        // its own weight - and paying for both would charge one hit two ways.
        // "The share of the player's health it took" is the model, and it is
        // the health that is meant.
        if (actorValue != RE::ActorValue::kHealth) {
            return;
        }

        // Damage takes health away. A restore is somebody healing and has
        // nothing to say about the cold.
        if (a_value >= 0.0f) {
            return;
        }

        Queue(sign, -a_value);
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

        // THE LESION COUNTER COUNTS HITS, not damage, so it needs the debounce
        // the cold bar has no use for: one application is one wound here, and a
        // cloak effect would otherwise open thirty a second.
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

            // THIS PATH SAID NOTHING AT ALL. Not a word for a counted
            // hit, not a word for what it was worth, while the cold bar
            // beside it printed two lines for every tick of the same
            // damage. The only line the lesions ever wrote was the one at
            // the moment of contracting, so a log could show an illness
            // arriving with nothing whatever leading up to it - which is
            // exactly how it read when one was reported.
            if (Settings::bDebugLog) {
                const char* edid = a_effect->GetFormEditorID();
                logger::info("lesion: {} hit {} [{:08X}] worth {:+.1f} "
                             "(resist x{:.2f}), holding {:+.1f}",
                    NameOf(kind), (edid && *edid) ? edid : "<no editor id>",
                    a_effect->GetFormID(), damage, resist, held);
            }
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
