#pragma once

// Settings live in INI files that MCM Helper reads and writes, and the plugin
// reads directly. There is no Papyrus round trip and no GlobalVariable per
// setting - the old design needed 109 GLOBs plus a generated ResetDefaults()
// plus a SETTINGS_VERSION migration, and all three disappear here.
//
// Two files are read in order, the second overriding the first:
//
//   Data/MCM/Config/RFAB_SurvivalLayer/settings.ini   defaults shipped with the mod
//   Data/MCM/Settings/RFAB_SurvivalLayer.ini          what MCM Helper writes
//
// A key that is missing from both keeps the compiled-in default below, so
// adding a setting never breaks an existing install.

namespace RSL
{
    struct Settings
    {
        static void ReadSettings();

        // --- master ---
        static inline bool bModEnabled{ true };
        // On while the model is being tuned; the cold sample is what needs
        // watching and there is no other way to see its terms.
        static inline bool bDebugLog{ true };

        // Logs a marker before each step of the gameplay pass. Only for
        // locating a crash inside it: the pass runs every frame, so this
        // produces thousands of lines a minute.
        static inline bool bTracePass{ false };

        // --- HUD placement ---
        // Pixels of the fixed 1280x720 stage, which is exactly the unit v0.4.0
        // used (HudWidgetX/Y) and the one RFAB's own Interface.ini uses. Under
        // kShowAll that stage is scaled and centred as a whole, so a stage pixel
        // is the same place on screen at any resolution - see Menu/RSLMenu.h.
        static inline float fHudX{ 55.0f };
        static inline float fHudY{ 615.0f };
        static inline float fHudScale{ 1.0f };
        static inline float fHudOpacity{ 1.0f };

        // The temperature indicator is its own widget, placed and sized apart
        // from the bars - v0.4.0 splits it out the same way (HudTempX/Y/Scale).
        // Sharing the bars' scale would drag it around with them.
        static inline float fTempIconX{ 621.0f };
        static inline float fTempIconY{ 60.0f };
        static inline float fTempIconScale{ 1.0f };

        // Widget geometry, in stage units. These were compile-time constants
        // in Menu/HudLayout.h and are settings so the layout can be dialled in
        // game and only then written back as defaults. The bar block rebuilds
        // itself when any of them changes.
        //
        // BAR_W/BAR_H are matched to TrueHUD's player bar: its coloured strip
        // ends up 245x10 on screen, and our frame and pad take 2 units a side.
        static inline float fBarWidth{ 150.0f };
        static inline float fBarHeight{ 13.0f };
        static inline float fRowPitch{ 21.0f };
        static inline float fIconSize{ 19.0f };
        static inline float fIconGap{ 12.0f };
        static inline float fTempIconSize{ 41.0f };

        // How bright the middle of the bar's frame is, as a fraction of
        // the colour at its ends: 0 is black, 1 a frame of one colour all
        // the way along. Black was the first reading of "a gradient on the
        // frame" and it cut the bar in half at a distance; this is the
        // same gradient with somewhere to stop. A fraction rather than a
        // grey level so the hue of FRAME_GREY is kept at every setting.
        static inline float fFrameMid{ 0.5f };

        // Corner notifications, 0xRRGGBB. The colour is not a widget of ours:
        // Debug.Notification reaches HUDMenu.ShowMessage, whose clip sets
        // tf1.html = true, so whatever arrives is parsed as HTML and a colour
        // is a tag around the string.
        //
        // A light blue, the owner's pick. It carries against both a dark
        // interior and a snowfield, which the axis hues do not: those are bar
        // fills, sitting under a lightening ramp and a white marker, and on
        // their own against the HUD they are too dark to read as text.
        static inline std::uint32_t uNotifyColour{ 0xADD8E6 };


        // --- needs ---
        // Game hours from a full bar to an empty one, with nothing else acting
        // on it. At RFAB's TimeScale of 10 a game hour is six real minutes.
        //
        // These are v0.4.0's numbers: SleepMax and HungerMax were both 48, and
        // SleepRestorePerHour 6 against a 48-point bar is eight hours to full.
        // An earlier version of this file guessed 20 and 16 instead, which made
        // a day of waiting empty the rest bar outright.
        static inline float fSleepHoursToEmpty{ 48.0f };
        static inline float fHungerHoursToEmpty{ 48.0f };

        // Game hours of sleep that take an empty bar back to full.
        static inline float fSleepHoursToFull{ 8.0f };

        // v0.4.0 carries a SleepMinHours of 1, but the game will not let you
        // sleep for less than an hour in the first place, so the floor could
        // only ever reject a legitimate one-hour nap whose measured span came
        // out a hair under. Kept as a setting, off by default.
        static inline float fSleepMinHours{ 0.0f };

        // How much of the hunger bar food restores: its weight times a share.
        // The Papyrus model used the same shape - half a bar per unit of weight
        // for ordinary food, a full bar for a prepared dish.
        static inline float fFoodShareNormal{ 0.50f };
        static inline float fFoodShareSpecial{ 1.00f };

        // Combat burns through sleep and hunger faster. The cold axis uses the
        // inverse of this - heat is lost more slowly while fighting.
        static inline float fCombatDrainMult{ 5.0f };

        // How much faster the fast half of the hunger bar burns. Its own
        // setting and not a second use of fCombatDrainMult: the two mean
        // different things, and sharing one would mean that tuning combat drain
        // silently changes how filling an apple is, with nothing to say so.
        //
        // Three, not five. Combat ADDS to this rather than multiplying it, so
        // five put an apple in a fight at ten times the ordinary rate, which
        // spent the half faster than a fight lasts.
        static inline float fFastFoodDrainMult{ 3.0f };

        // The same idea on the sleep axis: how much faster the half earned in a
        // bedroll burns. Its own setting for the same reason as the one above -
        // these are three different facts that happen to be 5 today, and
        // sharing one number would mean tuning combat silently changes what a
        // night in a bedroll is worth, with nothing to say so.
        static inline float fBedrollSleepDrainMult{ 3.0f };

        // --- penalties ---
        // Where each axis stops being "safe": the point the penalty ramp starts
        // from, as a fraction of a full bar. From v0.4.0's grace values against
        // each axis maximum - sleep 16/48, hunger 12/48, cold 25/100.
        //
        // The widget's notch is drawn at these, it does not have its own: a
        // notch that could disagree with the threshold would be a lie about
        // where the penalty begins.
        static inline float fSleepSafe{ 1.0f - 16.0f / 48.0f };
        static inline float fHungerSafe{ 1.0f - 12.0f / 48.0f };
        static inline float fColdSafe{ 1.0f - 25.0f / 100.0f };

        // v0.4.0's numbers, all multiples of five like the rest of RFAB's
        // stat sheet. SpeedCap is the important one: RFAB locks the player in
        // place at SpeedMult <= 0 and its own burden already takes up to 50.
        static inline float fPenaltyPrimary{ 60.0f };
        static inline float fPenaltyCross{ 10.0f };
        static inline float fPenaltySpeed{ 10.0f };
        static inline float fSpeedCap{ 30.0f };
        static inline float fPenaltyCap{ 85.0f };
        static inline float fTierStep{ 5.0f };

        // Full-bar bonuses: while an axis stays within fBonusThresholdPct of
        // full, the matching pool regenerates fBonusRegenPct faster. Flat, not
        // ramped.
        static inline bool  bBonusEnabled{ true };
        static inline float fBonusRegenPct{ 25.0f };
        static inline float fBonusThresholdPct{ 15.0f };

        // --- cold ---
        //
        // Every number from here down that is not marked otherwise was solved
        // offline against the owner's target table by tools/fit_climate.py.
        // The table is the specification; the solver's target-against-result
        // report, all 19 rows, is what says how close each one lands. Retune
        // by editing the table and rerunning it, not by nudging these by feel -
        // one of them moved by hand moves rows that were already right.
        //
        // The temperature at which the player is comfortable. Below it, every
        // degree adds load; above it, none does.
        static inline float fComfortTemp{ 12.0f };
        static inline float fLoadPerDegree{ 0.055f };
        static inline float fWetLoad{ 0.227f };

        // Wet clothes stop being clothes. At full soaking this much of the
        // warmth they were giving is gone - v0.4.0 folded the same idea into
        // WetnessFactor as a multiplier on warmth. Not solved: only two target
        // rows are wet and both wear nothing, so the table cannot see it.
        static inline float fWetWarmthLoss{ 0.60f };

        // In-game minutes to soak through, and to dry out again. Drying only
        // runs by a fire or under cover, so the two are not symmetrical in
        // practice however close the numbers are.
        static inline float fSoakMinutes{ 10.0f };
        static inline float fDryMinutes{ 15.0f };

        // Outdoors where the baked map has nothing to say: a worldspace of
        // its own - Blackreach, Sovngarde, the Soul Cairn, another mod's island
        // - or a spot with no terrain record under it. Not a failure case, and
        // not a region either: the map covers every place the player reaches
        // under Skyrim's or Solstheim's own sky.
        static inline float fTempUnknown{ -1.0f };

        // The sky. Snow is the one the table pinned (its blizzard rows); rain
        // and cloud keep the 8:5:2 shape the first model had, at the solved
        // scale.
        static inline float fWeatherSnow{ -4.2f };
        static inline float fWeatherRain{ -2.6f };
        static inline float fWeatherCloudy{ -1.1f };

        // The hour, as a curve rather than a switch: zero at the warmest hour
        // and the whole of fDayAmplitude at the coldest, which is the hour
        // before the region's own sunrise. The region temperatures are the
        // warmest-hour ones, so this only ever subtracts - and the minus lives
        // in the number, the way fWeatherRain and fWeatherCloudy already do,
        // instead of being applied by the code that reads it.
        static inline float fDayAmplitude{ -3.2f };
        static inline float fDayPeakHour{ 15.0f };

        // The tent. It adds no warmth - awake or asleep, in the cold or out of
        // it; making heat is the fire's job. What a tent does is KEEP heat,
        // and it does that twice over while you sleep in your own:
        //
        //   - the cold falls at fTentColdSlow of its usual speed;
        //   - and it cannot fall past fTentColdFloor at all, and is lifted to
        //     that if the night started worse.
        //
        // Both are gated on the perks and on actually sleeping - a tent cannot
        // be walked into, so sleep is the only way to be under one. The floor
        // is v0.4.0's SleptColdCap, which we had cut as a patch over a missing
        // equilibrium; the equilibrium exists now and this sits on top of it
        // deliberately, as the defining property of a tent.
        static inline float fTentColdFloor{ 0.15f };
        static inline float fTentColdSlow{ 0.50f };

        // Insulation. Four clothing slots at fWarmthPerSlot, plus frost
        // resistance weighted in as an equal partner. v0.4.0 had 7 and 50%;
        // both are solved values now, and armour is still never rated
        // individually - that would mean writing warmth into RFAB's records.
        static inline float fWarmthPerSlot{ 14.67f };
        static inline float fFrostResistWeight{ 0.818f };
        static inline float fWarmthRelief{ 0.010f };

        // Indoors. v0.4.0 expressed the same two rooms as percentages of the
        // hold's outdoor severity - SevInterior 60 and SevColdInterior 45 - and
        // the second is why the ice-cave list exists: "interior" does not save
        // you in Alftand or Yngvild. The warm one is solved; the cold one is
        // set a little under Snow, which is what an interior cut into a glacier
        // should be.
        static inline float fInteriorTemp{ 6.0f };
        static inline float fColdInteriorTemp{ -7.0f };

        // Heat sources. v0.4.0 searched a radius of 400 units and multiplied
        // the outdoor severity by 0.2 when it found one; here a fire raises the
        // temperature instead, which is the same relief expressed in the units
        // this model actually uses.
        //
        // A SHARE of what the cold is short by, not a flat number of degrees.
        // The reason is in the target table: all eight interior rows sit at the
        // same temperature, so they pin what a fire is WORTH (0.15 of the bar)
        // while saying nothing about its shape - and the one outdoor fire row,
        // four slots and 50% resist in the mountains, asks for more than the
        // flat number can give (0.47 against a required 0.50+). A share
        // satisfies both. It also removes the oddity that a flat fire threw
        // away whatever part of itself pushed past comfort.
        //
        // fFireShare is solved rather than chosen: 0.15 of a bar, six degrees
        // short of comfort, is 0.4583 of the shortfall. The cap is the one
        // number here the target table does not fix, and it carries the whole
        // of how much of a rescue a fire is.
        //
        // EVERY FIRE IS THE SAME FIRE. A flat +10 used to sit on top of one the
        // player lit themselves, and it put the model outside its own
        // specification: row 18 asks for 0.50+ in the mountains at a fire, the
        // share alone returns 0.599, and the bonus took that row to 1.000. A
        // fire carried up a mountain is worth having because it could be
        // carried there, not because it burns hotter than anyone else's.
        //
        // The cap reaches no interior row: at +6 the shortfall is six degrees
        // and the share gives 2.75, nowhere near any cap under eleven. It only
        // ever decides the cold outdoors, which is what makes it safe to turn.
        // At 7 a fire in ordinary snow settles on the penalty line and the
        // mountains become survivable rather than solved.
        static inline float fFireShare{ 0.4583f };
        static inline float fFireMaxDeg{ 7.0f };

        // A torch is this much of a fire, cap and all. In a warm interior that
        // comes out at the 1.2 degrees it was before.
        static inline float fTorchOfFire{ 0.44f };

        // Half of v0.4.0's 400 for the radius. That number was reached through
        // a different model - severity was multiplied by 0.2 anywhere inside it
        // - and at 400 here a fire reaches further than it looks like it should.
        static inline float fFireRadius{ 200.0f };

        // The ice crust. v0.4.0's generator defaults are on, at 90 points of
        // deprivation - a reserve of 0.10. The Papyrus fallback in that build
        // said 50, but the generator is where the balance actually lives.
        // How far off level a placed object may be laid. Campfire used 25 and
        // it showed: on a real hillside the fire sat at 25 degrees against a
        // 40-degree slope and read as wrong. Past this the surface is something
        // to lean against rather than lie on, and the object goes down flatter
        // than the ground.
        // Campfire's own limit, and for the same reason: past this an object
        // is standing on its edge rather than lying on the ground.
        //
        // It was briefly 45 for the bedroll and 0 for the fire, which was a
        // workaround for the tilt being computed in the player's frame instead
        // of in world axes - the fire looked wrong at any angle because the
        // angle was pointing the wrong way, and levelling it hid that.
        static inline float fMaxPlacementTilt{ 25.0f };

        // The camp: what it costs and how long it lasts. There is no switch
        // for the fire itself - see the note on chopping below.
        static inline float fCampfireBurnHours{ 4.0f };

        // With both survival perks the fire is banked properly and lasts the
        // night. This is the one thing Acclimatization does for the camp that
        // is not simply "the tent exists" - see docs/PERKS.md.
        static inline float fCampfireBurnHoursPerk{ 12.0f };
        // Six, because a tree gives two. RFAB's Survival Basics doubles
        // every harvest in the game, a tree is now just another harvest, and
        // the cost of a fire is set against what three trees yield rather than
        // against v0.4.0's number from before trees gave anything.
        static inline float fCampfireFuel{ 6.0f };
        static inline float fCampfireCooldown{ 5.0f };   // real seconds between casts

        // Firewood off trees. Like the fire, this has no switch any more.
        //
        // Both had one, both were taken out of the menu as things that are the
        // mod rather than choices within it - and that is exactly when the trap
        // sprang: a "0" already written to MCM\Settings\RFAB_SurvivalLayer.ini
        // outlives the row that wrote it, and the player is left with a feature
        // he cannot switch back on. A setting nobody can reach must not be able
        // to turn anything off.
        static inline float fTreeChopCooldownHours{ 12.0f };

        // The spit and the pot sit lower than the fire they belong to. Both
        // by the same amount, because they are aligned to each other already -
        // this lifts the pair. Measured off the mesh in game, like the pot's
        // own offsets, which is why it is a setting and not a constant.
        static inline float fCookGearZ{ 13.0f };

        // Warming your hands at a fire: cosmetic, and off is a fair choice.
        static inline bool  bWarmAnim{ true };
        static inline float fWarmAnimDelay{ 5.0f };   // seconds still first

        // How close the fire has to be for the idle, separately from how far it
        // warms. v0.4.0 shared fFireRadius for both, but it also refused to
        // count a torch for the idle while counting it for warmth, and the port
        // lost that distinction - which is most of why the idle felt like it
        // reached too far. Its own number, so the reach can be settled without
        // moving the cold model.
        static inline float fWarmAnimRadius{ 200.0f };

        // The cold takes the way out away: no Teleport and no Mark and Recall
        // while hypothermia is on or the cold axis is at or under its safe
        // mark. Optional, because it is a real restriction on how the pack is
        // played rather than a fix to anything.
        static inline bool  bColdBlocksTeleport{ true };

        static inline bool  bColdShaderEnabled{ true };
        static inline float fColdShaderAt{ 0.10f };

        // Being soaked, shown on the character. Wet clothing costs up to 60% of
        // its warmth and the player was never told; this is where they find out.

        // The screen while freezing: the picture goes soft and dark at the
        // edges and fades out, coming in over the last third of the bar.
        //
        // Seventeen candidates and a camera shake were put on a debug page and
        // looked at one at a time; these two are what survived, and the window
        // is what looked right with them. Kept as constants in Core/ColdScreen
        // rather than as settings - the choice is made, and a slider on it
        // would only invite it to be unmade.
        static inline bool  bColdScreenEnabled{ true };

        // How much of an elemental hit reaches the cold bar.
        //
        // The hit is measured as the share of the player's maximum health it
        // actually took - so a stronger spell moves the bar further, and a
        // spell a ward swallowed moves it not at all. This is the one number
        // that says how much of that share lands: at 0.5 a hit worth a tenth of
        // the player's health is worth a twentieth of the bar. Frost takes it
        // off, fire adds it, and burning yourself warm stays a real trade
        // because it still costs the health it is measured by.
        //
        // It replaces v0.4.0's FrostHitCold and FireHitWarm, which were flat:
        // the same two points of bar for a candle and for a dragon.
        static inline float fElemDamageShare{ 0.5f };

        // --- diseases ---
        // Thresholds are reserves: v0.4.0's 50 and 90 points of deprivation.
        static inline bool  bDiseasesEnabled{ true };
        static inline float fColdCatchAt{ 0.50f };
        static inline float fColdCatchWorstAt{ 0.10f };
        static inline float fColdCatchChanceMin{ 10.0f };
        static inline float fColdCatchChanceMax{ 90.0f };
        // What one dose of medicine is worth to an illness past stage 1, in P.
        // It no longer cures at that point, but it should not be thrown away
        // either. Ten is what a clean linen cloth gives a lesion, so a dose
        // means the same thing wherever it lands. Zero makes medicine useless
        // past stage 1, which is a fair setting to want.
        static inline float fCurePotency{ 10.0f };
        static inline float fDiseaseProgressHours{ 24.0f };
        static inline float fDiseaseDecayHours{ 24.0f };

        // Per melee hit from a carrier, before disease resistance.
        static inline float fDiseaseHitChance{ 100.0f };

        // A hit taken on the block does not infect.
        //
        // RFAB's own rule as of [ver. 15.09.2026], and this is our side of it:
        // their diseases go through their SKSE plugin and ours through
        // Events.cpp, so nothing is shared and the two have to agree by hand.
        // TESHitEvent carries kHitBlocked, which is the same HitData flag their
        // ApplyCustomCombatHitSpells reads, so both are answering the same
        // question about the same hit.
        //
        // Default on, because "a shield stops the bite" is what the pack now
        // says everywhere and a draugr being the one exception reads as a bug.
        static inline bool bBlockStopsDisease{ true };

        // Per raw item eaten on a weak stomach. Flat - disease resistance does
        // not help, only a strong-stomach race or being undead does.
        static inline float fFoodPoisonChance{ 50.0f };

        // Raw meat on a weak stomach doubles the player over. Cosmetic, and it
        // uses the engine's own stagger rather than an animation of ours, so
        // there is nothing to install and nothing to go wrong on a load.
        static inline bool  bRawFoodStagger{ true };
        static inline float fRawFoodStaggerForce{ 0.5f };

        // Elemental lesions - frostbite and burns. Their P runs the other way
        // round from an ordinary illness: hits and deep cold push it down, and
        // it is crossing the SAME threshold that both catches it and moves a
        // stage, so ContractP does two jobs on purpose.
        static inline bool  bRfabDiseasesEnabled{ true };

        // The sick sound. Every illness here announces itself once and is then
        // silent for hours; this is the one thing that keeps saying so, in the
        // character's own voice instead of in text.
        static inline bool  bCoughEnabled{ true };

        // Scales the whole schedule at once - 1, 2 and 4 coughs a game hour by
        // stage. One knob rather than three, because what anyone ever wants to
        // say is "more of this" or "less".
        static inline float fCoughRateMult{ 1.0f };
        static inline float fCoughVolume{ 1.0f };
        static inline bool  bElemLesionEnabled{ true };
        static inline float fElemLesionColdAt{ 0.10f };      // reserve, = v0.4.0's 90
        static inline float fElemLesionHypoChance{ 50.0f };  // %/hour at hypothermia 2+
        static inline float fElemLesionHitP{ -4.0f };        // P per hit, pre-resist
        static inline float fElemLesionContractP{ 70.0f };

        // --- hypothermia ---
        // v0.4.0's numbers, as reserves - the same end of the axis every other
        // threshold in this file is measured from, and the end the widget draws.
        // It sets in at 90 points of deprivation, a reserve of 0.10, and starts
        // to lift at 25 points, a reserve of 0.75. They were stored the other
        // way round at first and flipped at the point of use, which made the
        // MCM show two of these sliders running backwards against the rest.
        static inline bool  bHypothermiaEnabled{ true };

        // Hypothermia blocks rest from stage 1, which v0.4.0 does through
        // Game.SetInChargen - the same lever the engine itself uses. That call
        // is the only Papyrus this mod makes, and it is the one thing in the
        // stage-one path that cannot be checked by reading a record, so it gets
        // its own switch: turning it off leaves hypothermia otherwise intact.
        static inline bool  bHypoBlocksRest{ true };
        static inline float fHypoThreshold{ 0.10f };
        static inline float fHypoRecoverThreshold{ 0.75f };
        // Game hours below the threshold per stage, BEFORE warmth is taken
        // into account: the figure is divided by the same (1 + k x dry warmth)
        // that slows the cold axis, so this is the naked case. v0.4.0 had 1.0
        // flat; halving it keeps a bundled-up character near the old pace while
        // making the bottom of the bar in rags as sharp as it should be.
        static inline float fHypoWorsenHours{ 0.25f };
        static inline float fHypoRecoverHours{ 1.0f };
        static inline float fHypoDrainPerSec{ 1.0f };
        static inline float fHypoDrainRamp{ 30.0f };

        // How far the cold axis moves towards its target in one game hour.
        //
        // Calibrated against the note in the generator, which is where v0.4.0
        // records what the axis is supposed to feel like: naked with no
        // resistance, a cold region on a clear day empties the bar in about
        // fifteen real minutes, and a blizzard halves that. At TimeScale 10
        // that worst case is roughly 1.2 game hours to fall from full to a
        // tenth, which needs a rate near 2. The first value here was 0.45,
        // better than four times too slow, and the bar barely moved.
        //
        // Warming and chilling at the same rate, and the two knobs kept apart
        // only so the ratio can be argued with later.
        //
        // v0.4.0's WarmupMult was ten to one, and this carried it over on the
        // grounds that it "survives the change of model". It does not. That
        // multiplier existed because the old model integrated in one direction
        // with no target: recovery was the same crawl as freezing unless it was
        // given its own rate. Here the axis approaches a target exponentially,
        // and a fire moves the target a long way at once - so the relief comes
        // from the size of the gap, which closes fastest at the start, and not
        // from the rate at all. The ten was doing the job twice.
        //
        // Bar per game hour PER UNIT OF LOAD - the speed is set by how cold the
        // situation is, not by how much bar is left. See Climate::ColdSpeed.
        //
        // 0.40 is v0.4.0's pace, converted. Its ColdRate was 1.0 against a
        // severity scale of 0..100 and its own comment records what that was
        // tuned to: "naked, 0 resist, cold region, day and clear: 0 to 100 in
        // 15 real minutes at TimeScale 10". That is 40 points of a 100 point
        // bar per game hour, against a severity of 40. The same situation here
        // carries a load of 1.04, so the same 2.5 game hours of bar means 0.4
        // per hour per unit of load - and the fifteen minutes come out again.
        //
        // Warming is ten times that, which is v0.4.0's WarmupMult exactly.
        // TUNED IN GAME, not derived. The v0.4.0 arithmetic above is where
        // these started and is kept because it explains the shape of the model,
        // but the numbers themselves are the owner's, set against how the axis
        // actually feels at RFAB's TimeScale.
        static inline float fColdChillRate{ 3.00f };
        static inline float fColdWarmRate{ 10.00f };

        // How the two ends of the journey are shaped, in bar.
        //
        // Freezing runs straight while the target is far and eases in as it
        // arrives: at this much gap left it is at half speed. Warming does the
        // opposite - a head start that fades - at this much gap it runs at
        // double. Neither changes where the bar settles, only how it gets
        // there; set both very small for a straight line with a hard stop.
        static inline float fColdChillEase{ 0.10f };

        // How much warmth slows the fall as well as raising the floor.
        // The chilling speed is divided by (1 + this x dry warmth), so 0
        // restores the old behaviour exactly: everyone freezes at the same
        // speed and a coat only changes where they stop.
        //
        // At 0.01, four slots of gear with 25 frost resist come to about 79
        // points of warmth and a divisor near 1.8 - freezing takes roughly
        // twice as long as bare skin. It sits on the Debug page because it
        // is the one number in the model with no field measurement behind
        // it yet.
        static inline float fWarmthSlowsChill{ 0.01f };

        // How long the white loss marker trails the bar. 1 is the shipped
        // speed; 2 makes it last twice as long. It is the only readout of how
        // FAST an axis is falling, and at 1 it was gone before the eye found it.
        static inline float fMarkerLinger{ 2.0f };

        // THE AIR ABOVE THE GROUND, for the baked climate map.
        //
        // Near the ground the temperature is the ground's - that is what a
        // surface layer is in the real thing, and it is why the floor of the
        // College of Winterhold is as warm as the town below rather than three
        // thousand units colder. fSkyBuffer is how deep that layer is; above
        // it the air takes over and the temperature crosses to fSkyTemp over
        // fSkySpan, so a Dovahkiin lifted into the sky is in the sky wherever
        // he was standing.
        static inline float fSkyBuffer{ 3500.0f };
        static inline float fSkySpan{ 15000.0f };
        static inline float fSkyTemp{ -60.0f };

        // What one point of an item's frost resistance buys, in bar. A potion
        // of 25 gives a quarter of the bar at 0.01 - enough to cross a pass,
        // not enough to live up there.
        static inline float fColdPerResistPoint{ 0.01f };

    private:
        static void ReadFile(std::string_view a_path);

        static void ReadBool(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool& a_out);
        static void ReadFloat(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, float& a_out);
        static void ReadUInt(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t& a_out);
    };
}
