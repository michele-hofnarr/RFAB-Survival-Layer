#include "PCH.h"

#include "Core/Forms.h"

#include "Core/FormIDs.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view PLUGIN = "RFAB_SurvivalLayer.esp"sv;

        // Vanilla, so it is looked up in Skyrim.esm rather than the plugin.
        constexpr std::string_view SKYRIM = "Skyrim.esm"sv;
        constexpr RE::FormID       FX_COLD_SHADER = 0xDC20D;

        constexpr RE::FormID       RACE_DRAUGR = 0xD53;
        constexpr RE::FormID       RACE_SLAUGHTERFISH = 0x13203;
        constexpr RE::FormID       KW_TROLL = 0xF5D16;
        constexpr RE::FormID       KW_UNDEAD = 0x13796;
        constexpr RE::FormID       KW_BEDROLL = 0xE4AD6;

        // RFAB's own food classification.
        constexpr std::string_view RFAB = "RFAB.esp"sv;
        constexpr RE::FormID       KW_RAW_FOOD = 0xCD63E;
        constexpr RE::FormID       KW_STRONG_STOMACH = 0x4CF31E;

        RE::TESDataHandler* g_handler = nullptr;

        // The cast is As<T>(), never LookupForm<T>.
        //
        // LookupForm<T> checks `form->Is(T::FORMTYPE)`, and FORMTYPE is a
        // single concrete value. TESBoundObject is a base class - its FORMTYPE
        // is inherited from TESForm, which is FormType::None - so asking for
        // one matches nothing and returns null for a record that is sitting
        // right there. That took out every placeable and carryable in the mod
        // at once: firewood, the wood axe, the backpack, the bedroll and its
        // furniture, the tent, the campfire and the cook pot. The campfire said
        // "no firewood" while the player had six, and the bedroll fell back to
        // being an ordinary dropped item.
        //
        // As<T>() casts on the real type hierarchy, so it is right for base and
        // leaf classes alike.
        template <class T>
        [[nodiscard]] T* LookupIn(std::string_view a_file, RE::FormID a_id, const char* a_what)
        {
            auto* form = g_handler->LookupForm(a_id, a_file);
            auto* typed = form ? form->As<T>() : nullptr;
            if (!typed) {
                logger::error("{} ({:#08x}) not found in {}{}", a_what, a_id, a_file,
                    form ? " (wrong type)" : "");
            }
            return typed;
        }

        template <class T>
        [[nodiscard]] T* Lookup(RE::FormID a_localID, const char* a_what)
        {
            return LookupIn<T>(PLUGIN, a_localID, a_what);
        }

        // Resolve a list of sound descriptors, skipping any that went missing.
        // One record per file is not a choice: see BuildCoughSound in the
        // generator for why a descriptor holding several cannot be built.
        [[nodiscard]] std::vector<RE::BGSSoundDescriptorForm*> LoadSounds(
            const auto& a_ids, const char* a_what)
        {
            std::vector<RE::BGSSoundDescriptorForm*> out;
            out.reserve(std::size(a_ids));
            for (const auto id : a_ids) {
                if (auto* snd = Lookup<RE::BGSSoundDescriptorForm>(id, a_what)) {
                    out.push_back(snd);
                }
            }
            return out;
        }

        // Fill one row of the disease table. Every progressive illness has the
        // same records - three stage spells and six things it can say - so they
        // are resolved by a loop rather than by two hundred named fields.
        void LoadDisease(DiseaseForms& a_out, std::string_view a_id, const char* a_what,
            const RE::FormID (&a_stage)[3], const RE::FormID (&a_msg)[3],
            RE::FormID a_cured, const RE::FormID (&a_ease)[2])
        {
            a_out.id = a_id;
            for (int i = 0; i < 3; ++i) {
                a_out.stage[i] = Lookup<RE::SpellItem>(a_stage[i], a_what);
                a_out.msg[i] = Lookup<RE::BGSMessage>(a_msg[i], a_what);
            }
            a_out.cured = Lookup<RE::BGSMessage>(a_cured, a_what);
            for (int i = 0; i < 2; ++i) {
                a_out.ease[i] = Lookup<RE::BGSMessage>(a_ease[i], a_what);
            }
        }
    }

    namespace
    {
        // The base disease and its marker live in whichever master defines
        // them; our two stages and the messages are ours.
        //
        // GetFormFromFile needs the ORIGIN file, not RFAB.esp: RFAB overrides
        // the vanilla records rather than adding new ones, so asking RFAB.esp
        // for them returns nothing.
        struct RfabSource
        {
            std::string_view id;
            std::string_view baseFile;
            RE::FormID       base;
            std::string_view markFile;
            RE::FormID       mark;
            RE::FormID       ours[2];
            RE::FormID       msg[2];
            RE::FormID       ease[2];
            RE::FormID       cured;
        };

        void LoadRfab(RfabDiseaseForms& a_out, const RfabSource& a_src)
        {
            a_out.id = a_src.id;
            a_out.base = LookupIn<RE::SpellItem>(a_src.baseFile, a_src.base, "RFAB disease");
            a_out.mark =
                LookupIn<RE::EffectSetting>(a_src.markFile, a_src.mark, "RFAB disease marker");
            for (int i = 0; i < 2; ++i) {
                a_out.ours[i] = Lookup<RE::SpellItem>(a_src.ours[i], "RFAB wrapper stage");
                a_out.msg[i] = Lookup<RE::BGSMessage>(a_src.msg[i], "RFAB wrapper message");
                a_out.ease[i] = Lookup<RE::BGSMessage>(a_src.ease[i], "RFAB wrapper ease");
            }
            a_out.cured = Lookup<RE::BGSMessage>(a_src.cured, "RFAB wrapper cured message");
        }
    }

    bool Forms::Load()
    {
        g_handler = RE::TESDataHandler::GetSingleton();
        if (!g_handler) {
            logger::error("no data handler - forms cannot be resolved");
            return false;
        }

        using namespace FormIDs;

        abSleep = Lookup<RE::SpellItem>(AbSleep, "sleep penalty ability");
        abHunger = Lookup<RE::SpellItem>(AbHunger, "hunger penalty ability");
        abCold = Lookup<RE::SpellItem>(AbCold, "cold penalty ability");

        abBonusWarm = Lookup<RE::SpellItem>(AbBonusWarm, "warm bonus ability");
        abBonusRest = Lookup<RE::SpellItem>(AbBonusRest, "rest bonus ability");
        abBonusFed = Lookup<RE::SpellItem>(AbBonusFed, "fed bonus ability");

        abHypo1 = Lookup<RE::SpellItem>(AbHypo1, "hypothermia stage 1");
        abHypo2 = Lookup<RE::SpellItem>(AbHypo2, "hypothermia stage 2");
        abHypo3 = Lookup<RE::SpellItem>(AbHypo3, "hypothermia stage 3");
        msgHypo1 = Lookup<RE::BGSMessage>(MsgHypo1, "hypothermia message 1");
        msgHypo2 = Lookup<RE::BGSMessage>(MsgHypo2, "hypothermia message 2");
        msgHypo3 = Lookup<RE::BGSMessage>(MsgHypo3, "hypothermia message 3");
        msgHypoCured = Lookup<RE::BGSMessage>(MsgHypoCured, "hypothermia cured message");
        msgHypoNoRest = Lookup<RE::BGSMessage>(MsgHypoNoRest, "hypothermia no-rest message");
        msgHypoEase2 = Lookup<RE::BGSMessage>(MsgHypoEase2, "hypothermia eased-to-2 message");
        msgHypoEase1 = Lookup<RE::BGSMessage>(MsgHypoEase1, "hypothermia eased-to-1 message");

        dzMarker = Lookup<RE::SpellItem>(DiseaseMarker, "disease marker");
        abCold1 = Lookup<RE::SpellItem>(DiseaseColdCommon1, "common cold stage 1");
        abCold2 = Lookup<RE::SpellItem>(DiseaseColdCommon2, "common cold stage 2");
        abCold3 = Lookup<RE::SpellItem>(DiseaseColdCommon3, "common cold stage 3");
        msgCold1 = Lookup<RE::BGSMessage>(MsgColdCommon1, "common cold message 1");
        msgCold2 = Lookup<RE::BGSMessage>(MsgColdCommon2, "common cold message 2");
        msgCold3 = Lookup<RE::BGSMessage>(MsgColdCommon3, "common cold message 3");
        msgCold0 = Lookup<RE::BGSMessage>(MsgColdCommonCured, "common cold cured message");
        msgColdEase2 = Lookup<RE::BGSMessage>(MsgColdCommonEase2, "common cold eased-to-2");
        msgColdEase1 = Lookup<RE::BGSMessage>(MsgColdCommonEase1, "common cold eased-to-1");

        // The four caught by being hit or by eating badly. Ids are v0.4.0's,
        // and they have to stay exactly these two letters: the co-save keys its
        // stored stage and P on them, so renaming one loses that illness on
        // every existing save.
        LoadDisease(hitDisease[0], "BR"sv, "brown rot",
            { DiseaseBrownRot1, DiseaseBrownRot2, DiseaseBrownRot3 },
            { MsgBrownRot1, MsgBrownRot2, MsgBrownRot3 },
            MsgBrownRotCured, { MsgBrownRotEase2, MsgBrownRotEase1 });

        LoadDisease(hitDisease[1], "GW"sv, "gutworm",
            { DiseaseGutworm1, DiseaseGutworm2, DiseaseGutworm3 },
            { MsgGutworm1, MsgGutworm2, MsgGutworm3 },
            MsgGutwormCured, { MsgGutwormEase2, MsgGutwormEase1 });

        LoadDisease(hitDisease[2], "GS"sv, "green spore",
            { DiseaseGreenspore1, DiseaseGreenspore2, DiseaseGreenspore3 },
            { MsgGreenspore1, MsgGreenspore2, MsgGreenspore3 },
            MsgGreensporeCured, { MsgGreensporeEase2, MsgGreensporeEase1 });

        LoadDisease(hitDisease[3], "FP"sv, "food poisoning",
            { DiseaseFoodPoison1, DiseaseFoodPoison2, DiseaseFoodPoison3 },
            { MsgFoodPoison1, MsgFoodPoison2, MsgFoodPoison3 },
            MsgFoodPoisonCured, { MsgFoodPoisonEase2, MsgFoodPoisonEase1 });

        // Elemental lesions run their own P model but carry the same records.
        LoadDisease(elemLesion, "EL"sv, "elemental lesions",
            { DiseaseElemLesion1, DiseaseElemLesion2, DiseaseElemLesion3 },
            { MsgElemLesion1, MsgElemLesion2, MsgElemLesion3 },
            MsgElemLesionCured, { MsgElemLesionEase2, MsgElemLesionEase1 });

        fireSources = Lookup<RE::BGSListForm>(FireSources, "fire source list");
        coldInteriors = Lookup<RE::BGSListForm>(ColdInteriors, "cold interior list");

        {
            constexpr std::string_view DB = "Dragonborn.esm"sv;
            const RfabSource sources[7] = {
                { "AT"sv,  SKYRIM, 0xB877C, RFAB,   0xCD9BD,
                  { DzAT2, DzAT3 }, { MsgDzAT2, MsgDzAT3 },
                  { MsgDzATEase2, MsgDzATEase1 }, MsgDzATCured },
                { "RJ"sv,  SKYRIM, 0xB8782, SKYRIM, 0xB877A,
                  { DzRJ2, DzRJ3 }, { MsgDzRJ2, MsgDzRJ3 },
                  { MsgDzRJEase2, MsgDzRJEase1 }, MsgDzRJCured },
                { "WB"sv,  SKYRIM, 0xB8783, SKYRIM, 0xB877B,
                  { DzWB2, DzWB3 }, { MsgDzWB2, MsgDzWB3 },
                  { MsgDzWBEase2, MsgDzWBEase1 }, MsgDzWBCured },
                { "RA"sv,  SKYRIM, 0xB8781, SKYRIM, 0xB8779,
                  { DzRA2, DzRA3 }, { MsgDzRA2, MsgDzRA3 },
                  { MsgDzRAEase2, MsgDzRAEase1 }, MsgDzRACured },
                { "BF"sv,  SKYRIM, 0xB877E, SKYRIM, 0xB8776,
                  { DzBF2, DzBF3 }, { MsgDzBF2, MsgDzBF3 },
                  { MsgDzBFEase2, MsgDzBFEase1 }, MsgDzBFCured },
                { "BRR"sv, SKYRIM, 0xB877F, SKYRIM, 0xB8777,
                  { DzBRR2, DzBRR3 }, { MsgDzBRR2, MsgDzBRR3 },
                  { MsgDzBRREase2, MsgDzBRREase1 }, MsgDzBRRCured },
                { "DR"sv,  DB,     0x285C1, DB,     0x285C0,
                  { DzDR2, DzDR3 }, { MsgDzDR2, MsgDzDR3 },
                  { MsgDzDREase2, MsgDzDREase1 }, MsgDzDRCured },
            };
            for (int i = 0; i < 7; ++i) {
                LoadRfab(rfabDisease[i], sources[i]);
            }
            peryiteBlessing =
                LookupIn<RE::SpellItem>(RFAB, 0x60A5, "Peryite blessing");
        }

        powerCampfire = Lookup<RE::SpellItem>(PowerCampfire, "campfire power");
        mgefLightCampfire =
            Lookup<RE::EffectSetting>(MgefLightCampfire, "campfire effect");
        campfireLit = Lookup<RE::TESBoundObject>(CampfireLit, "lit campfire");
        msgCampLit = Lookup<RE::BGSMessage>(MsgCampLit, "campfire lit message");
        msgCampOut = Lookup<RE::BGSMessage>(MsgCampOut, "campfire out message");
        msgCampConfirm =
            Lookup<RE::BGSMessage>(MsgCampConfirm, "campfire confirm message");
        msgCampNoFuel = Lookup<RE::BGSMessage>(MsgCampNoFuel, "campfire no-fuel message");
        msgCampNoPerk = Lookup<RE::BGSMessage>(MsgCampNoPerk, "campfire no-perk message");
        msgCampRain = Lookup<RE::BGSMessage>(MsgCampRain, "campfire rain message");
        msgNoRoom = Lookup<RE::BGSMessage>(MsgNoRoom, "no-room message");
        msgWetSoaked = Lookup<RE::BGSMessage>(MsgWetSoaked, "soaked message");
        msgWetDry = Lookup<RE::BGSMessage>(MsgWetDry, "dried message");
        msgNoTeleport = Lookup<RE::BGSMessage>(MsgNoTeleport, "teleport-blocked message");

        // The pieces we place are vanilla clutter. These ids are the ones the
        // generator resolved from editor ids and wrote into _RSL_Forms.psc -
        // read off that rather than remembered, which is how the wrong ones
        // get in.
        baseCampfire =
            LookupIn<RE::TESBoundObject>(SKYRIM, 0x35F49, "burning campfire");
        baseCookSpit = LookupIn<RE::TESBoundObject>(SKYRIM, 0x1018E3, "cooking spit");
        baseCookPot = LookupIn<RE::TESBoundObject>(SKYRIM, 0x1010B3, "cooking pot");
        kettle = LookupIn<RE::TESBoundObject>(SKYRIM, 0x12FE6, "kettle");
        firewood = LookupIn<RE::TESBoundObject>(SKYRIM, 0x6F993, "firewood");

        woodAxe = LookupIn<RE::TESBoundObject>(SKYRIM, 0x2F2F4, "wood axe");
        backpack = LookupIn<RE::TESBoundObject>(RFAB, 0xCD955, "adventurer backpack");
        msgTreeCooldown =
            Lookup<RE::BGSMessage>(MsgTreeCooldown, "tree cooldown message");

        bedrollItem = Lookup<RE::TESBoundObject>(BedrollItem, "bedroll item");
        bedrollFurn = Lookup<RE::TESBoundObject>(BedrollFurn, "bedroll furniture");
        baseTent = LookupIn<RE::TESBoundObject>(SKYRIM, 0x800E2, "small tent");

        perkSurvivalBasics = LookupIn<RE::BGSPerk>(RFAB, 0xCE266, "survival basics perk");
        perkCook = LookupIn<RE::BGSPerk>(RFAB, 0xCE264, "chef perk");
        perkAcclimatization =
            LookupIn<RE::BGSPerk>(RFAB, 0xCE268, "acclimatization perk");
        perkCheerfulness = LookupIn<RE::BGSPerk>(RFAB, 0xCE45F, "cheerfulness perk");

        idleWarm = LookupIn<RE::TESIdleForm>(SKYRIM, 0xE8642, "warm hands idle");
        raceDraugr = LookupIn<RE::TESRace>(SKYRIM, RACE_DRAUGR, "draugr race");
        raceSlaughterfish =
            LookupIn<RE::TESRace>(SKYRIM, RACE_SLAUGHTERFISH, "slaughterfish race");
        raceOrc = LookupIn<RE::TESRace>(SKYRIM, 0x13747, "orc race");
        raceKhajiit = LookupIn<RE::TESRace>(SKYRIM, 0x13745, "khajiit race");

        // Orcs and khajiit cough as men do and then some of their own, so
        // their lists open with the male records instead of duplicating them.
        constexpr RE::FormID COUGH_FEMALE[] = { SndCoughFemale1, SndCoughFemale2,
            SndCoughFemale3, SndCoughFemale4, SndCoughFemale5, SndCoughFemale6,
            SndCoughFemale7, SndCoughFemale8, SndCoughFemale9 };
        constexpr RE::FormID COUGH_MALE[] = { SndCoughMale1, SndCoughMale2,
            SndCoughMale3, SndCoughMale4, SndCoughMale5, SndCoughMale6,
            SndCoughMale7, SndCoughMale8 };
        constexpr RE::FormID COUGH_ORC[] = { SndCoughOrc1, SndCoughOrc2,
            SndCoughOrc3, SndCoughOrc4 };
        constexpr RE::FormID COUGH_KHAJIIT[] = { SndCoughKhajiit1, SndCoughKhajiit2 };

        coughFemale = LoadSounds(COUGH_FEMALE, "cough, female");
        coughMale = LoadSounds(COUGH_MALE, "cough, male");

        const auto orcOnly = LoadSounds(COUGH_ORC, "cough, orc");
        coughOrc = coughMale;
        coughOrc.insert(coughOrc.end(), orcOnly.begin(), orcOnly.end());

        const auto khajiitOnly = LoadSounds(COUGH_KHAJIIT, "cough, khajiit");
        coughKhajiit = coughMale;
        coughKhajiit.insert(coughKhajiit.end(), khajiitOnly.begin(), khajiitOnly.end());

        kwTroll = LookupIn<RE::BGSKeyword>(SKYRIM, KW_TROLL, "troll keyword");
        kwUndead = LookupIn<RE::BGSKeyword>(SKYRIM, KW_UNDEAD, "undead keyword");
        kwBedRoll = LookupIn<RE::BGSKeyword>(SKYRIM, KW_BEDROLL, "bedroll keyword");
        kwRawFood = LookupIn<RE::BGSKeyword>(RFAB, KW_RAW_FOOD, "raw food keyword");
        kwStrongStomach =
            LookupIn<RE::BGSKeyword>(RFAB, KW_STRONG_STOMACH, "strong stomach keyword");
        kwSpecialFood = LookupIn<RE::BGSKeyword>(RFAB, 0x0CD63D, "special food keyword");

        mgefTeleport = LookupIn<RE::EffectSetting>(RFAB, 0x06B0F8, "teleport effect");
        mgefMarkRecall = LookupIn<RE::EffectSetting>(RFAB, 0x1AC746, "mark-and-recall effect");

        fxColdShader = LookupIn<RE::TESEffectShader>(SKYRIM, FX_COLD_SHADER, "frost shader");
        globTimeScale = LookupIn<RE::TESGlobal>(SKYRIM, 0x00003A, "TimeScale global");

        // DLC2SolstheimWorld. Looked up quietly: a game without Dragonborn is
        // a supported game, and the climate simply never sees the island.
        if (auto* form = g_handler->LookupForm(0x000800, "Dragonborn.esm"sv)) {
            wsSolstheim = form->As<RE::TESWorldSpace>();
        }

        sndPotionUse =
            LookupIn<RE::BGSSoundDescriptorForm>(SKYRIM, 0x0B6435, "potion-use sound");
        alchBandage = LookupIn<RE::AlchemyItem>(RFAB, 0x0CF133, "clean linen cloth");

        _ready = abSleep && abHunger && abCold &&
                 abBonusWarm && abBonusRest && abBonusFed &&
                 abHypo1 && abHypo2 && abHypo3;
        if (_ready) {
            logger::info("forms resolved");
        } else {
            logger::error("some forms are missing - is RFAB_SurvivalLayer.esp enabled?");
        }
        return _ready;
    }

    bool HasCampPerks()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && Forms::perkSurvivalBasics && Forms::perkAcclimatization &&
               player->HasPerk(Forms::perkSurvivalBasics) &&
               player->HasPerk(Forms::perkAcclimatization);
    }
}
