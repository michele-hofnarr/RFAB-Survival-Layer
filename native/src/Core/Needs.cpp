#include "PCH.h"

#include "Core/Needs.h"

#include "Core/Climate.h"
#include "Core/Trees.h"
#include "Core/Disease.h"
#include "Core/Elemental.h"
#include "Core/Bedroll.h"
#include "Core/Campfire.h"
#include "Core/Forms.h"
#include "Core/ColdVisual.h"
#include "Core/Hypothermia.h"
#include "Core/Notify.h"
#include "Core/StagedDisease.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr std::uint32_t SERIALIZATION_ID = 'RSLC';
        constexpr std::uint32_t RECORD_NEEDS = 'NEED';
        constexpr std::uint32_t RECORD_DISEASE = 'DISE';
        constexpr std::uint32_t RECORD_CAMP = 'CAMP';
        constexpr std::uint32_t RECORD_TREES = 'TREE';
        // 2: the camp record carries every bedroll, not one.
        // 3: hunger became two halves, so the needs record is a different
        //    shape. Only that record changed, which is why a version 2 co-save
        //    is still read rather than thrown away whole - losing a player's
        //    camp, illnesses and felled trees to a change in one field would be
        //    a poor trade.
        // 4: the cold axis grew a second half too - the bought-time buffer.
        // 5: and so did sleep - the half earned in a bedroll.
        constexpr std::uint32_t RECORD_VERSION = 5;
        constexpr std::uint32_t RECORD_VERSION_MIN = 2;

        // The needs record as older versions wrote it. Kept verbatim: reading
        // an old record means reading the OLD shape, and inferring that shape
        // from the current struct is how a migration silently reads garbage.
        struct StateV2
        {
            float sleep{ 1.0f };
            float hunger{ 1.0f };
            float cold{ 1.0f };
            float wetness{ 0.0f };
            float lastGameDays{ -1.0f };
        };

        struct StateV3
        {
            float sleep{ 1.0f };
            float hungerSpecial{ 1.0f };
            float hungerFast{ 0.0f };
            float cold{ 1.0f };
            float wetness{ 0.0f };
            float lastGameDays{ -1.0f };
        };

        struct StateV4
        {
            float sleep{ 1.0f };
            float hungerSpecial{ 1.0f };
            float hungerFast{ 0.0f };
            float cold{ 1.0f };
            float coldTemp{ 0.0f };
            float wetness{ 0.0f };
            float lastGameDays{ -1.0f };
        };

        constexpr float HOURS_PER_DAY = 24.0f;

        // Long spans are replayed in steps rather than applied in one go. Cold
        // approaches its target exponentially, so a single large step and many
        // small ones do not agree once the target moves - and the player was
        // somewhere the whole time.
        constexpr float REPLAY_STEP = 1.0f;

        // A ceiling on how much of a single jump is charged. Only reachable by
        // waiting or fast travelling for days on end; it exists so a broken
        // clock cannot empty every axis in one frame.
        // No cap on the span. v0.4.0 charges the whole delta and only clamps a
        // negative one; a ceiling here silently threw away everything past it,
        // so a long wait or a jailed-for-a-month script cost nothing.

        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        // One bar, two halves, spent FROM THE TOP DOWN.
        //
        // The upper half is the part that does not last - fast food, a night in
        // a bedroll - and it goes first. While it holds, the earned half does
        // not move at all, which is the whole point of having two: what you
        // scraped together buys time for what you worked for. The cold buffer
        // next door works the same way.
        //
        // This replaces draining both at once. That older rule needed no
        // ordering because the fast half simply burned out sooner, but it also
        // meant an apple could not protect a meal - both fell together, and the
        // apple only made the total fall faster.
        //
        // The arithmetic is in HOURS, not in bar, so the crossover inside one
        // step is exact: when the upper half runs out mid-step, the hours it
        // could not cover are charged to the lower one at the lower one's rate.
        // Doing it in bar would either lose those hours or charge them twice.
        void SpendHalves(float& a_top, float& a_base, float a_hours,
            float a_hoursToEmpty, float a_topMult, float a_baseMult)
        {
            const float perHour = 1.0f / std::max(1.0f, a_hoursToEmpty);
            float       left = a_hours;

            if (a_top > 0.0f && a_topMult > 0.0f) {
                const float rate = perHour * a_topMult;
                const float spent = std::min(left, a_top / rate);
                a_top = std::max(0.0f, a_top - spent * rate);
                left -= spent;
            }
            if (left > 0.0f) {
                a_base = std::clamp(a_base - left * perHour * a_baseMult, 0.0f, 1.0f);
            }
        }

        [[nodiscard]] bool PlayerInCombat()
        {
            const auto* player = Player();
            return player && player->IsInCombat();
        }

        // RFAB drives vampire feeding itself through Req_FeedPenaltyScript, so
        // this mod stays out of the way: no rest and no hunger for the undead.
        [[nodiscard]] bool PlayerIsUndead()
        {
            auto* player = Player();
            return player && player->HasKeywordString("ActorTypeUndead"sv);
        }
    }

    Needs& Needs::GetSingleton()
    {
        static Needs singleton;
        return singleton;
    }

    void Needs::Reset()
    {
        _state = State{};
        _sleptHoursPending = 0.0f;
        _bedKnown = false;
        _bedIsBedroll = false;
        _sleeping = false;
        _wetSeen = -1.0f;
    }

    void Needs::SetSleeping(bool a_sleeping)
    {
        _sleeping = a_sleeping;
    }

    void Needs::NoteBed(RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return;
        }

        // Only furniture you can lie on. Chairs, benches, crafting stations and
        // every other activatable thing in the world go past without touching
        // the note - otherwise walking up to an anvil on the way to bed would
        // erase the answer.
        auto* furniture = a_ref->GetBaseObject()
                              ? a_ref->GetBaseObject()->As<RE::TESFurniture>()
                              : nullptr;
        if (!furniture ||
            !furniture->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSleep)) {
            return;
        }

        // Asked of the REFERENCE, not the base form: TESObjectREFR::HasKeyword
        // walks through to the base object anyway, and asking the reference is
        // what makes our own placed bedrolls answer as well.
        _bedIsBedroll = Forms::kwBedRoll && a_ref->HasKeyword(Forms::kwBedRoll);
        _bedKnown = true;

        logger::info("about to sleep on {} [{:08X}] - {}",
            furniture->GetFormEditorID() && *furniture->GetFormEditorID()
                ? furniture->GetFormEditorID()
                : "<no editor id>",
            furniture->GetFormID(), _bedIsBedroll ? "a bedroll" : "a real bed");
    }

    void Needs::Update()
    {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) {
            return;
        }

        _hoursThisTick = 0.0f;

        const float now = calendar->GetCurrentGameTime();

        // First reading after a load or a new game: take the clock, integrate
        // nothing. Otherwise the gap between saving and loading would be
        // charged to the player.
        if (_state.lastGameDays < 0.0f) {
            _state.lastGameDays = now;
            _sleptHoursPending = 0.0f;
            return;
        }

        // Asleep: the clock races ahead through the fade, and none of it is
        // this tick's business. The whole night is charged once, by NoteSlept,
        // with the sleep axis exempted - so the reference point is moved past
        // these hours and nothing is integrated. Without this the tick charged
        // the night at the WAKING rate and NoteSlept then charged it again.
        if (_sleeping) {
            _state.lastGameDays = now;
            return;
        }

        const float hours = (now - _state.lastGameDays) * HOURS_PER_DAY;
        _state.lastGameDays = now;

        // Time can run backwards across a load of an older save, so only a
        // forward delta is integrated - but the slept hours below are folded
        // in either way.
        if (hours > 0.0f) {
            _hoursThisTick = hours;

            float remaining = _hoursThisTick;
            while (remaining > 0.0f) {
                const float step = std::min(REPLAY_STEP, remaining);
                remaining -= step;
                Advance(step, true);
            }
        }

        // The axes were charged for the hours just slept at the event itself,
        // and the tick skipped the whole span while _sleeping was set, so this
        // tick's own delta cannot contain them. The illnesses still have to
        // advance over them - v0.4.0 calls every Advance*Dz explicitly for the
        // same reason - so they are handed on here, once.
        _hoursThisTick += std::exchange(_sleptHoursPending, 0.0f);
    }

    void Needs::Advance(float a_hours, bool a_chargeSleep, float a_coldRateMult)
    {
        // TimeScale, on its edges. Every rate in this mod is per GAME hour -
        // v0.4.0's were too, and its tuning note says "TimeScale=10" - so this
        // one number decides what all of them mean in real minutes. A game set
        // to 1 stretches the whole model twentyfold and reads, from the inside,
        // exactly like a model that does not work. It is worth a line whenever
        // it moves, and something does move it: the console writes it into the
        // save, and mods set it on a new game.
        if (Forms::globTimeScale) {
            static float lastScale = -1.0f;
            const float  scale = Forms::globTimeScale->value;
            if (scale != lastScale) {
                logger::info("timescale: {:.2f} -> {:.2f} (vanilla 20; every rate "
                             "here is per GAME hour)",
                    lastScale < 0.0f ? scale : lastScale, scale);
                lastScale = scale;
            }
        }

        if (const bool undead = PlayerIsUndead(); undead != _undead) {
            logger::info("undead: {} -> {}", _undead, undead);
            _undead = undead;
        }

        // Sleep pauses only the sleep axis. Hunger and cold run through the
        // night, so the slept span comes through here with a_chargeSleep off
        // rather than being cancelled afterwards.
        //
        // What used to stand here was a credit of "hours already paid for by
        // sleeping", drawn down against the clock delta. Two accumulators that
        // must cancel exactly, with nothing enforcing it: any surplus silently
        // ate the drain, and a surplus is what happens the moment the player
        // sleeps while already rested, since the credit was banked whether or
        // not the sleep restored anything. It pinned the axis at 1.000 for
        // minutes on end while hunger fell normally beside it.
        if (a_chargeSleep && !_undead) {
            // Combat is ADDED to the bedroll's own multiplier, not multiplied
            // by it: a proper night's rest goes at the combat rate, a nap at
            // the combat rate plus its own. Same rule as fast food, and the
            // same reason - 5 x 5 is a number nobody asked for.
            const float combat = PlayerInCombat() ? Settings::fCombatDrainMult : 0.0f;
            SpendHalves(_state.sleepTemp, _state.sleep, a_hours,
                Settings::fSleepHoursToEmpty,
                Settings::fBedrollSleepDrainMult + combat,
                combat > 0.0f ? combat : 1.0f);
        }

        // Hunger runs the whole time, asleep or not.
        if (!_undead) {
            // Gutworm makes you burn through food faster: 1.3, 1.7, then 2.5
            // times. Controller-side in v0.4.0 as well - there is no actor
            // value that says "this illness is eating your dinner". It scales
            // the WHOLE drain, so it goes on both halves.
            const float gutworm = StagedDisease::GutwormHungerMult();

            // Combat is ADDED, not multiplied: a proper meal goes at the combat
            // rate, an apple at the combat rate plus its own.
            const float combat = PlayerInCombat() ? Settings::fCombatDrainMult : 0.0f;

            SpendHalves(_state.hungerFast, _state.hungerSpecial, a_hours,
                Settings::fHungerHoursToEmpty,
                (Settings::fFastFoodDrainMult + combat) * gutworm,
                (combat > 0.0f ? combat : 1.0f) * gutworm);
        }

        // Wetness first, because the cold model reads it.
        //
        // Soaking runs whenever rain is falling on you or you are in water.
        // Drying runs the rest of the time - anywhere, fire or no fire, roof or
        // no roof. It used to need one of those two, which meant standing in a
        // dry field in clear weather kept you soaked indefinitely; nothing
        // about a field does that.
        {
            const Climate probe = Climate::Sample();
            const float   minutes = a_hours * 60.0f;
            if (probe.soaking) {
                // fSoakMinutes is the RAIN's pace. Water is not a slower or a
                // faster rain, it is water: a minute in it and you are as wet as
                // you can get, whatever the rain is set to.
                const float soakMinutes = probe.inWater
                    ? 1.0f
                    : std::max(1.0f, Settings::fSoakMinutes);
                _state.wetness = std::min(1.0f, _state.wetness + minutes / soakMinutes);
            } else {
                _state.wetness = std::max(0.0f,
                    _state.wetness - minutes / std::max(1.0f, Settings::fDryMinutes));
            }

            // THE ONLY THING THE PLAYER EVER SEES OF THIS AXIS.
            //
            // Wetness has no bar and no icon, and it costs up to 60% of what
            // clothing is worth - so without these two lines a player can be
            // freezing twice as fast with nothing on screen to say why.
            //
            // Announced on the EDGES only, and only once each: soaked through,
            // and dry again. Nothing in between, because nothing in between is
            // a decision the player can act on.
            //
            // _wetSeen starts negative so the first pass after a load records
            // where the axis stands without announcing it. Otherwise loading a
            // save made in the rain would greet the player with "you are
            // soaked" every time.
            if (_wetSeen >= 0.0f) {
                if (_state.wetness >= 1.0f && _wetSeen < 1.0f) {
                    Notify::GetSingleton().Push(Forms::msgWetSoaked);
                    logger::info("wetness: soaked through");
                } else if (_state.wetness <= 0.0f && _wetSeen > 0.0f) {
                    Notify::GetSingleton().Push(Forms::msgWetDry);
                    logger::info("wetness: dry again");
                }
            }
            _wetSeen = _state.wetness;
        }

        // Cold moves towards the level this situation settles at, rather than
        // being integrated one way forever.
        Climate climate = Climate::Sample();
        climate.wetness = _state.wetness;
        _lastClimate = climate;

        // The target is not clamped and the step is. A situation the player
        // cannot survive has a target below zero, and how far below is what
        // makes it drain faster than one that merely hurts: the load is bigger,
        // so the speed is. Clamping before the step would flatten that.
        //
        // THE WHOLE BAR DECIDES the direction, not the reserve underneath
        // it. With a buffer running, the reserve can sit below a target the
        // player is nowhere near - and from the reserve alone ColdSpeed
        // would call that "warming" and start the head start, when what
        // should actually happen is the buffer melting.
        const float before = Cold();
        const float target = climate.ColdTarget();

        // A fight keeps the body hot, so heat leaves it that much more slowly -
        // the mirror of the combat multiplier on sleep and hunger, which burn
        // that much faster. Warming is untouched: v0.4.0 divides only the
        // losing delta (`ElseIf pl.GetCombatState() == 1`), and warming there
        // has its own multiplier.
        //
        // This was in v0.4.0 and was lost in the port. Settings.h even said so
        // in words - "the cold axis uses the inverse of this" - while no code
        // read it.
        const bool  losing = target < before;
        const float combat = (losing && PlayerInCombat())
                                 ? std::max(1.0f, Settings::fCombatDrainMult)
                                 : 1.0f;

        // Bar per game hour, then the hours. The step never crosses the target:
        // the axis settles there rather than oscillating around it.
        //
        // Only the losing side is scaled. A tent is shelter from the cold, not
        // a brake on a fire - the same reasoning that already makes the combat
        // multiplier one-sided, and the same reason warming ignores the load.
        const float rateMult = losing ? a_coldRateMult : 1.0f;
        const float speed = climate.ColdSpeed(before) * rateMult / combat;
        const float step = speed * a_hours;
        const float moved = losing ? std::max(before - step, target)
                                   : std::min(before + step, target);
        const float delta = std::clamp(moved, 0.0f, 1.0f) - before;

        // THE BUFFER BURNS AT THE SITUATION'S OWN CHILLING SPEED - the same
        // speed the reserve would be falling at, not a rate of its own - and it
        // burns REGARDLESS OF THE TARGET. That is the one way it differs from
        // the reserve: the reserve settles at the equilibrium and stops, bought
        // time keeps going. Sitting in a warm room does not bank it.
        //
        // The target is absent from this line on purpose, so the gap shaping is
        // absent with it: that factor exists to seat the axis softly on its
        // target, and the buffer is not heading for one.
        //
        // Everything that is NOT the target is carried over, though, and that
        // means the tent multiplier and the combat brake as well - the same two
        // the reserve's own speed takes. Leaving them off made the sentence
        // above false where it matters most: a night in a tent halves the
        // reserve's fall and used to leave bought time burning at full rate,
        // and a fight slowed the reserve fivefold while the buffer ran on.
        const float burn = std::abs(climate.ColdLoad()) * Settings::fColdChillRate /
                           climate.ChillSlow() * a_coldRateMult / combat * a_hours;

        // ONE DRAIN, NOT TWO. What the buffer burns is also what it pays
        // towards the fall - the burn IS the absorption, seen from the other
        // side - so the reserve only moves for what the buffer could not cover.
        // Charging the fall to the buffer and then burning it again on top
        // would spend bought time at twice the speed the bar was falling.
        const float paid = std::min(_state.coldTemp, burn);
        _state.coldTemp -= paid;

        if (delta < 0.0f) {
            // FALLING: while the buffer covers it the reserve does not move at
            // all. That is the whole of what a draught buys - not a cure, a
            // delay - and it is why hypothermia neither worsens nor eases while
            // one is running.
            _state.cold = std::clamp(_state.cold + std::min(0.0f, delta + paid),
                0.0f, 1.0f);
        } else if (delta > 0.0f) {
            // WARMING: real heat goes into the reserve, and the buffer is
            // pushed out from underneath as it fills. A fire does not stack on
            // top of bought time; it replaces it.
            _state.cold = std::clamp(_state.cold + delta, 0.0f, 1.0f);
        }
        _state.coldTemp = std::min(_state.coldTemp, 1.0f - _state.cold);

        // Frost and fire hits are folded here rather than applied where they
        // land, so every change to the axis goes through one place. They land
        // on the reserve: a firebolt is heat, not borrowed time.
        //
        // Tick() first: it is what turns the burning-over-time effects into
        // queued damage, and it has to run before the queue is drained.
        Elemental::GetSingleton().Tick();
        if (const float nudge = Elemental::GetSingleton().TakeQueued(); nudge != 0.0f) {
            _state.cold = std::clamp(_state.cold + nudge, 0.0f, 1.0f);
            _state.coldTemp = std::min(_state.coldTemp, 1.0f - _state.cold);
        }

        // Tuning a temperature model without seeing its terms is guesswork, so
        // the whole sample goes to the log at a readable rate - and as terms,
        // not as a total. The constants here will be moved again; a single
        // "temp -9.0" cannot say which of the six numbers behind it is wrong,
        // and the block below is laid out to be read against a row of the
        // target table in docs/CLIMATE.md.
        if (Settings::bDebugLog) {
            static auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
            const auto  now = std::chrono::steady_clock::now();

            if (now - last >= std::chrono::seconds(5)) {
                last = now;

                if (climate.baked) {
                    logger::info("climate: {}, z={:.0f}, surface {:+.1f}, "
                                 "air {:.0f} above the ground "
                                 "(buffer {:.0f}, then {:.0f} more to {:.0f})",
                        climate.placeName, climate.worldZ, climate.surfaceTemp,
                        climate.airAbove, Settings::fSkyBuffer,
                        Settings::fSkySpan, Settings::fSkyTemp);
                } else {
                    // Not an error, but worth seeing: outdoors this means the
                    // map had nothing for the spot, which is either a
                    // worldspace of its own or a missing _RSL_Climate.bin.
                    logger::info("climate: {}, z={:.0f}, flat {:+.1f}",
                        climate.placeName, climate.worldZ, climate.baseTemp);
                }
                logger::info("  temp {:+.1f} = place {:+.1f}  sky {:+.1f} ({})  "
                             "hour {:+.1f} ({:02d}:{:02d})",
                    climate.temperature, climate.baseTemp, climate.weatherOffset,
                    climate.weatherBlend >= 1.0f
                        ? std::string(climate.weatherName)
                        : fmt::format("{}<-{} {:.0f}%", climate.weatherName,
                              climate.weatherFrom, climate.weatherBlend * 100.0f),
                    climate.dayOffset,
                    static_cast<int>(climate.hour),
                    static_cast<int>(climate.hour * 60.0f) % 60);
                logger::info("                fire {:+.1f}"
                             "  (a roof is worth no degrees)",
                    climate.fireOffset);

                const float dryWarmth = climate.DryWarmth();
                logger::info("  warmth {:.1f} = {} slots x {:.1f}  resist {:.0f}% x {:.3f}"
                             "   (dry {:.1f})",
                    climate.warmth, climate.slots, Settings::fWarmthPerSlot,
                    climate.resist, Settings::fFrostResistWeight, dryWarmth);

                const float chill =
                    std::max(0.0f, Settings::fComfortTemp - climate.temperature);
                logger::info("  load {:+.3f} = chill {:.1f}deg x {:.3f} (comfort {:.0f})"
                             "  + wet {:.2f} x {:.3f}  - warmth {:.1f} x {:.3f}",
                    climate.ColdLoad(), chill, Settings::fLoadPerDegree,
                    Settings::fComfortTemp, climate.wetness, Settings::fWetLoad,
                    dryWarmth, Settings::fWarmthRelief);

                // The unclamped target next to the axis it is pulling. The
                // clamped one is what the bar can show; the unclamped one is
                // what decides how fast it gets there, and without it a fast
                // fall looks like a bug.
                logger::info("  target {:+.3f} -> clamped {:.3f}   cold {:.3f} -> {:.3f}"
                             "  ({:.3f} bar/h{}, step {:.3f}h)  feel {}",
                    target, std::clamp(target, 0.0f, 1.0f), before, Cold(),
                    speed, combat > 1.0f ? ", in combat" : "", a_hours,
                    climate.Feel());

                // The two halves separately whenever there is a buffer: the
                // point of the mechanic is that the first number holds still
                // while the second drains, and a sum hides exactly that.
                if (_state.coldTemp > 0.0f) {
                    logger::info("    base {:.3f} + bought {:.3f}"
                                 "  (burning {:.3f} bar/h, target not involved)",
                        _state.cold, _state.coldTemp,
                        std::abs(climate.ColdLoad()) * Settings::fColdChillRate /
                            climate.ChillSlow());
                }

                // Why that speed and not the plain rate. Only printed while
                // freezing, because warming is not divided.
                if (target <= before) {
                    logger::info("  warmth slows the fall: /{:.2f} "
                                 "(1 + {:.3f} x dry {:.1f})",
                        climate.ChillSlow(), Settings::fWarmthSlowsChill, dryWarmth);
                }

                if (climate.nearFire) {
                    if (auto* fire = Climate::NearestFire(Settings::fFireRadius)) {
                        auto*       base = fire->GetBaseObject();
                        const char* edid = base ? base->GetFormEditorID() : nullptr;
                        logger::info("  fire: {} [{:08X}] at {:.0f}, worth {:+.1f} deg"
                                     " (share {:.2f}, cap {:.1f})",
                            (edid && *edid) ? edid : "<no editor id>",
                            base ? base->GetFormID() : 0,
                            fire->GetPosition().GetDistance(
                                RE::PlayerCharacter::GetSingleton()->GetPosition()),
                            climate.fireOffset, Settings::fFireShare,
                            Settings::fFireMaxDeg);
                    } else {
                        logger::info("  fire: a torch in hand, worth {:+.1f} deg"
                                     " ({:.2f} of a fire)",
                            climate.fireOffset, Settings::fTorchOfFire);
                    }
                }
                if (climate.sheltered || climate.soaking || climate.ownFire) {
                    logger::info("  cover: {}{}{}",
                        climate.sheltered ? "roof " : "",
                        climate.soaking ? "soaking " : "",
                        climate.ownFire ? "own fire" : "");
                }
                // The three axes as the widget draws them. v0.4.0 logged this
                // every tick alongside the cold terms; without it a report of
                // "it happened while I was hungry" cannot be checked against
                // what the mod actually saw.
                logger::info("needs: sleep {:.3f} ({:.3f}+{:.3f}) "
                             "hunger {:.3f} ({:.3f}+{:.3f}) "
                             "cold {:.3f} ({:.3f}+{:.3f}) wet {:.2f} | dt {:.3f}h{}",
                    Sleep(), _state.sleep, _state.sleepTemp,
                    Hunger(), _state.hungerSpecial, _state.hungerFast,
                    Cold(), _state.cold, _state.coldTemp, _state.wetness, a_hours,
                    _undead ? " | undead" : "");
            }
        }
    }

    void Needs::NoteSlept(float a_gameHours)
    {
        if (a_gameHours <= 0.0f || a_gameHours < Settings::fSleepMinHours) {
            logger::info("slept {:.1f}h - too short to count as rest", a_gameHours);
            return;
        }

        // The tent, asked once and used twice. It is the only thing in the
        // model that keeps warmth rather than making it, and it does that in
        // two ways: the night's cold falls at half speed, and it cannot fall
        // past fTentColdFloor at all.
        const bool tent = HasCampPerks() && Bedroll::GetSingleton().UnderOwnTent();

        // WHICH HALF THE NIGHT GOES INTO. That one answer decides everything
        // else about the night.
        //
        // An unknown bed counts as a real one. Sleep can begin without an
        // activation we saw (the console, another mod), and a bed is the
        // ordinary case: a bedroll is something the player pitched and walked
        // up to, so it is the one that always announces itself.
        auto*      player = Player();
        const bool knew = _bedKnown;
        const bool bedroll = _bedKnown && _bedIsBedroll;
        const bool cheerful = player && Forms::perkCheerfulness &&
                              player->HasPerk(Forms::perkCheerfulness);
        const bool toBase = !bedroll || cheerful;

        // A NIGHT ON THE GROUND IS A TRADE, NOT A REST.
        //
        // Sleeping rough does not stop the clock on tiredness - you lie there
        // and the night still costs you. So the sleep axis IS charged for these
        // hours, at the waking rate, and only then is the shallow rest credited
        // on top of it, into the half that does not keep. What comes out is an
        // exchange: the bar stays about as full as it was, but the good sleep
        // in it has turned into bad. Skip three days in a bedroll and you wake
        // with a full bar made entirely of the kind that burns five times over.
        //
        // A real bed does stop that clock: the axis is exempt, and the night is
        // pure gain. So is a bedroll with Cheerfulness - the perk is exactly
        // "you have learned to sleep well anywhere".
        //
        // Hunger and cold are charged here either way, because the tick was
        // told to skip these hours entirely (see Needs::Update). ONCE - it used
        // to be twice, tick and event both, and that is a different story.
        Advance(a_gameHours, !toBase, tent ? Settings::fTentColdSlow : 1.0f);

        // GOOD SLEEP DISPLACES BAD, and only in that direction.
        //
        // A real night grows into the WHOLE bar and clips the nap out from
        // under it. Note what that means with these numbers: the bar holds at
        // most ONE bar of bad sleep however long the player dozed, and a full
        // night is worth a full bar (fSleepHoursToFull), so one proper night
        // always clears the lot. Cap it on the sum instead and a full bar of
        // bad sleep can never be mended:
        // there is no room to credit, the night is worth nothing, and the
        // player is stuck with rubbish rest for ever.
        //
        // The other direction is NOT displacement - bad sleep cannot push good
        // sleep out. It takes it the slow way, through the night's own charge
        // above, which is what makes sleeping rough a trade rather than a
        // swap.
        //
        // Note this is deliberately NOT what food does: eating a meal does not
        // displace fast food, and that is intended there. Sleep is not food -
        // one good night IS meant to undo the bad ones.
        //
        // Brown rot takes its cut off the top: rotting flesh sleeps badly, so
        // the night is worth 0.9 / 0.8 / 0.7 of itself by stage. v0.4.0's
        // BrSleepMult, on the same line of the same function - it was the twin
        // of gutworm's hunger multiplier, and unlike that one it never made it
        // into the port.
        const float rot = StagedDisease::BrownRotSleepMult();
        const float room =
            toBase ? std::max(0.0f, 1.0f - _state.sleep) : std::max(0.0f, 1.0f - Sleep());
        const float restored = std::min(
            a_gameHours * rot / std::max(1.0f, Settings::fSleepHoursToFull), room);
        if (toBase) {
            _state.sleep += restored;
            _state.sleepTemp = std::min(_state.sleepTemp, 1.0f - _state.sleep);
        } else {
            _state.sleepTemp += restored;
        }
        _bedKnown = false;

        // And the floor. A hard one, not a target: asleep in your own tent
        // the cold axis does not go below fTentColdFloor, and comes UP to it if
        // the night started worse. It is the one promise the tent makes - a
        // blizzard in the mountains cannot kill you in your sleep - and it is
        // why the Acclimatisation perk is worth taking.
        //
        // v0.4.0 had SleptColdCap and we cut it as a patch over a missing
        // equilibrium. This is not that: the equilibrium is there now and this
        // sits on top of it deliberately, gated on the perks (there is no tent
        // without them) and on actually sleeping under the thing - you cannot
        // walk into a tent, so sleep is the only way to be under one.
        if (tent && _state.cold < Settings::fTentColdFloor) {
            logger::info("tent: cold {:.3f} -> {:.3f} (the tent's floor)",
                _state.cold, Settings::fTentColdFloor);
            _state.cold = Settings::fTentColdFloor;
            // Real warmth, so it displaces bought time like any other.
            _state.coldTemp = std::min(_state.coldTemp, 1.0f - _state.cold);
        }

        // Move the reference point by hand so the slept span cannot enter the
        // next tick's delta, as a charge or as a double count. This is what
        // v0.4.0 does (K_LASTTIME at OnSleepStop) and it needs no second
        // accumulator kept in step with the clock.
        //
        // The span comes from the event, not the clock, so how far the clock
        // has already moved by now is worth seeing rather than assuming:
        // "pending" is what the next tick would otherwise have charged, and it
        // should come out close to the slept span.
        float pending = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            const float now = calendar->GetCurrentGameTime();
            if (_state.lastGameDays >= 0.0f) {
                pending = (now - _state.lastGameDays) * HOURS_PER_DAY;
            }
            _state.lastGameDays = now;
        }

        _sleptHoursPending += a_gameHours;

        logger::info("slept {:.1f}h on {} -> {} +{:.2f} into {}{} | sleep {:.2f} "
                     "({:.2f}+{:.2f}) | clock had moved {:.2f}h",
            a_gameHours,
            !knew ? "an unknown bed" : (bedroll ? "a bedroll" : "a bed"),
            toBase ? "rested," : "charged for the night,",
            restored, toBase ? "base" : "temp",
            rot < 1.0f ? fmt::format(" (brown rot x{:.1f})", rot) : "",
            Sleep(), _state.sleep, _state.sleepTemp, pending);
    }

    Needs::Meal Needs::Assess(RE::AlchemyItem* a_item)
    {
        Meal meal;
        if (!a_item || !a_item->IsFood()) {
            return meal;
        }

        // Drinks quench, they do not feed - and what says "drink" is the sound
        // it makes going down.
        //
        // Not a keyword and not the editor id. A keyword misses more than half
        // of them: only 19 of RFAB's 34 RFAB_Drink_* carry RFAB_SpecialDrink,
        // and wine, mead, ale and water otherwise carry the same VendorItemFood
        // an apple does. The editor id would separate all 34, and that is what
        // v0.4.0 used - through po3's PAPYRUS function. The native
        // GetFormEditorID is not the same thing: it comes back empty for most
        // records, which Climate.cpp already found out the hard way and this
        // check then repeated. That is why a bottle of water fed a quarter of a
        // bar.
        //
        // ITMPotionUse is on all 33 food-flagged RFAB_Drink_*, on none of
        // RFAB's 78 other foods, and on vanilla's drinks as well; RFAB's foods
        // use ITMFoodEat or one of the soup sounds. One test, read off the
        // record itself, no dependency on anything being cached.
        if (Forms::sndPotionUse && a_item->data.consumptionSound == Forms::sndPotionUse) {
            return meal;
        }

        meal.feeds = true;

        // A proper meal, and the only thing that fills the half of the bar that
        // lasts. RFAB's own keyword, by form rather than by name - the drinks
        // taught that lesson. Raw meat and anything with two effects still
        // count for their full WEIGHT below; what they do not count as is a
        // meal.
        meal.special = Forms::kwSpecialFood && a_item->HasKeyword(Forms::kwSpecialFood);

        // Raw food on a weak stomach comes straight back up. A strong-stomach
        // race or being undead is what makes it food instead.
        auto* player = RE::PlayerCharacter::GetSingleton();
        // ...but it is still worth what it is worth, and the value is worked
        // out for it too. The preview shows that and says nothing about the
        // stomach; the eating path checks this flag first and empties the bar.
        meal.rawWeak =
            Forms::kwRawFood && a_item->HasKeyword(Forms::kwRawFood) && player &&
            !(Forms::kwStrongStomach && player->HasKeyword(Forms::kwStrongStomach)) &&
            !(Forms::kwUndead && player->HasKeyword(Forms::kwUndead));

        // A prepared dish counts double. Keywords are referenced by name rather
        // than by form id: RFAB updates regularly, and a keyword it already
        // defines is a stable thing to point at.
        const bool substantial =
            meal.special ||
            (Forms::kwRawFood && a_item->HasKeyword(Forms::kwRawFood)) ||
            a_item->effects.size() >= 2;

        // Some produce weighs nothing; it should still count for a little
        // rather than for nothing at all.
        float weight = a_item->GetWeight();
        if (weight <= 0.0f) {
            weight = 0.1f;
        }

        const float share = substantial ? Settings::fFoodShareSpecial
                                        : Settings::fFoodShareNormal;

        // Gutworm eats first. A quarter, a half, then four fifths of the meal
        // never reaches the player.
        const float kept = 1.0f - StagedDisease::GutwormFoodPenalty();
        meal.restore = std::clamp(weight * share * kept, 0.0f, 1.0f);
        return meal;
    }

    float Needs::ColdGift(RE::AlchemyItem* a_item)
    {
        if (!a_item) {
            return 0.0f;
        }

        // Every frost-resistance effect on the item, summed - a draught with
        // two of them is worth both. The magnitude is the one on the ITEM
        // (EFIT), not the base effect's: that is where a potion's strength
        // actually lives, and two potions sharing one base effect differ only
        // there.
        float points = 0.0f;
        for (const auto* effect : a_item->effects) {
            if (!effect || !effect->baseEffect) {
                continue;
            }
            if (effect->baseEffect->data.primaryAV != RE::ActorValue::kResistFrost) {
                continue;
            }
            // Frost VULNERABILITY is a real thing in this mod and it must not
            // hand out time. What it does instead is already modelled: negative
            // resist lowers warmth, which lowers the equilibrium and speeds the
            // fall. Taking the buffer away on top would charge for it twice.
            points += std::max(0.0f, effect->effectItem.magnitude);
        }

        return points * Settings::fColdPerResistPoint;
    }

    void Needs::OnDrankWarm(float a_gift)
    {
        if (a_gift <= 0.0f) {
            return;
        }

        // The ceiling is on the SUM. A player at 0.9 with a 0.25 draught gets
        // 0.1 of buffer and no more - the bar cannot hold more than a bar, and
        // a buffer that could would be a second, hidden reserve.
        const float before = _state.coldTemp;
        _state.coldTemp = std::clamp(_state.coldTemp + a_gift, 0.0f,
            std::max(0.0f, 1.0f - _state.cold));
        logger::info("warm draught: +{:.3f} bought (offered {:.3f}, "
                     "buffer {:.3f} -> {:.3f}, base {:.3f})",
            _state.coldTemp - before, a_gift, before, _state.coldTemp, _state.cold);
    }

    float Needs::HungerPreview(RE::AlchemyItem* a_item) const
    {
        const auto meal = Assess(a_item);
        if (!meal.feeds) {
            return -1.0f;
        }

        // meal.rawWeak is not consulted. See the note on the declaration: the
        // preview promises the meal, the stomach has the last word.
        return std::clamp(Hunger() + meal.restore, 0.0f, 1.0f);
    }

    void Needs::OnAte(float a_restore, bool a_special)
    {
        // Only what fits. The bar is the cap, not each half: a full stomach
        // takes nothing, however the fullness is made up.
        const float room = std::max(0.0f, 1.0f - Hunger());
        const float ate = std::min(a_restore, room);

        if (a_special) {
            _state.hungerSpecial += ate;
        } else {
            _state.hungerFast += ate;
        }

        logger::info("ate {} (+{:.2f} of {:.2f}) -> hunger {:.2f} ({:.2f}+{:.2f})",
            a_special ? "a meal" : "fast food", ate, a_restore, Hunger(),
            _state.hungerSpecial, _state.hungerFast);
    }

    void Needs::EmptyHunger()
    {
        _state.hungerSpecial = 0.0f;
        _state.hungerFast = 0.0f;
        logger::info("ate raw on a weak stomach -> hunger 0.00");
    }

    void Needs::StaggerPlayer(float a_force)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) {
            return;
        }
        // staggerMagnitude is 0..1; the direction variable decides which way,
        // and 0 is straight back, which is what being sick looks like.
        player->SetGraphVariableFloat("staggerDirection"sv, 0.0f);
        player->SetGraphVariableFloat("staggerMagnitude"sv,
            std::clamp(a_force, 0.0f, 1.0f));
        player->NotifyAnimationGraph("staggerStart"sv);
    }

    // --- serialization -----------------------------------------------------

    struct NeedsSerialization
    {
        static void Save(SKSE::SerializationInterface* a_intfc)
        {
            auto& state = Needs::GetSingleton()._state;
            if (!a_intfc->OpenRecord(RECORD_NEEDS, RECORD_VERSION)) {
                logger::error("could not open the needs record for writing");
                return;
            }
            a_intfc->WriteRecordData(state);

            // Diseases go in their own record: the set of ids is open, so it is
            // written as a count and then length-prefixed names, rather than as
            // a struct that would have to change shape every time one is added.
            auto& diseases = Disease::GetSingleton().All();
            if (!a_intfc->OpenRecord(RECORD_DISEASE, RECORD_VERSION)) {
                logger::error("could not open the disease record for writing");
                return;
            }
            const auto count = static_cast<std::uint32_t>(diseases.size());
            a_intfc->WriteRecordData(count);
            for (const auto& [id, entry] : diseases) {
                const auto length = static_cast<std::uint32_t>(id.size());
                a_intfc->WriteRecordData(length);
                a_intfc->WriteRecordData(id.data(), length);
                a_intfc->WriteRecordData(entry);
            }

            // What the player has built. Form ids of live references, which the
            // interface remaps on load - a reference saved in one load order
            // and read back in another is not the same number.
            if (!a_intfc->OpenRecord(RECORD_CAMP, RECORD_VERSION)) {
                logger::error("could not open the camp record for writing");
                return;
            }
            a_intfc->WriteRecordData(Campfire::GetSingleton().Data());

            auto&      camps = Bedroll::GetSingleton().Camps();
            const auto laidCount = static_cast<std::uint32_t>(camps.size());
            a_intfc->WriteRecordData(laidCount);
            for (const auto& [furniture, tent] : camps) {
                a_intfc->WriteRecordData(furniture);
                a_intfc->WriteRecordData(tent);
            }

            // Which trees are still growing back. This has to survive a save or
            // the cooldown is not ours at all: the vanilla harvested flag would
            // stay set until the cell resets, and in this pack that is three
            // in-game years away.
            auto& cut = Trees::GetSingleton().Table();
            if (!a_intfc->OpenRecord(RECORD_TREES, RECORD_VERSION)) {
                logger::error("could not open the tree record for writing");
                return;
            }
            const auto trees = static_cast<std::uint32_t>(cut.size());
            a_intfc->WriteRecordData(trees);
            for (const auto& [id, when] : cut) {
                a_intfc->WriteRecordData(id);
                a_intfc->WriteRecordData(when);
            }
        }

        static void Load(SKSE::SerializationInterface* a_intfc)
        {
            std::uint32_t type = 0;
            std::uint32_t version = 0;
            std::uint32_t length = 0;

            while (a_intfc->GetNextRecordInfo(type, version, length)) {
                if (version < RECORD_VERSION_MIN || version > RECORD_VERSION) {
                    logger::warn("record {:X} is version {}, expected {}..{} - skipped",
                        type, version, RECORD_VERSION_MIN, RECORD_VERSION);
                    continue;
                }

                if (type == RECORD_DISEASE) {
                    std::uint32_t count = 0;
                    a_intfc->ReadRecordData(count);
                    for (std::uint32_t i = 0; i < count; ++i) {
                        // Not "length": that is the record's own length from
                        // GetNextRecordInfo, and shadowing it here means a
                        // mistake reads the wrong number of bytes in silence.
                        std::uint32_t idLength = 0;
                        a_intfc->ReadRecordData(idLength);

                        std::string id;
                        id.resize(idLength);
                        a_intfc->ReadRecordData(id.data(), idLength);

                        Disease::State entry{};
                        a_intfc->ReadRecordData(entry);
                        Disease::GetSingleton().Get(id) = entry;
                    }
                    logger::info("{} disease states restored", count);
                    continue;
                }

                if (type == RECORD_TREES) {
                    std::uint32_t count = 0;
                    a_intfc->ReadRecordData(count);

                    auto& cut = Trees::GetSingleton().Table();
                    cut.clear();
                    std::uint32_t kept = 0;
                    for (std::uint32_t i = 0; i < count; ++i) {
                        RE::FormID id = 0;
                        float      when = 0.0f;
                        a_intfc->ReadRecordData(id);
                        a_intfc->ReadRecordData(when);
                        // Same rule as the camp: an id from another load order
                        // is not the same reference.
                        if (id && a_intfc->ResolveFormID(id, id)) {
                            cut[id] = when;
                            ++kept;
                        }
                    }
                    logger::info("{} cut trees restored", kept);
                    continue;
                }

                if (type == RECORD_CAMP) {
                    Campfire::State fire{};
                    a_intfc->ReadRecordData(fire);

                    // Every reference id has to be put through ResolveFormID:
                    // the load order can differ from the one that saved, and an
                    // unresolved id points at whatever now sits in that slot.
                    // Anything that will not resolve is gone, so it is dropped
                    // rather than kept as a handle to something else.
                    const auto fix = [&](RE::FormID& a_id) {
                        if (a_id && !a_intfc->ResolveFormID(a_id, a_id)) {
                            a_id = 0;
                        }
                    };
                    fix(fire.fire);
                    fix(fire.spit);
                    fix(fire.pot);
                    Campfire::GetSingleton().Data() = fire;

                    std::uint32_t camps = 0;
                    a_intfc->ReadRecordData(camps);

                    auto& laid = Bedroll::GetSingleton().Camps();
                    laid.clear();
                    for (std::uint32_t i = 0; i < camps; ++i) {
                        RE::FormID furniture = 0;
                        RE::FormID tent = 0;
                        a_intfc->ReadRecordData(furniture);
                        a_intfc->ReadRecordData(tent);
                        fix(furniture);
                        fix(tent);
                        if (furniture) {
                            laid[furniture] = tent;
                        }
                    }

                    logger::info("camp restored (fire {:08X} spit {:08X} pot {:08X}, "
                                 "{} bedroll(s))",
                        fire.fire, fire.spit, fire.pot, laid.size());
                    continue;
                }

                if (type != RECORD_NEEDS) {
                    logger::warn("unknown record {:X} in the co-save, skipped", type);
                    continue;
                }

                Needs::State state{};
                bool          ok = false;

                if (version >= RECORD_VERSION) {
                    ok = a_intfc->ReadRecordData(state);
                } else if (version == 4) {
                    // A save made before sleep had halves. What was in the bar
                    // was earned somewhere, and there is no telling where, so
                    // all of it counts as a real bed - starting the player on a
                    // bedroll's worth would burn most of it within the hour.
                    StateV4 old{};
                    ok = a_intfc->ReadRecordData(old);
                    if (ok) {
                        state.sleep = old.sleep;
                        state.sleepTemp = 0.0f;
                        state.hungerSpecial = old.hungerSpecial;
                        state.hungerFast = old.hungerFast;
                        state.cold = old.cold;
                        state.coldTemp = old.coldTemp;
                        state.wetness = old.wetness;
                        logger::info("needs record upgraded from version {}", version);
                    }
                } else if (version == 3) {
                    // A save made before the cold axis had a buffer. Nothing
                    // was bought, so there is nothing to restore: coldTemp
                    // starts at zero and the reserve is what it always was.
                    StateV3 old{};
                    ok = a_intfc->ReadRecordData(old);
                    if (ok) {
                        state.sleep = old.sleep;
                        state.sleepTemp = 0.0f;
                        state.hungerSpecial = old.hungerSpecial;
                        state.hungerFast = old.hungerFast;
                        state.cold = old.cold;
                        state.coldTemp = 0.0f;
                        state.wetness = old.wetness;
                        logger::info("needs record upgraded from version {}", version);
                    }
                } else {
                    // A save made before hunger had halves. What was in the bar
                    // was earned the hard way, so all of it counts as a proper
                    // meal; starting the player on fast food would quietly eat
                    // most of their bar within the hour.
                    StateV2 old{};
                    ok = a_intfc->ReadRecordData(old);
                    if (ok) {
                        state.sleep = old.sleep;
                        state.sleepTemp = 0.0f;
                        state.hungerSpecial = old.hunger;
                        state.hungerFast = 0.0f;
                        state.cold = old.cold;
                        state.coldTemp = 0.0f;
                        state.wetness = old.wetness;
                        logger::info("needs record upgraded from version {}", version);
                    }
                }

                if (ok) {
                    // The clock reading is deliberately discarded: on load it
                    // belongs to whenever the save was made, and keeping it
                    // would charge the player for the gap.
                    state.lastGameDays = -1.0f;
                    Needs::GetSingleton()._state = state;
                    logger::info("needs restored (sleep {:.2f} ({:.2f}+{:.2f}) "
                                 "hunger {:.2f} ({:.2f}+{:.2f}) "
                                 "cold {:.2f} ({:.2f}+{:.2f}))",
                        state.sleep + state.sleepTemp, state.sleep, state.sleepTemp,
                        state.hungerSpecial + state.hungerFast,
                        state.hungerSpecial, state.hungerFast,
                        state.cold + state.coldTemp, state.cold, state.coldTemp);
                }
            }
        }

        static void Revert(SKSE::SerializationInterface*)
        {
            Needs::GetSingleton().Reset();
            Disease::GetSingleton().Clear();
            Hypothermia::GetSingleton().Forget();
            ColdVisual::GetSingleton().Forget();
            Campfire::GetSingleton().Reset();
            Bedroll::GetSingleton().Reset();
        }
    };

    void Needs::InstallSerialization()
    {
        auto* intfc = SKSE::GetSerializationInterface();
        if (!intfc) {
            logger::error("no serialization interface - needs will not persist");
            return;
        }
        intfc->SetUniqueID(SERIALIZATION_ID);
        intfc->SetSaveCallback(NeedsSerialization::Save);
        intfc->SetLoadCallback(NeedsSerialization::Load);
        intfc->SetRevertCallback(NeedsSerialization::Revert);
        logger::info("serialization installed");
    }
}
