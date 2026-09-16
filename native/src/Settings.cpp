#include "PCH.h"

#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr auto DEFAULTS_INI = "Data/MCM/Config/RFAB_SurvivalLayer/settings.ini"sv;
        constexpr auto USER_INI = "Data/MCM/Settings/RFAB_SurvivalLayer.ini"sv;
    }

    void Settings::ReadSettings()
    {
        ReadFile(DEFAULTS_INI);
        ReadFile(USER_INI);
        logger::info("settings read (hud {:.3f}/{:.3f} scale {:.2f})", fHudX, fHudY, fHudScale);
    }

    void Settings::ReadFile(std::string_view a_path)
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto rc = ini.LoadFile(std::string(a_path).c_str());
        if (rc < 0) {
            // Not an error: the user file does not exist until MCM writes it,
            // and every key then just keeps the value it already had.
            logger::info("no settings at {}", a_path);
            return;
        }

        ReadBool(ini, "General", "bModEnabled", bModEnabled);
        ReadBool(ini, "General", "bDebugLog", bDebugLog);
        ReadBool(ini, "General", "bTracePass", bTracePass);

        ReadFloat(ini, "HUD", "fBarWidth", fBarWidth);
        ReadFloat(ini, "HUD", "fBarHeight", fBarHeight);
        ReadFloat(ini, "HUD", "fRowPitch", fRowPitch);
        ReadFloat(ini, "HUD", "fIconSize", fIconSize);
        ReadFloat(ini, "HUD", "fIconGap", fIconGap);
        ReadFloat(ini, "HUD", "fTempIconSize", fTempIconSize);
        ReadFloat(ini, "HUD", "fFrameMid", fFrameMid);
        ReadFloat(ini, "HUD", "fHudX", fHudX);
        ReadFloat(ini, "HUD", "fHudY", fHudY);
        ReadFloat(ini, "HUD", "fHudScale", fHudScale);
        ReadFloat(ini, "HUD", "fHudOpacity", fHudOpacity);
        ReadFloat(ini, "HUD", "fTempIconX", fTempIconX);
        ReadFloat(ini, "HUD", "fTempIconY", fTempIconY);
        ReadFloat(ini, "HUD", "fTempIconScale", fTempIconScale);
        ReadUInt(ini, "HUD", "uNotifyColour", uNotifyColour);

        ReadFloat(ini, "Needs", "fSleepHoursToEmpty", fSleepHoursToEmpty);
        ReadFloat(ini, "Needs", "fHungerHoursToEmpty", fHungerHoursToEmpty);
        ReadFloat(ini, "Needs", "fSleepHoursToFull", fSleepHoursToFull);
        ReadFloat(ini, "Needs", "fSleepMinHours", fSleepMinHours);
        ReadFloat(ini, "Needs", "fCombatDrainMult", fCombatDrainMult);
        ReadFloat(ini, "Needs", "fFastFoodDrainMult", fFastFoodDrainMult);
        ReadFloat(ini, "Needs", "fBedrollSleepDrainMult", fBedrollSleepDrainMult);
        ReadFloat(ini, "Needs", "fFoodShareNormal", fFoodShareNormal);
        ReadFloat(ini, "Needs", "fFoodShareSpecial", fFoodShareSpecial);

        ReadFloat(ini, "Penalty", "fSleepSafe", fSleepSafe);
        ReadFloat(ini, "Penalty", "fHungerSafe", fHungerSafe);
        ReadFloat(ini, "Penalty", "fColdSafe", fColdSafe);
        ReadFloat(ini, "Penalty", "fPenaltyPrimary", fPenaltyPrimary);
        ReadFloat(ini, "Penalty", "fPenaltyCross", fPenaltyCross);
        ReadFloat(ini, "Penalty", "fPenaltySpeed", fPenaltySpeed);
        ReadFloat(ini, "Penalty", "fSpeedCap", fSpeedCap);
        ReadFloat(ini, "Penalty", "fPenaltyCap", fPenaltyCap);
        ReadFloat(ini, "Penalty", "fTierStep", fTierStep);
        ReadBool(ini, "Penalty", "bBonusEnabled", bBonusEnabled);
        ReadFloat(ini, "Penalty", "fBonusRegenPct", fBonusRegenPct);
        ReadFloat(ini, "Penalty", "fBonusThresholdPct", fBonusThresholdPct);

        ReadFloat(ini, "Cold", "fComfortTemp", fComfortTemp);
        ReadFloat(ini, "Cold", "fLoadPerDegree", fLoadPerDegree);
        ReadFloat(ini, "Cold", "fWetLoad", fWetLoad);
        ReadFloat(ini, "Cold", "fWetWarmthLoss", fWetWarmthLoss);
        ReadFloat(ini, "Cold", "fSwimTemp", fSwimTemp);
        ReadFloat(ini, "Cold", "fSwimChillMult", fSwimChillMult);
        ReadFloat(ini, "Cold", "fSoakMinutes", fSoakMinutes);
        ReadFloat(ini, "Cold", "fDryMinutes", fDryMinutes);
        ReadFloat(ini, "Cold", "fWarmthPerSlot", fWarmthPerSlot);
        ReadFloat(ini, "Cold", "fFrostResistWeight", fFrostResistWeight);
        ReadFloat(ini, "Cold", "fWarmthRelief", fWarmthRelief);
        ReadFloat(ini, "Cold", "fTentColdFloor", fTentColdFloor);
        ReadFloat(ini, "Cold", "fTentColdSlow", fTentColdSlow);

        // The sky, the hour, and the one temperature for everywhere the
        // baked map does not reach. All tuning, all in the MCM.
        ReadFloat(ini, "Cold", "fTempUnknown", fTempUnknown);
        ReadFloat(ini, "Cold", "fWeatherSnow", fWeatherSnow);
        ReadFloat(ini, "Cold", "fWeatherRain", fWeatherRain);
        ReadFloat(ini, "Cold", "fWeatherCloudy", fWeatherCloudy);
        ReadFloat(ini, "Cold", "fDayAmplitude", fDayAmplitude);
        ReadFloat(ini, "Cold", "fDayPeakHour", fDayPeakHour);
        ReadBool(ini, "Disease", "bDiseasesEnabled", bDiseasesEnabled);
        ReadFloat(ini, "Disease", "fColdCatchAt", fColdCatchAt);
        ReadFloat(ini, "Disease", "fColdCatchWorstAt", fColdCatchWorstAt);
        ReadFloat(ini, "Disease", "fColdCatchChanceMin", fColdCatchChanceMin);
        ReadFloat(ini, "Disease", "fColdCatchChanceMax", fColdCatchChanceMax);
        ReadFloat(ini, "Disease", "fCurePotency", fCurePotency);
        ReadFloat(ini, "Disease", "fDiseaseProgressHours", fDiseaseProgressHours);
        ReadFloat(ini, "Disease", "fDiseaseDecayHours", fDiseaseDecayHours);
        ReadFloat(ini, "Disease", "fDiseaseHitChance", fDiseaseHitChance);
        ReadBool(ini, "Disease", "bBlockStopsDisease", bBlockStopsDisease);
        ReadFloat(ini, "Disease", "fFoodPoisonChance", fFoodPoisonChance);
        ReadBool(ini, "Disease", "bRawFoodStagger", bRawFoodStagger);
        ReadFloat(ini, "Disease", "fRawFoodStaggerForce", fRawFoodStaggerForce);
        ReadBool(ini, "Disease", "bRfabDiseasesEnabled", bRfabDiseasesEnabled);
        ReadBool(ini, "Disease", "bCoughEnabled", bCoughEnabled);
        ReadFloat(ini, "Disease", "fCoughRateMult", fCoughRateMult);
        ReadFloat(ini, "Disease", "fCoughVolume", fCoughVolume);
        ReadBool(ini, "Disease", "bElemLesionEnabled", bElemLesionEnabled);
        ReadFloat(ini, "Disease", "fElemLesionColdAt", fElemLesionColdAt);
        ReadFloat(ini, "Disease", "fElemLesionHypoChance", fElemLesionHypoChance);
        ReadFloat(ini, "Disease", "fElemLesionHitP", fElemLesionHitP);
        ReadFloat(ini, "Disease", "fElemLesionContractP", fElemLesionContractP);

        ReadBool(ini, "Hypothermia", "bHypothermiaEnabled", bHypothermiaEnabled);
        ReadBool(ini, "Hypothermia", "bHypoBlocksRest", bHypoBlocksRest);
        ReadFloat(ini, "Hypothermia", "fHypoThreshold", fHypoThreshold);
        ReadFloat(ini, "Hypothermia", "fHypoRecoverThreshold", fHypoRecoverThreshold);
        ReadFloat(ini, "Hypothermia", "fHypoWorsenHours", fHypoWorsenHours);
        ReadFloat(ini, "Hypothermia", "fHypoRecoverHours", fHypoRecoverHours);
        ReadFloat(ini, "Hypothermia", "fHypoDrainPerSec", fHypoDrainPerSec);
        ReadFloat(ini, "Hypothermia", "fHypoDrainRamp", fHypoDrainRamp);

        ReadFloat(ini, "Camp", "fTreeChopCooldownHours", fTreeChopCooldownHours);
        ReadFloat(ini, "Camp", "fMaxPlacementTilt", fMaxPlacementTilt);
        ReadFloat(ini, "Camp", "fCookGearZ", fCookGearZ);
        ReadFloat(ini, "Camp", "fCampfireBurnHours", fCampfireBurnHours);
        ReadFloat(ini, "Camp", "fCampfireBurnHoursPerk", fCampfireBurnHoursPerk);
        ReadFloat(ini, "Camp", "fCampfireFuel", fCampfireFuel);
        ReadFloat(ini, "Camp", "fCampfireCooldown", fCampfireCooldown);
        ReadBool(ini, "Cold", "bWarmAnim", bWarmAnim);
        ReadFloat(ini, "Cold", "fWarmAnimDelay", fWarmAnimDelay);
        ReadFloat(ini, "Cold", "fWarmAnimRadius", fWarmAnimRadius);
        ReadBool(ini, "Cold", "bColdBlocksTeleport", bColdBlocksTeleport);
        ReadBool(ini, "Cold", "bColdShaderEnabled", bColdShaderEnabled);
        ReadFloat(ini, "Cold", "fColdShaderAt", fColdShaderAt);
        ReadBool(ini, "Cold", "bColdScreenEnabled", bColdScreenEnabled);
        ReadFloat(ini, "Cold", "fInteriorTemp", fInteriorTemp);
        ReadFloat(ini, "Cold", "fColdInteriorTemp", fColdInteriorTemp);
        ReadFloat(ini, "Cold", "fFireRadius", fFireRadius);
        ReadFloat(ini, "Cold", "fFireShare", fFireShare);
        ReadFloat(ini, "Cold", "fFireMaxDeg", fFireMaxDeg);
        ReadFloat(ini, "Cold", "fTorchOfFire", fTorchOfFire);
        ReadFloat(ini, "Cold", "fElemDamageShare", fElemDamageShare);
        ReadFloat(ini, "Cold", "fColdChillRate", fColdChillRate);
        ReadFloat(ini, "Cold", "fColdWarmRate", fColdWarmRate);
        ReadFloat(ini, "Cold", "fColdChillEase", fColdChillEase);
        ReadFloat(ini, "Cold", "fWarmthSlowsChill", fWarmthSlowsChill);
        ReadFloat(ini, "Debug", "fMarkerLinger", fMarkerLinger);
        ReadFloat(ini, "Cold", "fColdPerResistPoint", fColdPerResistPoint);
    }

    // Each reader overwrites only when the key is actually present, so a partial
    // file layers cleanly on top of the defaults instead of blanking them.
    void Settings::ReadBool(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool& a_out)
    {
        if (const auto* v = a_ini.GetValue(a_section, a_key)) {
            a_out = a_ini.GetBoolValue(a_section, a_key, a_out);
            (void)v;
        }
    }

    void Settings::ReadFloat(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, float& a_out)
    {
        if (a_ini.GetValue(a_section, a_key)) {
            a_out = static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, a_out));
        }
    }

    void Settings::ReadUInt(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t& a_out)
    {
        if (a_ini.GetValue(a_section, a_key)) {
            a_out = static_cast<std::uint32_t>(a_ini.GetLongValue(a_section, a_key, static_cast<long>(a_out)));
        }
    }
}
