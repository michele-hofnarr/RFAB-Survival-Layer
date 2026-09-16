#pragma once

// Forms from the mod's own plugin, resolved once.
//
// v0.4.0 reached these through _RSL_Forms.psc, which the generator emits: 266
// getters, every one a Game.GetFormFromFile at the point of use. Papyrus had no
// choice; here they are looked up at kDataLoaded and kept, which is the single
// biggest reason the tick stops costing anything.
//
// The ids come from Core/FormIDs.h, which the generator writes on every run.
// They used to be typed in here by hand, and that was a standing trap:
// CopyVanillaMgef drops and re-copies the penalty library each run, so ids
// churn, and nothing tied the two files together.

namespace RSL
{
    // One progressive illness's records. The four caught by being hit, the
    // seven wrapped around RFAB's own and the elemental lesions are all this
    // same shape, so they live in a table rather than as named fields.
    struct DiseaseForms
    {
        // The key the co-save stores stage and P under. Two letters, and they
        // must not change: renaming one loses that illness on every save.
        std::string_view id;

        RE::SpellItem*  stage[3]{};   // 1, 2, 3
        RE::BGSMessage* msg[3]{};     // caught, worse, worst
        RE::BGSMessage* cured{};
        RE::BGSMessage* ease[2]{};    // eased to 2, eased to 1

        [[nodiscard]] bool Valid() const { return stage[0] != nullptr; }
    };

    // One of RFAB's own diseases with our second and third act on it. Stage 1
    // is their record and stays theirs; only 2 and 3 are ours.
    struct RfabDiseaseForms
    {
        std::string_view id;

        RE::SpellItem*      base{ nullptr };   // RFAB's, stage 1
        RE::EffectSetting*  mark{ nullptr };   // its first unconditional debuff
        RE::SpellItem*      ours[2]{};         // stages 2 and 3
        RE::BGSMessage*     msg[2]{};          // worse, worst

        // Eased to 2, eased to 1. The mod's own illnesses have had these
        // since the ease messages were wired up; the wrappers were left
        // without, so an RFAB disease announced every worsening and said
        // nothing at all about getting better.
        RE::BGSMessage*     ease[2]{};

        RE::BGSMessage*     cured{ nullptr };
    };

    struct Forms
    {
        // Resolve everything. Safe to call again.
        static bool Load();

        [[nodiscard]] static bool Ready() { return _ready; }

        // The three penalty abilities. Each carries four effects in an order
        // the generator fixes and this code depends on:
        //     0 Health, 1 Magicka, 2 Stamina, 3 SpeedMult
        static inline RE::SpellItem* abSleep{ nullptr };
        static inline RE::SpellItem* abHunger{ nullptr };
        static inline RE::SpellItem* abCold{ nullptr };

        // Full-bar bonuses, one effect each: cold -> Health regen,
        // sleep -> Magicka, hunger -> Stamina.
        static inline RE::SpellItem* abBonusWarm{ nullptr };
        static inline RE::SpellItem* abBonusRest{ nullptr };
        static inline RE::SpellItem* abBonusFed{ nullptr };

        // Hypothermia: three stage abilities and the notifications.
        static inline RE::SpellItem* abHypo1{ nullptr };
        static inline RE::SpellItem* abHypo2{ nullptr };
        static inline RE::SpellItem* abHypo3{ nullptr };
        static inline RE::BGSMessage* msgHypo1{ nullptr };
        static inline RE::BGSMessage* msgHypo2{ nullptr };
        static inline RE::BGSMessage* msgHypo3{ nullptr };
        static inline RE::BGSMessage* msgHypoCured{ nullptr };
        static inline RE::BGSMessage* msgHypoNoRest{ nullptr };
        static inline RE::BGSMessage* msgHypoEase2{ nullptr };
        static inline RE::BGSMessage* msgHypoEase1{ nullptr };

        // Common cold: three stages and their notifications.
        static inline RE::SpellItem*  abCold1{ nullptr };
        static inline RE::SpellItem*  abCold2{ nullptr };
        static inline RE::SpellItem*  abCold3{ nullptr };
        static inline RE::BGSMessage* msgCold1{ nullptr };
        static inline RE::BGSMessage* msgCold2{ nullptr };
        static inline RE::BGSMessage* msgCold3{ nullptr };
        static inline RE::BGSMessage* msgCold0{ nullptr };
        static inline RE::BGSMessage* msgColdEase2{ nullptr };
        static inline RE::BGSMessage* msgColdEase1{ nullptr };

        // RFAB's own seven, in v0.4.0's order: AT RJ WB RA BF BRR DR. The
        // first six are base-game diseases the Peryite blessing freezes at
        // stage 1; DR is Dragonborn's Droops and is not covered by it.
        static inline RfabDiseaseForms rfabDisease[7]{};

        // The hidden marker that keeps an advanced illness legible to the
        // world. No effects, no name on screen - see Disease::SyncMarker.
        static inline RE::SpellItem* dzMarker{ nullptr };

        // RFAB_Blessing_Peryite. Its holders keep the boon on RFAB's stage 1.
        static inline RE::SpellItem* peryiteBlessing{ nullptr };

        // Caught by a hit or by eating badly: brown rot, gutworm, green spore,
        // food poisoning. Order is v0.4.0's hdId array.
        static inline DiseaseForms hitDisease[4]{};

        // Frostbite and burns. Same records, its own progression.
        static inline DiseaseForms elemLesion{};

        // Who gives you what. v0.4.0 matches the aggressor's race for draugr
        // and slaughterfish and a keyword for trolls, because troll variants
        // are several races but all carry ActorTypeTroll.
        static inline RE::TESRace*    raceDraugr{ nullptr };
        static inline RE::TESRace*    raceSlaughterfish{ nullptr };

        // Two races cough with a voice of their own, on top of the common one.
        static inline RE::TESRace*    raceOrc{ nullptr };
        static inline RE::TESRace*    raceKhajiit{ nullptr };

        // One descriptor per FILE, and the choice is made here rather than by
        // the engine: a descriptor holding a list of files cannot be built from
        // a script at all (BuildCoughSound in the generator has the evidence).
        // The orc and khajiit lists open with the common male files, which cost
        // nothing extra - they are the same records, listed twice.
        static inline std::vector<RE::BGSSoundDescriptorForm*> coughFemale{};
        static inline std::vector<RE::BGSSoundDescriptorForm*> coughMale{};
        static inline std::vector<RE::BGSSoundDescriptorForm*> coughOrc{};
        static inline std::vector<RE::BGSSoundDescriptorForm*> coughKhajiit{};
        static inline RE::BGSKeyword* kwTroll{ nullptr };

        // Raw food, and who can stomach it.
        static inline RE::BGSKeyword* kwRawFood{ nullptr };
        static inline RE::BGSKeyword* kwStrongStomach{ nullptr };

        // A prepared dish. It decides both what a meal is worth and which half
        // of the hunger bar it fills.
        static inline RE::BGSKeyword* kwSpecialFood{ nullptr };

        // The two ways out of the cold. RFAB gives the spell and the scroll of
        // each pair the same effect, so two entries cover four records - and
        // any later scroll built on the same effect as well.
        static inline RE::EffectSetting* mgefTeleport{ nullptr };
        static inline RE::EffectSetting* mgefMarkRecall{ nullptr };

        // What a drink sounds like going down. RFAB gives every one of its 34
        // RFAB_Drink_* the potion sound and every food an eating sound, and so
        // does vanilla - so this is what tells the two apart. See Needs::Assess.
        static inline RE::BGSSoundDescriptorForm* sndPotionUse{ nullptr };

        // The clean linen cloth, by form rather than by name.
        static inline RE::AlchemyItem* alchBandage{ nullptr };
        static inline RE::BGSKeyword* kwUndead{ nullptr };

        // What tells a bedroll from a bed, and the only thing that does.
        // A FURN record has no field for it: a bed and a bedroll are both
        // kCanSleep furniture with a kSleep marker, and nothing else in the
        // record separates them. Vanilla's own keyword does - 87 records
        // across the masters carry it and the 64 real beds do not - and our
        // own _RSL_BedrollFurn already carries it too, so the test needs no
        // list and no markup of anyone else's records.
        static inline RE::BGSKeyword* kwBedRoll{ nullptr };

        // Solstheim's worldspace. The only worldspace that has to be
        // recognised by form rather than by geometry: it is its own root, so
        // its coordinates say nothing about where on Tamriel's map it is.
        // Null when Dragonborn.esm is not loaded, which is a fine answer -
        // there is no Solstheim to stand on either.
        static inline RE::TESWorldSpace* wsSolstheim{ nullptr };

        // Everything that counts as a fire to warm yourself at, built by the
        // generator from the master files plus an explicit allowlist.
        static inline RE::BGSListForm* fireSources{ nullptr };
        static inline RE::BGSListForm* coldInteriors{ nullptr };

        // The campfire: the power that lights it, what it is made of, and
        // the four things it can say.
        static inline RE::SpellItem*    powerCampfire{ nullptr };
        static inline RE::EffectSetting* mgefLightCampfire{ nullptr };
        static inline RE::TESBoundObject* campfireLit{ nullptr };
        static inline RE::TESBoundObject* baseCampfire{ nullptr };
        static inline RE::TESBoundObject* baseCookSpit{ nullptr };
        static inline RE::TESBoundObject* baseCookPot{ nullptr };

        // Vanilla clutter, carried rather than consumed: the camp kitchen.
        // Without it a fire is only a fire.
        static inline RE::TESBoundObject* kettle{ nullptr };
        static inline RE::TESBoundObject* firewood{ nullptr };
        static inline RE::BGSMessage*   msgCampLit{ nullptr };
        static inline RE::BGSMessage*   msgCampOut{ nullptr };
        static inline RE::BGSMessage*   msgCampConfirm{ nullptr };
        static inline RE::BGSMessage*   msgCampNoFuel{ nullptr };
        static inline RE::BGSMessage*   msgCampNoPerk{ nullptr };
        static inline RE::BGSMessage*   msgCampRain{ nullptr };

        // Shared by the fire and the bedroll: both refuse when the way ahead
        // is solid, and both say the same thing about it.
        static inline RE::BGSMessage*   msgNoRoom{ nullptr };

        // The two ends of the wetness axis. It is the one axis with no bar and
        // no icon, so a line in the corner is the only way the player learns it
        // is there at all.
        static inline RE::BGSMessage*   msgWetSoaked{ nullptr };
        static inline RE::BGSMessage*   msgWetDry{ nullptr };

        // Why the scroll did nothing.
        static inline RE::BGSMessage*   msgNoTeleport{ nullptr };

        // The portable bedroll: the item you carry, the furniture it becomes,
        // and the tent that goes over it.
        static inline RE::TESBoundObject* bedrollItem{ nullptr };
        static inline RE::TESBoundObject* bedrollFurn{ nullptr };
        static inline RE::TESBoundObject* baseTent{ nullptr };

        // Chopping: the tool, the stand-in for it, and the "not yet" notice.
        static inline RE::TESBoundObject* woodAxe{ nullptr };
        static inline RE::TESBoundObject* backpack{ nullptr };
        static inline RE::BGSMessage*     msgTreeCooldown{ nullptr };

        // RFAB's survival perks. Basics gates the camp, Chef adds the cook pot,
        // Acclimatization is the second half of the shelter gate.
        static inline RE::BGSPerk* perkSurvivalBasics{ nullptr };
        static inline RE::BGSPerk* perkCook{ nullptr };
        static inline RE::BGSPerk* perkAcclimatization{ nullptr };

        // Cheerfulness - "you have learned to sleep well". RFAB gives it no
        // perk entries of its own, so it is a pure marker, and here it is
        // what makes a night in a bedroll count as real rest.
        static inline RE::BGSPerk* perkCheerfulness{ nullptr };

        // The vanilla warm-hands idle.
        static inline RE::TESIdleForm* idleWarm{ nullptr };

        // Vanilla frost shader, the ice crust on the character.
        static inline RE::TESEffectShader* fxColdShader{ nullptr };


        // Vanilla's TimeScale global (0x3A, 20 by default). Nothing here sets
        // it; it is read so the log can say what it is, because every rate in
        // this mod is per GAME hour and TimeScale is what turns those into real
        // minutes. A game run at 1 makes the whole model twenty times slower
        // than the balance it was tuned against, which looks exactly like a
        // broken model from the inside.
        static inline RE::TESGlobal* globTimeScale{ nullptr };

    private:
        static inline bool _ready{ false };
    };

    // Both RFAB survival perks at once. v0.4.0 called this HasShelterPerks and
    // hung the cold ceiling on it; here it gates the tent and decides whether a
    // camp of your own is worth anything against the cold. One place, because
    // it was written twice and the second copy was already out of step.
    [[nodiscard]] bool HasCampPerks();
}
