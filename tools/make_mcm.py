#!/usr/bin/env python3
"""Build the MCM from the settings the plugin actually reads.

The menu used to be emitted by the record generator, because every row pointed
at a GLOB and only the generator knew the form ids. It no longer does: MCM
Helper's ModSetting rows address a plain ini key, so the menu needs nothing from
the .esp at all - and the 109 GLOBs it needed are gone with it.

What it does need is to agree with Settings.h, exactly, forever. So both files
are read from there rather than typed twice:

    MCM/Config/RFAB_SurvivalLayer/config.json   the menu layout
    MCM/Config/RFAB_SurvivalLayer/settings.ini  the defaults the plugin loads first

The layout below is written by hand because the order and the grouping are a
design decision, not something to derive - but it is CHECKED against the plugin:
a setting the plugin reads and the layout does not place is an error, and so is
a layout row naming a setting that no longer exists. Neither can become a
silently dead slider.

The translation keys are checked the same way, against the single Russian
source the rest of the mod's text comes from.

Usage:
    python tools/make_mcm.py
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "native" / "src" / "Settings.h"
SOURCE = ROOT / "native" / "src" / "Settings.cpp"
STRINGS = ROOT / "SSEEdit_Scripts" / "RFAB_SurvivalLayer_strings.txt"
OUT = ROOT / "MCM" / "Config" / "RFAB_SurvivalLayer"
PAPYRUS = ROOT / "scripts" / "source" / "_RSL_MCM.psc"

# The quest the MCM script is attached to, as MCM Helper names forms:
# "<plugin>|<local form id>". _RSL_QstMCM, read off the plugin.
MCM_QUEST = "RFAB_SurvivalLayer.esp|913"

# Ranges. A slider that cannot reach the value the designer wants is the same
# bug as a missing row, so every one of them is named here rather than guessed.
RANGES = {
    "fHudX": (0, 1280, 1),
    "fHudY": (0, 720, 1),
    "fTempIconX": (0, 1280, 1),
    "fTempIconY": (0, 720, 1),
    "fHudScale": (0.5, 2.0, 0.05),
    "fTempIconScale": (0.5, 2.0, 0.05),
    "fHudOpacity": (0.0, 1.0, 0.05),
    "fBarWidth": (40, 600, 1),
    "fBarHeight": (4, 60, 1),
    "fRowPitch": (4, 80, 1),
    "fIconSize": (0, 80, 1),
    "fIconGap": (0, 60, 1),
    "fTempIconSize": (4, 120, 1),
    "fFrameMid": (0.0, 1.0, 0.05),
    "fSleepSafe": (0.0, 1.0, 0.05),
    "fHungerSafe": (0.0, 1.0, 0.05),
    "fColdSafe": (0.0, 1.0, 0.05),

    "fSleepHoursToEmpty": (24, 168, 1),
    "fHungerHoursToEmpty": (24, 168, 1),
    "fSleepHoursToFull": (1, 24, 1),
    "fSleepMinHours": (0, 8, 1),
    "fFastFoodDrainMult": (1, 20, 1),
    "fBedrollSleepDrainMult": (1, 20, 1),
    "fCombatDrainMult": (1, 15, 1),
    "fFoodShareNormal": (0.0, 2.0, 0.05),
    "fFoodShareSpecial": (0.0, 3.0, 0.05),

    "fComfortTemp": (-10, 30, 1),
    "fLoadPerDegree": (0.0, 0.2, 0.005),
    "fWetLoad": (0.0, 1.0, 0.05),
    "fWetWarmthLoss": (0.0, 1.0, 0.05),
    "fSwimTemp": (-60, 0, 1),
    "fSwimChillMult": (1, 20, 0.5),
    "fWarmthPerSlot": (0, 30, 1),
    "fFrostResistWeight": (0.0, 2.0, 0.05),
    "fWarmthRelief": (0.0, 0.05, 0.001),
    "fInteriorTemp": (-20, 30, 1),
    "fColdInteriorTemp": (-20, 30, 1),
    "fFireRadius": (100, 2000, 50),
    "fFireShare": (0.0, 1.0, 0.01),
    "fFireMaxDeg": (0, 30, 0.5),
    "fTorchOfFire": (0.0, 1.0, 0.01),
    "fTentColdFloor": (0.0, 0.5, 0.01),
    "fTentColdSlow": (0.0, 1.0, 0.05),

    "fTempUnknown": (-40, 30, 0.1),
    "fWeatherSnow": (-20, 0, 0.1),
    "fWeatherRain": (-20, 0, 0.1),
    "fWeatherCloudy": (-20, 0, 0.1),
    "fDayAmplitude": (-20, 0, 0.1),
    "fDayPeakHour": (0, 23, 1),
    "fSoakMinutes": (1, 60, 1),
    "fDryMinutes": (1, 60, 1),
    "fColdChillRate": (0.05, 5.0, 0.05),
    "fColdWarmRate": (0.5, 30.0, 0.5),
    "fColdChillEase": (0.0, 0.5, 0.01),
    "fWarmthSlowsChill": (0.0, 0.05, 0.001),
    "fMarkerLinger": (1, 10, 0.1),
    "fColdPerResistPoint": (0.0, 0.05, 0.001),
    "fElemDamageShare": (0.0, 1.0, 0.1),
    "fColdShaderAt": (0.0, 1.0, 0.05),
    "fWarmAnimDelay": (2, 20, 1),
    "fWarmAnimRadius": (50, 800, 10),

    "fMaxPlacementTilt": (0, 89, 1),
    "fCookGearZ": (-60, 60, 1),
    "fCampfireBurnHours": (1, 24, 1),
    "fCampfireBurnHoursPerk": (1, 48, 1),
    "fCampfireFuel": (1, 10, 1),
    "fCampfireCooldown": (0, 30, 1),
    "fTreeChopCooldownHours": (0, 72, 1),

    "fCurePotency": (0, 100, 1),
    "fCoughRateMult": (0.1, 5.0, 0.1),
    "fCoughVolume": (0.0, 5.0, 0.01),
    "fDiseaseProgressHours": (5, 120, 1),
    "fDiseaseDecayHours": (5, 240, 1),
    "fDiseaseHitChance": (0, 100, 1),
    "fFoodPoisonChance": (0, 100, 1),
    "fRawFoodStaggerForce": (0.0, 1.0, 0.05),
    "fColdCatchAt": (0.0, 1.0, 0.05),
    "fColdCatchWorstAt": (0.0, 1.0, 0.05),
    "fColdCatchChanceMin": (0, 100, 1),
    "fColdCatchChanceMax": (0, 100, 1),
    "fElemLesionColdAt": (0.0, 1.0, 0.05),
    "fElemLesionHypoChance": (0, 100, 1),
    "fElemLesionHitP": (-20, 0, 1),
    "fElemLesionContractP": (10, 100, 1),

    "fHypoThreshold": (0.0, 1.0, 0.05),
    "fHypoRecoverThreshold": (0.0, 1.0, 0.05),
    "fHypoWorsenHours": (0.25, 24, 0.25),
    "fHypoRecoverHours": (1, 24, 1),
    "fHypoDrainPerSec": (0, 10, 1),
    "fHypoDrainRamp": (5, 120, 1),

    "fPenaltyPrimary": (0, 100, 1),
    "fPenaltyCross": (0, 100, 1),
    "fPenaltySpeed": (0, 50, 1),
    "fSpeedCap": (0, 80, 1),
    "fPenaltyCap": (0, 95, 1),
    "fTierStep": (1, 25, 1),
    "fBonusRegenPct": (0, 200, 1),
    "fBonusThresholdPct": (0, 50, 1),
}

# Settings the plugin reads that are deliberately NOT in the menu.
#
# They still appear in settings.ini and are still read, so anyone who wants one
# can set it by hand; what they are not is a row every player has to scroll
# past. Each is here for one of two reasons: it is measured once and never
# chosen (mesh alignment, a coefficient of the mountain line), or it switches
# off a feature that IS the mod (the campfire, chopping wood).
#
# This is not an escape hatch from the check below - a name here must still be
# read by the plugin, and must not also sit on a page.
HIDDEN = {
    "fSleepMinHours",        # the floor under one night, set once
    "fFoodShareNormal",      # what a kilo of food is worth: balance, not taste
    "fFoodShareSpecial",
    "fWarmAnimRadius",       # the animation's reach and its patience - both
    "fWarmAnimDelay",        # measured against the mesh, not chosen
    "fHypoDrainPerSec",      # the shape of the third stage's damage
    "fHypoDrainRamp",
    "fWarmthSlowsChill",     # how much a coat slows the chill: one calibration
    "fColdPerResistPoint",   # how much delay a frost potion buys, tuned once
    "bRawFoodStagger",       # raw food knocking you about: on, or edit the ini
    "fRawFoodStaggerForce",
    "fCookGearZ",            # measured off the mesh in game, not a preference
    "fCampfireCooldown",     # a guard against double-casting, not a choice
}

# The settings pages. Every setting the plugin reads appears exactly once, here
# or in HIDDEN, and main() will refuse to write anything if that stops being
# true.
PAGES = [
    ("_RSL_PageGeneral", [
        ("set", "bModEnabled"),
        ("btn", "_RSL_BtnReset", "ResetDefaults"),
        ("hdr", "_RSL_HdrColdVisual"),
        ("set", "bColdShaderEnabled"), ("set", "fColdShaderAt"),
        ("set", "bColdScreenEnabled"), ("set", "bColdBlocksTeleport"),
        ("hdr", "_RSL_HdrWarmAnim"),
        ("set", "bWarmAnim"),
    ]),
    ("_RSL_PageHud", [
        ("hdr", "_RSL_HdrHudPos"),
        ("set", "fHudX"), ("set", "fHudY"),
        ("set", "fHudScale"), ("set", "fHudOpacity"),
        ("hdr", "_RSL_HdrHudNotify"),
        ("set", "uNotifyColour"),
        ("hdr", "_RSL_HdrHudTemp"),
        ("set", "fTempIconX"), ("set", "fTempIconY"), ("set", "fTempIconScale"),
        ("hdr", "_RSL_HdrWidgetGeom"),
        ("set", "fBarWidth"), ("set", "fBarHeight"), ("set", "fRowPitch"),
        ("set", "fIconSize"), ("set", "fIconGap"), ("set", "fTempIconSize"),
        ("set", "fFrameMid"), ("set", "fMarkerLinger"),
    ]),
    ("_RSL_PageNeeds", [
        ("hdr", "_RSL_HdrSleepCurve"),
        ("set", "fSleepHoursToEmpty"), ("set", "fSleepHoursToFull"),
        ("hdr", "_RSL_HdrHungerCurve"),
        ("set", "fHungerHoursToEmpty"),
        ("hdr", "_RSL_HdrCombat"),
        ("set", "fCombatDrainMult"), ("set", "fFastFoodDrainMult"),
        ("set", "fBedrollSleepDrainMult"),
    ]),
    ("_RSL_PageCold", [
        ("hdr", "_RSL_HdrColdModel"),
        ("set", "fComfortTemp"), ("set", "fLoadPerDegree"),
        ("set", "fColdChillRate"), ("set", "fColdWarmRate"),
        ("set", "fColdChillEase"),
        ("hdr", "_RSL_HdrWarmth"),
        ("set", "fWarmthPerSlot"), ("set", "fFrostResistWeight"),
        ("set", "fWarmthRelief"),
        ("hdr", "_RSL_HdrHeat"),
        ("set", "fInteriorTemp"), ("set", "fColdInteriorTemp"),
        ("set", "fFireRadius"), ("set", "fFireShare"),
        ("set", "fFireMaxDeg"), ("set", "fTorchOfFire"),
        ("hdr", "_RSL_HdrWet"),
        ("set", "fSwimTemp"), ("set", "fSwimChillMult"),
        ("set", "fWetLoad"), ("set", "fWetWarmthLoss"),
        ("set", "fSoakMinutes"), ("set", "fDryMinutes"),
        ("hdr", "_RSL_HdrElemHit"),
        ("set", "fElemDamageShare"),
    ]),
    ("_RSL_PageClimate", [
        ("hdr", "_RSL_HdrOffMap"),
        ("txt", ["_RSL_HdrOffMapNote"]),
        ("set", "fTempUnknown"),
        ("hdr", "_RSL_HdrSky"),
        ("set", "fWeatherSnow"), ("set", "fWeatherRain"),
        ("set", "fWeatherCloudy"),
        ("set", "fDayAmplitude"), ("set", "fDayPeakHour"),
    ]),
    ("_RSL_PageCamp", [
        ("hdr", "_RSL_HdrPlacement"),
        ("set", "fMaxPlacementTilt"),
        ("hdr", "_RSL_HdrCampfire"),
        ("set", "fCampfireBurnHours"), ("set", "fCampfireBurnHoursPerk"),
        ("set", "fCampfireFuel"),
        ("hdr", "_RSL_HdrTent"),
        ("set", "fTentColdFloor"), ("set", "fTentColdSlow"),
        ("hdr", "_RSL_HdrWood"),
        ("set", "fTreeChopCooldownHours"),
    ]),
    ("_RSL_PageDisease", [
        ("hdr", "_RSL_HdrDisease"),
        ("set", "bDiseasesEnabled"), ("set", "fCurePotency"),
        ("set", "fDiseaseProgressHours"),
        ("set", "fDiseaseDecayHours"), ("set", "fDiseaseHitChance"),
        ("set", "bBlockStopsDisease"),
        ("set", "fFoodPoisonChance"),
        ("hdr", "_RSL_HdrColdCatch"),
        ("set", "fColdCatchAt"), ("set", "fColdCatchWorstAt"),
        ("set", "fColdCatchChanceMin"), ("set", "fColdCatchChanceMax"),
        ("hdr", "_RSL_HdrRfabDz"),
        ("set", "bRfabDiseasesEnabled"),
        ("set", "bCoughEnabled"), ("set", "fCoughRateMult"),
        ("set", "fCoughVolume"),
        ("hdr", "_RSL_HdrElemLesion"),
        ("set", "bElemLesionEnabled"), ("set", "fElemLesionColdAt"),
        ("set", "fElemLesionHypoChance"), ("set", "fElemLesionHitP"),
        ("set", "fElemLesionContractP"),
        ("hdr", "_RSL_HdrHypo"),
        ("set", "bHypothermiaEnabled"), ("set", "bHypoBlocksRest"),
        ("set", "fHypoThreshold"),
        ("set", "fHypoRecoverThreshold"), ("set", "fHypoWorsenHours"),
        ("set", "fHypoRecoverHours"),
    ]),
    ("_RSL_PagePenalty", [
        ("hdr", "_RSL_HdrThreshold"),
        ("set", "fSleepSafe"), ("set", "fHungerSafe"), ("set", "fColdSafe"),
        ("hdr", "_RSL_HdrPenalty"),
        ("set", "fPenaltyPrimary"), ("set", "fPenaltyCross"),
        ("set", "fPenaltySpeed"), ("set", "fSpeedCap"),
        ("set", "fPenaltyCap"), ("set", "fTierStep"),
        ("hdr", "_RSL_HdrBonus"),
        ("set", "bBonusEnabled"), ("set", "fBonusRegenPct"),
        ("set", "fBonusThresholdPct"),
    ]),
    ("_RSL_PageDebug", [
        ("hdr", "_RSL_HdrDebug"),
        ("set", "bDebugLog"), ("set", "bTracePass"),
        ("btn", "_RSL_BtnResetDiseases", "ResetIllnesses"),
    ]),
]


# What a C++ type means to the menu. A colour is an int to MCM Helper and a
# swatch with a picker to the player, which is the whole reason the type is
# carried through here rather than assumed from the name.
KINDS = {"bool": "bool", "float": "float", "std::uint32_t": "colour"}


def defaults():
    """key -> (literal, kind), read off the inline initialisers."""
    text = HEADER.read_text(encoding="utf-8")
    found = {}
    for kind, name, value in re.findall(
            r"static inline (bool|float|std::uint32_t)\s+(\w+)\s*\{\s*([^}]+?)\s*\}",
            text):
        found[name] = (value.strip(), KINDS[kind])
    return found


def sections():
    """key -> ini section, read off the ReadFloat/ReadBool calls."""
    text = SOURCE.read_text(encoding="utf-8")
    found = {}
    for section, name in re.findall(
            r'Read(?:Float|Bool|UInt)\(ini,\s*"(\w+)",\s*"(\w+)"', text):
        found[name] = section
    return found


def translation_keys():
    """Every $KEY the single Russian source defines."""
    keys = set()
    for line in STRINGS.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("mcm.") and "=" in line:
            keys.add(line[4:].partition("=")[0].strip())
    return keys


def literal(value):
    """Turn the C++ initialiser into a number. Expressions are evaluated.

    The booleans are tested BEFORE the float suffix is stripped: doing it the
    other way turns "false" into "alse".
    """
    v = value.strip()
    if v in ("true", "false"):
        return 1 if v == "true" else 0
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", v):
        return int(v, 16)          # before the suffix strip, which eats an "f"
    return eval(v.replace("f", ""), {"__builtins__": {}})   # our own header only


def fmt(step):
    """Decimals the slider shows. SkyUI reads the index as a precision.

    Taken from the step itself rather than from its magnitude. The old rule was
    "0.1 or coarser gets one decimal", which is wrong for any step that is not a
    power of ten: a 0.25 step displayed 0.25 as "0.3", so the hypothermia slider
    looked as if it could not go below 0.3 when it was already sitting on 0.25.
    """
    text = "%g" % step
    places = len(text.split(".")[1]) if "." in text else 0
    return "{%d}" % min(places, 4)


def main():
    values = defaults()
    where = sections()

    # --- the layout has to cover the plugin, exactly ----------------------
    # row[0] is the kind and row[1] the name; a button carries a third
    # field and nothing else does.
    placed = [row[1] for _, rows in PAGES for row in rows if row[0] == "set"]
    errors = []
    for name in sorted(set(where) - set(placed) - HIDDEN):
        errors.append(f"{name} is read by the plugin but is on no MCM page")
    for name in sorted(HIDDEN - set(where)):
        errors.append(f"{name} is hidden from the menu but the plugin never reads it")
    for name in sorted(HIDDEN & set(placed)):
        errors.append(f"{name} is both hidden and on a page")
    for name in sorted(set(placed) - set(where)):
        errors.append(f"{name} is on an MCM page but the plugin never reads it")
    for name in sorted({n for n in placed if placed.count(n) > 1}):
        errors.append(f"{name} appears on more than one MCM page")
    for name in placed:
        if name in where and name not in values:
            errors.append(f"{name} is read but has no compiled-in default")
        if name.startswith("f") and name not in RANGES:
            errors.append(f"{name} has no range - add it to RANGES")
    if errors:
        for e in errors:
            print(f"  !! {e}")
        return 1

    # --- config.json ------------------------------------------------------
    pages = []
    wanted = set()

    for title, rows in PAGES:
        content = []
        wanted.add(f"${title}")
        for row in rows:
            kind, name = row[0], row[1]
            # Only a button carries a third field: the Papyrus function it
            # calls. Everything else is a pair.
            fn = row[2] if len(row) > 2 else None
            if kind == "hdr":
                content.append({"text": f"${name}", "type": "header"})
                wanted.add(f"${name}")
                continue
            # A line of explanation, where the page needs one:
            # "name" is a list of keys, not a single one.
            if kind == "txt":
                for key in name:
                    content.append({"text": f"${key}", "type": "text"})
                    wanted.add(f"${key}")
                continue
            # A button, and the function it calls lives in _RSL_MCM.psc.
            #
            # MCM Helper has no bulk "reset to defaults" of its own - the R
            # key resets ONE option, from settings.ini - so the loop over every
            # setting is ours, in Papyrus. The illness reset is the other kind:
            # Papyrus only forwards it, and the work is native.
            if kind == "btn":
                content.append({
                    "text": f"${name}",
                    "help": f"${name}_help",
                    "type": "text",
                    "action": {
                        "type": "CallFunction",
                        "form": MCM_QUEST,
                        "scriptName": "_RSL_MCM",
                        "function": fn,
                    },
                })
                wanted.add(f"${name}")
                wanted.add(f"${name}_help")
                continue
            section = where[name]
            wanted.add(f"$_RSL_{name}")
            wanted.add(f"$_RSL_{name}_help")
            # The setting is named by the row's own "id", as "key:section".
            # It is NOT a field inside valueOptions: MCM Helper validates that
            # object strictly and refuses the whole config with
            # "Unexpected key: modSettingName", which reads like a typo and is
            # really the wrong level. Checked against TrueHUD's own config.
            row = {
                "id": f"{name}:{section}",
                "text": f"$_RSL_{name}",
                "help": f"$_RSL_{name}_help",
            }
            if values[name][1] == "bool":
                row["type"] = "toggle"
                row["valueOptions"] = { "sourceType": "ModSettingBool" }
            elif values[name][1] == "colour":
                # SkyUI's own colour picker, which MCM Helper drives through
                # OnOptionColorOpen/Accept. No range and no format: the swatch
                # is the value.
                row["type"] = "color"
                row["valueOptions"] = { "sourceType": "ModSettingInt" }
            else:
                lo, hi, step = RANGES[name]
                row["type"] = "slider"
                row["valueOptions"] = {
                    "min": lo,
                    "max": hi,
                    "step": step,
                    "formatString": fmt(step),
                    "sourceType": "ModSettingFloat",
                }
            content.append(row)
        pages.append({
            "pageDisplayName": f"${title}",
            "cursorFillMode": "topToBottom",
            "content": content,
        })

    config = {
        "modName": "RFAB_SurvivalLayer",
        # Plain text, not a translation key: configs register before
        # translations load, and an unresolved key in displayName silently keeps
        # the mod out of the menu entirely.
        "displayName": "RFAB Survival",
        "pages": pages,
    }

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "config.json").write_text(
        json.dumps(config, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    # --- settings.ini -----------------------------------------------------
    # The defaults the plugin reads before the user's own, written from the same
    # table, so a new setting appears in both files or in neither.
    #
    # The hidden ones are written too: this file is the plugin's defaults, not
    # the menu's, and a setting kept out of the menu is exactly the one somebody
    # will come here to change by hand.
    rows = {}
    for name in placed + sorted(HIDDEN):
        rows.setdefault(where[name], []).append(name)

    lines = ["; AUTO-GENERATED by tools/make_mcm.py -- do not edit by hand.",
             "; Defaults for every setting the plugin reads. MCM writes the",
             "; player's changes to Data/MCM/Settings/RFAB_SurvivalLayer.ini,",
             "; which is loaded after this and wins.", ""]
    for section in sorted(rows):
        lines.append(f"[{section}]")
        for name in rows[section]:
            raw, kind = values[name]
            value = literal(raw)
            if kind == "bool":
                lines.append(f"{name} = {int(value)}")
            elif kind == "colour":
                # SimpleIni reads an 0x prefix as hex, and a colour written
                # decimal is unreadable to anyone opening this file. MCM writes
                # the player's own choice decimal into the user ini; both parse.
                lines.append(f"{name} = 0x{int(value):06X}")
            else:
                lines.append(f"{name} = {value}")
        lines.append("")
    (OUT / "settings.ini").write_text("\n".join(lines), encoding="utf-8")

    # --- _RSL_MCM.psc -----------------------------------------------------
    # The reset button's other half, written from the same table as
    # settings.ini so the two cannot say different things. v0.4.0 generated its
    # reset the same way (_RSL_Balance.ResetDefaults, which set every GLOB);
    # there are no GLOBs now, so it goes through MCM Helper's own setters -
    # which is what makes the menu show the new values rather than merely
    # behave by them.
    psc = [
        "Scriptname _RSL_MCM extends MCM_ConfigBase",
        "{MCM menu registration. Layout lives entirely in",
        " MCM/Config/RFAB_SurvivalLayer/config.json (the base class reads it on",
        " OnConfigManagerReady).",
        "",
        " ResetDefaults below is AUTO-GENERATED by tools/make_mcm.py from the",
        " same defaults as settings.ini - do not edit it by hand, and re-run the",
        " generator plus tools/compile_mcm.bat after adding a setting.}",
        "",
        "Function ResetDefaults()",
        "  ; Written by tools/make_mcm.py. One line per setting the plugin reads.",
    ]
    for name in placed:
        raw, kind = values[name]
        value = literal(raw)
        ident = f'"{name}:{where[name]}"'
        if kind == "bool":
            psc.append(f"  SetModSettingBool({ident}, {'true' if value else 'false'})")
        elif kind == "colour":
            psc.append(f"  SetModSettingInt({ident}, {int(value)})")
        else:
            psc.append(f"  SetModSettingFloat({ident}, {float(value):.6f})")
    psc += [
        "  ; Redraw, or the page keeps showing what it showed before the reset.",
        "  RefreshMenu()",
        "EndFunction",
        "",
        "; Take every illness off the player - the same thing switching the mod",
        "; off does to them, and nothing else. For an illness that has got stuck.",
        ";",
        "; Papyrus only forwards it. The work is native, because the stage, the",
        "; accumulator and the spells are all held there and a script has no way",
        "; to reach them.",
        "Function ResetIllnesses()",
        "  ResetIllnessesNative()",
        "EndFunction",
        "",
        "; Bound by the plugin at load - see native/src/Papyrus.cpp.",
        "Function ResetIllnessesNative() global native",
        "",
    ]
    PAPYRUS.write_text(chr(10).join(psc), encoding="utf-8")
    print(f"  _RSL_MCM.psc {len(placed)} settings in ResetDefaults()")

    total = sum(len(p["content"]) for p in pages)
    print(f"  config.json  {len(pages)} pages, {total} rows")
    print(f"  settings.ini {len(placed) + len(HIDDEN)} defaults "
          f"({len(HIDDEN)} not in the menu)")

    # --- and every key it names has to have text --------------------------
    missing = sorted(wanted - translation_keys())
    if missing:
        print(f"  !! {len(missing)} translation keys missing from "
              f"{STRINGS.name}:")
        for key in missing:
            print(f"       {key}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
