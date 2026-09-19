#include "PCH.h"

#include "Core/Climate.h"

#include "Core/ClimateMap.h"

#include "Core/Forms.h"
#include "Core/Campfire.h"
#include "Core/ColdScreen.h"
#include "Core/Shelter.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // Tamriel, and the one number in this file that is a form id: the
        // worldspace is a master record and cannot move.
        constexpr RE::FormID TAMRIEL = 0x0000003C;

        // Does this worldspace measure in Tamriel's own coordinates?
        //
        // Skyrim's cities are separate worldspaces, so asking the player where
        // they are inside Whiterun gives a position in WhiterunWorld - and the
        // outlines would be read against the wrong origin if that were not
        // checked. It turns out no conversion is needed: WhiterunWorld,
        // SolitudeWorld, MarkarthWorld, RiftenWorld and WindhelmWorld all sit
        // on Tamriel's map at 1:1 with no offset (ONAM) and take its sky
        // (PNAM), and their cell grids line up with the parent's - measured,
        // not assumed.
        //
        // The worldspaces that do NOT are the ones that say so in the same two
        // fields: Blackreach carries a scale of 2.5 and an offset, Sovngarde a
        // scale of -1, and the dungeon worldspaces carry offsets of their own.
        // Those fall through to Unknown, which has its own temperature rather
        // than a nonsense region read off a foreign coordinate system.
        [[nodiscard]] bool SharesTamrielCoords(const RE::TESWorldSpace* a_ws)
        {
            int guard = 0;
            for (const auto* ws = a_ws; ws && guard < 8; ws = ws->parentWorld, ++guard) {
                if (ws->GetFormID() == TAMRIEL) {
                    return true;
                }
                const auto& o = ws->worldMapOffsetData;
                const bool identity = o.mapScale == 1.0f && o.mapOffsetX == 0.0f &&
                                      o.mapOffsetY == 0.0f && o.mapOffsetZ == 0.0f;
                if (!identity ||
                    !ws->parentUseFlags.any(RE::TESWorldSpace::ParentUseFlag::kUseSkyCell)) {
                    return false;
                }
            }
            return false;
        }

        // Ice caves and frozen ruins: an interior that is no warmer than the
        // glacier it is cut into. The list is a FLST of locations in the plugin,
        // the same set CC Survival Mode uses, and the check is the same parent
        // walk as the hold - a cell's own location is usually the child.
        [[nodiscard]] bool ColdInteriorHere(RE::BGSLocation* a_location)
        {
            if (!Forms::coldInteriors) {
                return false;
            }
            int guard = 0;
            for (auto* loc = a_location; loc && guard < 16; loc = loc->parentLoc, ++guard) {
                if (Forms::coldInteriors->HasForm(loc)) {
                    return true;
                }
            }
            return false;
        }

        // What the sky is worth, and what to call it in the log.
        struct Weather
        {
            float       offset;
            const char* name;
            const char* from;   // the weather being left, while it is still felt
            float       blend;  // how much of `name` is in the mix, 0..1
        };

        [[nodiscard]] float WeatherOffsetOf(const RE::TESWeather* a_weather,
            const char*& a_name)
        {
            a_name = "clear";
            if (!a_weather) {
                return 0.0f;
            }
            const auto flags = a_weather->data.flags;
            if (flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
                a_name = "snow";
                return Settings::fWeatherSnow;
            }
            if (flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
                a_name = "rain";
                return Settings::fWeatherRain;
            }
            if (flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)) {
                a_name = "cloud";
                return Settings::fWeatherCloudy;
            }
            return 0.0f;
        }

        // The sky, blended over the engine's own weather transition.
        //
        // Skyrim does not switch weather, it crossfades it over minutes, and it
        // says how far along that is: `currentWeatherPct` is the share of the
        // new weather, `lastWeather` is the one being left. Reading the flags of
        // the current weather alone made the temperature step four degrees the
        // instant a blizzard was CHOSEN, while the sky outside was still clear.
        //
        // Nothing is invented here and there is no constant to tune: the
        // temperature now ramps over exactly the minutes the visuals do.
        [[nodiscard]] Weather WeatherNow()
        {
            const auto* sky = RE::Sky::GetSingleton();
            if (!sky) {
                return { 0.0f, "clear", "clear", 1.0f };
            }

            const char* nowName = "clear";
            const float now = WeatherOffsetOf(sky->currentWeather, nowName);

            const float pct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
            if (!sky->lastWeather || pct >= 1.0f) {
                return { now, nowName, nowName, 1.0f };
            }

            const char* wasName = "clear";
            const float was = WeatherOffsetOf(sky->lastWeather, wasName);
            return { was + (now - was) * pct, nowName, wasName, pct };
        }

        // Is anything falling that WETS you? Rain, and nothing else.
        //
        // Snow used to count and no longer does. Dry snow does not soak
        // clothing, and the model had it doing the work twice over: a blizzard
        // is already the coldest sky there is, and stacking the wet penalty on
        // top of it made snowfall worth more than everything else put together.
        // Water still soaks - swimming is handled by the caller.
        //
        // Getting wet is a yes or no, so the crossfade is read at its midpoint:
        // whichever weather holds more than half the mix is the one falling on
        // you. That keeps it in step with the temperature, which blends over the
        // same transition.
        [[nodiscard]] bool Raining()
        {
            const auto* sky = RE::Sky::GetSingleton();
            if (!sky) {
                return false;
            }
            const float pct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
            const auto* dominant = (pct >= 0.5f || !sky->lastWeather)
                                       ? sky->currentWeather
                                       : sky->lastWeather;
            return dominant && dominant->data.flags.any(
                                   RE::TESWeather::WeatherDataFlag::kRainy);
        }

        // The day, as a curve rather than a switch.
        //
        //     offset = -amp * (1 - cos(2pi (h - peak) / 24)) / 2
        //
        // Zero at the warmest hour and -amp twelve hours later, moving
        // smoothly between. It is never positive on purpose: the region
        // temperatures were solved against the target table's daytime rows, so
        // the warmest hour is the temperature the region IS, and every other
        // hour is a subtraction from it. A curve that also added would move
        // every one of those rows.
        // The coldest hour of the day is the one before the sun comes up, and
        // dawn is not a constant: every climate record carries its own sunrise,
        // and Skyrim's regions do not share one. So it is read from the climate
        // the sky is actually running rather than assumed to sit twelve hours
        // from the peak. Falls back to the old assumption if the sky has no
        // climate yet, which happens for a few frames on a load.
        [[nodiscard]] float ColdestHour(float a_warm)
        {
            const auto* sky = RE::Sky::GetSingleton();
            const auto* climate = sky ? sky->currentClimate : nullptr;
            if (!climate) {
                return std::fmod(a_warm + 12.0f, 24.0f);
            }
            // The record keeps sunrise in ten-minute steps.
            const float dawn = static_cast<float>(climate->timing.sunrise.begin) / 6.0f;
            return std::fmod(dawn - 1.0f + 24.0f, 24.0f);
        }

        [[nodiscard]] float DayOffset(float a_hour)
        {
            constexpr float PI = 3.14159265f;

            const float warm = std::fmod(Settings::fDayPeakHour + 24.0f, 24.0f);
            const float cold = ColdestHour(warm);

            // The two halves of the day are no longer the same length, so the
            // phase is walked at its own pace in each: the cooling half spends
            // the whole of PI between the peak and the pre-dawn hour, and the
            // warming half spends the rest getting back. Cosine still shapes
            // both, so the curve is flat at both anchors and has no corner
            // where the halves meet.
            float fall = std::fmod(cold - warm + 24.0f, 24.0f);
            if (fall < 0.5f || fall > 23.5f) {
                fall = 12.0f;          // the anchors have collapsed onto each other
            }
            const float rise = 24.0f - fall;

            const float t = std::fmod(a_hour - warm + 24.0f, 24.0f);
            const float phase = (t <= fall) ? PI * (t / fall)
                                            : PI + PI * ((t - fall) / rise);

            // fDayAmplitude is negative: the region temperatures ARE the
            // warmest-hour ones, so this only ever subtracts.
            return Settings::fDayAmplitude * 0.5f * (1.0f - std::cos(phase));
        }

        // Insulation from what the player is wearing, plus frost resistance as
        // an equal partner rather than an afterthought. Deliberately per-slot
        // and flat: rating individual armours would mean writing warmth values
        // into RFAB's own records, and that is not a road this mod goes down.
        // The two halves come back separately as well as summed: they are
        // tuned against each other, and the debug log has to be able to say
        // which of them is carrying.
        [[nodiscard]] float WornWarmth(RE::PlayerCharacter* a_player, int& a_slots,
            float& a_resist)
        {
            a_slots = 0;
            a_resist = 0.0f;
            if (!a_player) {
                return 0.0f;
            }

            using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

            // Four parts of the body, and the slots each of them is actually
            // worn in. The head takes two, and that is the whole of this fix:
            // a hood is NOT slot 30. Skyrim puts hoods on 31 (hair) together
            // with 42 (circlet) and reserves 30 for helmets - measured across
            // this load order, 137 head pieces on 31 against 16 on 30. Asking
            // only about 30 lost the head slot for anyone wearing a hood,
            // which is most of the early game, and cost them 14.7 of warmth
            // without a word anywhere.
            //
            // 42 is deliberately not here: a circlet on its own is jewellery,
            // and hoods already carry 31.
            constexpr std::pair<Slot, Slot> PARTS[] = {
                { Slot::kBody,  Slot::kBody },
                { Slot::kHead,  Slot::kHair },
                { Slot::kHands, Slot::kHands },
                { Slot::kFeet,  Slot::kFeet },
            };

            for (const auto& [first, second] : PARTS) {
                if (a_player->GetWornArmor(first) || a_player->GetWornArmor(second)) {
                    ++a_slots;
                }
            }

            if (auto* avOwner = a_player->AsActorValueOwner()) {
                a_resist = avOwner->GetActorValue(RE::ActorValue::kResistFrost);
            }
            return a_slots * Settings::fWarmthPerSlot +
                   a_resist * Settings::fFrostResistWeight;
        }
    }

    namespace
    {
        // A lit torch in either hand. v0.4.0 counts it towards warmth but not
        // towards the warm-hands idle, which is why it is a separate term
        // rather than folded into the fire search.
        [[nodiscard]] bool HoldingTorch(RE::PlayerCharacter* a_player)
        {
            for (const bool leftHand : { false, true }) {
                auto* worn = a_player->GetEquippedObject(leftHand);
                if (worn && worn->Is(RE::FormType::Light)) {
                    return true;
                }
            }
            return false;
        }

        // Any world fire within the radius.
        //
        // v0.4.0 asked the engine directly through
        // FindClosestReferenceOfAnyTypeInListFromRef; CommonLibSSE has no
        // binding for it, so the references in range are walked instead and
        // their base objects checked against the same form list. It stops at
        // the first hit, and the list is what the generator built - burning
        // campfires are Moveable Statics, which is the entry most home-made
        // fire checks miss.
        [[nodiscard]] RE::TESObjectREFR* FindFire(RE::PlayerCharacter* a_player,
            float a_radius)
        {
            auto* tes = RE::TES::GetSingleton();
            auto* list = Forms::fireSources;
            // No model means no loaded cell to walk, and walking it anyway is
            // not safe mid-transition.
            if (!tes || !list || !a_player || !a_player->Is3DLoaded()) {
                return nullptr;
            }

            RE::TESObjectREFR* best = nullptr;
            float              bestDist = std::numeric_limits<float>::max();
            const auto         from = a_player->GetPosition();

            tes->ForEachReferenceInRange(a_player, a_radius,
                [&](RE::TESObjectREFR& a_ref) {
                    if (a_ref.IsDisabled() || a_ref.IsDeleted()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* base = a_ref.GetBaseObject();
                    if (!base || !list->HasForm(base)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    // Nearest, not first: the idle turns towards it, so which
                    // one it is matters.
                    const auto  at = a_ref.GetPosition();
                    const float dx = at.x - from.x;
                    const float dy = at.y - from.y;
                    const float dz = at.z - from.z;
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < bestDist) {
                        bestDist = d2;
                        best = &a_ref;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            return best;
        }

        [[nodiscard]] bool NearFire(RE::PlayerCharacter* a_player, float a_radius)
        {
            return FindFire(a_player, a_radius) != nullptr;
        }

        // Which nearby things are NOT on the fire list.
        //
        // The fire list is built by an EditorID mask in the generator, and a
        // mask always misses something - the College of Winterhold's central
        // font warms nobody while the small lights around it do. Guessing the
        // record name from memory is how that gets fixed wrong; this prints the
        // candidates standing next to the player so the mask can be extended
        // from evidence.
        //
        // The first version filtered on the editor id containing a word like
        // "fire" or "light" and printed nothing at all: GetFormEditorID is
        // empty for most records unless po3_Tweaks has cached them, so the
        // filter threw everything away. It filters on form type instead, which
        // is always there, and prints the id when there is one.
        //
        // Debug only, at most once every few seconds, and capped so a busy room
        // cannot fill the log.
        void LogUnlistedHeat(RE::PlayerCharacter* a_player, float a_radius)
        {
            static std::chrono::steady_clock::time_point last{};
            const auto                                   now = std::chrono::steady_clock::now();
            if (std::chrono::duration<float>(now - last).count() < 5.0f) {
                return;
            }
            last = now;

            auto* tes = RE::TES::GetSingleton();
            auto* list = Forms::fireSources;
            if (!tes || !list || !a_player || !a_player->Is3DLoaded()) {
                return;
            }

            constexpr int MAX_LINES = 12;
            int           printed = 0;
            const auto    from = a_player->GetPosition();

            tes->ForEachReferenceInRange(a_player, a_radius,
                [&](RE::TESObjectREFR& a_ref) {
                    if (printed >= MAX_LINES) {
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    if (a_ref.IsDisabled() || a_ref.IsDeleted()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    auto* base = a_ref.GetBaseObject();
                    if (!base || list->HasForm(base)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    // The shapes a heat source actually takes. STAT was in
                    // this list on the first pass and drowned the log in
                    // XMarker, MapMarker and wall sections - nothing that has
                    // ever been a fire is a static.
                    const auto type = base->GetFormType();
                    if (type != RE::FormType::Light && type != RE::FormType::Activator &&
                        type != RE::FormType::MovableStatic &&
                        type != RE::FormType::Furniture) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    // The REFERENCE as well as the base. A base id alone names
                    // a kind of thing, and the question this line gets asked is
                    // always about one particular thing: which object is that,
                    // where is it, and is it one of ours. A base in the dynamic
                    // FF space says the object was made during play, and then
                    // the base id is not even a stable name for it.
                    const char*  edid = base->GetFormEditorID();
                    const auto   at = a_ref.GetPosition();
                    const char*  name = a_ref.GetName();
                    logger::info("  heat? {:<34} [{:08X}] {} ref {:08X} "
                                 "at {:.0f},{:.0f},{:.0f} d {:.0f}{}{}",
                        (edid && *edid) ? edid : "<no editor id>",
                        base->GetFormID(), RE::FormTypeToString(type),
                        a_ref.GetFormID(), at.x, at.y, at.z,
                        from.GetDistance(at),
                        (name && *name) ? " " : "", (name && *name) ? name : "");
                    ++printed;
                    return RE::BSContainer::ForEachResult::kContinue;
                });
        }
    }

    Climate Climate::Sample()
    {
        Climate out;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return out;
        }

        out.warmth = WornWarmth(player, out.slots, out.resist);

        if (Settings::bDebugLog) {
            LogUnlistedHeat(player, Settings::fFireRadius);
        }

        const auto* cell = player->GetParentCell();
        out.interior = cell && cell->IsInteriorCell();
        out.worldZ = player->GetPositionZ();

        if (out.interior) {
            // Interiors are their own thing: no weather, no region, and no
            // day curve either - a room is the same room at four in the
            // morning, and the owner's call was that indoors stays the warmer
            // of the two.
            const bool icy =
                ColdInteriorHere(player->GetPlayerRuntimeData().currentLocation);
            out.placeName = icy ? "cold interior" : "interior";
            out.baseTemp = icy ? Settings::fColdInteriorTemp : Settings::fInteriorTemp;
            out.temperature = out.baseTemp;
            out.sheltered = true;
            // Recorded for the log and nothing else. A fire of your own is
            // worth what any fire is worth.
            out.ownFire = Campfire::GetSingleton().PlayerAtOwnFire();

            // Same order as outdoors: the fire is a share of what is still
            // missing, so it is taken after everything that moved the
            // temperature. A torch is scaled here too - it was not before, and
            // a torch held up in a room being worth a whole hearth was an
            // oversight rather than a rule.
            {
                const float beforeFire = out.baseTemp;
                if (NearFire(player, Settings::fFireRadius)) {
                    out.nearFire = true;
                    out.fireOffset = Settings::fFireMaxDeg;
                } else if (HoldingTorch(player)) {
                    out.nearFire = true;
                    out.fireOffset = Settings::fTorchOfFire;
                }
                out.temperature = beforeFire + out.fireOffset;
            }
            return out;
        }

        const auto at = player->GetPosition();

        // THE MAP IS THE ANSWER. No region, no height line, no rectangles -
        // see ClimateMap.h for why all three went, and docs/CLIMATE_MAP.md for
        // what replaced them.
        //
        // Which grid to ask: Solstheim has its own coordinates, so the same
        // numbers mean different places there and in Tamriel.
        const auto* ws = player->GetWorldspace();
        std::optional<ClimateMap::World> world;
        if (Forms::wsSolstheim && ws == Forms::wsSolstheim) {
            world = ClimateMap::World::Solstheim;
        } else if (SharesTamrielCoords(ws)) {
            world = ClimateMap::World::Tamriel;
        }

        if (const auto surface =
                world ? ClimateMap::Surface(*world, at.x, at.y) : std::nullopt) {
            out.baked = true;
            out.surfaceTemp = *surface;

            // The ground under the player, not the player's own feet: standing
            // on a tower does not make the tower's floor into terrain.
            float ground = at.z;
            if (auto* tes = RE::TES::GetSingleton()) {
                float height = 0.0f;
                if (tes->GetLandHeight(at, height)) {
                    ground = height;
                }
            }
            out.airAbove = at.z - ground;
            out.baseTemp = ClimateMap::WithAltitude(*surface, ground, at.z);
            out.placeName = "the map";
        } else {
            // OFF THE MAP: a worldspace of its own - Blackreach, Sovngarde, the
            // Soul Cairn, another mod's island - or a spot with no terrain
            // record under it. One temperature, and the log says which.
            //
            // A second climate model used to answer this, and keeping two of
            // them in step bought nothing: the map covers every place the
            // player reaches under an open sky.
            out.baseTemp = Settings::fTempUnknown;
            out.placeName = "off the map";
        }

        const auto weather = WeatherNow();
        out.weatherOffset = weather.offset;
        out.weatherName = weather.name;
        out.weatherFrom = weather.from;
        out.weatherBlend = weather.blend;

        const auto* calendar = RE::Calendar::GetSingleton();
        out.hour = calendar ? calendar->GetHour() : 12.0f;
        out.dayOffset = DayOffset(out.hour);

        // Overhead cover, asked properly. A roof is a roof whether it is a
        // cave mouth, a bridge, a rock shelf or the player's own tent.
        //
        // It is worth no degrees at all. A roof does not heat the air under it
        // - it keeps the rain off, and that is its entire job here: nothing
        // falling on you means nothing soaking you, and wet clothes are what
        // the cold actually charges for.
        out.sheltered = Shelter::GetSingleton().Overhead();

        // Rain only reaches you under open sky. This is the whole point of
        // asking about the roof, and it is what makes finding cover in a
        // downpour a decision rather than scenery. Snow does not wet at all
        // any more - that is the owner's rule, and a blizzard is already the
        // coldest sky there is without counting twice.
        out.soaking = Raining() && !out.sheltered;

        if (auto* state = player->AsActorState(); state && state->IsSwimming()) {
            out.soaking = true;
            out.inWater = true;
        }

        // A FIRE IS A FIRE, whoever lit it. Owning one used to be worth a
        // flat ten degrees on top, and that is gone: it took the model past its
        // own target table, where row 18 asks for 0.50+ in the mountains at a
        // fire and the share alone already returns 0.599. What a fire of the
        // player's own is for is that it can be carried to where there is no
        // fire, which is most of the province, and the perks pay for exactly
        // that: lighting one at all, and banking it to last the night instead
        // of four hours.
        //
        // The tent is deliberately not here either. A tent adds no warmth at
        // all, awake or asleep; what it does is stop the cold falling past
        // fTentColdFloor while you sleep in it, and slow the fall while you do.
        // Keeping heat and making heat are different jobs, and the tent only
        // has the first. (Its canvas still counts as a roof, like any other
        // roof - which keeps the rain off and is worth no degrees.)
        out.ownFire = Campfire::GetSingleton().PlayerAtOwnFire();

        // A fire is warmth you can walk up to, so it raises the temperature
        // rather than reducing the load - the difference matters, because it
        // means standing by a fire in a blizzard is still colder than standing
        // by the same fire in a mild wood.
        //
        // It goes last because what it is worth depends on everything else: a
        // share of what the cold is still short by, after the region, the sky,
        // the hour, the roof and the camp have all had their say.
        const float beforeFire =
            out.baseTemp + out.weatherOffset + out.dayOffset;

        if (NearFire(player, Settings::fFireRadius)) {
            out.nearFire = true;
            out.fireOffset = Settings::fFireMaxDeg;
        } else if (HoldingTorch(player)) {
            out.nearFire = true;
            out.fireOffset = Settings::fTorchOfFire;
        }

        out.temperature = beforeFire + out.fireOffset;

        // AND THE WATER, after everything, the fire included.
        //
        // Every term above answers for the AIR over this spot, and the
        // air over a river can be perfectly mild - the baked field says
        // so and it is right. Water is not air: it takes heat out of a
        // body at a rate nothing on that scale describes. So going in is
        // worth a flat drop, applied where nothing can soften it, and the
        // indicator falls with it - which is the point. It should read as
        // freezing the moment the player is in, not once they climb out
        // wet.
        if (out.inWater) {
            out.temperature += Settings::fSwimTemp;
        }

        return out;
    }

    float Climate::ColdLoad() const
    {
        // Everything is expressed as a load on the player. Warmth and shelter
        // reduce it; cold air and being wet increase it.
        const float chill = std::max(0.0f, Settings::fComfortTemp - temperature);

        return chill * Settings::fLoadPerDegree +
               wetness * Settings::fWetLoad -
               DryWarmth() * Settings::fWarmthRelief;
    }

    float Climate::DryWarmth() const
    {
        // Wet CLOTHES stop insulating, and only the clothes.
        //
        // This multiplied the whole of `warmth`, which is the slots PLUS frost
        // resistance - so rain cut the enchantment and the potion with them. A
        // soaked player lost most of a resist effect that has nothing to do
        // with being wet, in the equilibrium and in the chilling speed both,
        // and nothing anywhere said so.
        //
        // Being soaked still costs the larger half: four slots are worth 58.7
        // and sixty per cent of that is what goes.
        return static_cast<float>(slots) * Settings::fWarmthPerSlot *
                   (1.0f - wetness * Settings::fWetWarmthLoss) +
               resist * Settings::fFrostResistWeight;
    }

    float Climate::ChillSlow() const
    {
        // The floor is for the other sign: a large negative FrostResist (a bad
        // cold, a curse) is meant to make you freeze faster, not to turn the
        // divisor negative and send the bar the wrong way.
        return std::max(0.1f, 1.0f + Settings::fWarmthSlowsChill * DryWarmth());
    }

    float Climate::ColdTarget() const
    {
        return std::clamp(1.0f - ColdLoad(), 0.0f, 1.0f);
    }

    int Climate::Feel() const
    {
        // WHAT THE ICON IS FOR: the one number the player cannot see.
        //
        // It reads the EQUILIBRIUM - where this place will leave you if you
        // stay - and not the bar, which is drawn two inches away with its own
        // notch on it. That is the whole of its job: standing at full in a
        // blizzard, the bar says everything is fine and the icon says it is
        // not, because the bar has not started falling yet.
        //
        // THE THRESHOLDS ARE NOT ITS OWN. Every one of them is a line the
        // player already meets somewhere else, so the icon cannot come to mean
        // something different from what it shows:
        //
        //     below BEGIN_AT      the screen effects start there
        //     below fColdSafe     the notch on the bar; penalties start there
        //     below the bonus     the full-bar bonus stops there
        //     at or above 1.0     a surplus: this place holds you full
        //
        // Move fColdSafe and the icon follows. The old thresholds were v0.4.0's
        // load steps (25 / 8 / -8 / -25 on a hundred-point severity scale), and
        // after the climate rework they had stopped dividing anything: four of
        // the five steps described surplus nobody can feel, while everything
        // from "you will sit at the notch" to "you will be dead in a quarter of
        // an hour" shared one icon.
        const float target = ColdTarget();

        float bonus = Settings::fBonusThresholdPct * 0.01f;
        if (bonus <= 0.0f) {
            bonus = 0.10f;  // the same guard Penalties uses
        }

        if (target < ColdScreen::BEGIN_AT) {
            return 0;
        }
        if (target < Settings::fColdSafe) {
            return 1;
        }
        if (target < 1.0f - bonus) {
            return 2;
        }
        if (target < 1.0f) {
            return 3;
        }
        return 4;
    }

    RE::TESObjectREFR* Climate::NearestFire(float a_radius)
    {
        return FindFire(RE::PlayerCharacter::GetSingleton(), a_radius);
    }

    bool Climate::RainOnPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* cell = player ? player->GetParentCell() : nullptr;
        if (!cell || cell->IsInteriorCell()) {
            return false;
        }
        return Raining() && !Shelter::GetSingleton().Overhead();
    }

    float Climate::ChillSpeed(float a_current) const
    {
        // WHERE YOU STOP AND HOW FAST YOU GET THERE ARE TWO QUESTIONS, and
        // ColdLoad answers only the first.
        //
        // It used to answer both: the speed was |ColdLoad| x fColdChillRate,
        // v0.4.0's law written as `(Severity - Mitigation) x ColdRate`. The
        // trouble is that ColdLoad is also what sets ColdTarget, so a colder
        // place lowered the equilibrium AND raised the speed with one number.
        // Distance and rate were welded together, and no slider could separate
        // them.
        //
        // Hypothermia never had that problem and is the model followed here:
        // `100 / (fHypoWorsenHours x ChillSlow)`. The place decides whether it
        // starts and where it ends; the coat decides how fast. So the chill is
        // a flat rate divided by insulation, and nothing else.
        //
        // Warming keeps its own flat rate - see ColdSpeed below - because a
        // coat that made a fire work more slowly would be the wrong lesson.
        const float base = Settings::fColdChillRate / ChillSlow();

        // Freezing eases in as it arrives, so the curve settles instead of
        // snapping: a speed that runs flat right up to the target and then
        // stops dead reads as a glitch. This is the one place the gap is
        // allowed to matter, and it can only ever slow the fall - the factor
        // is below one and approaches it.
        const float gap = std::abs(ColdTarget() - a_current);
        return base * (gap / (gap + std::max(0.01f, Settings::fColdChillEase)));
    }

    float Climate::ColdSpeed(float a_current) const
    {
        // WARMING RUNS AT ITS OWN RATE, AND NOTHING SCALES IT.
        //
        // It used to be |load| x fColdWarmRate, the mirror of the chilling
        // half, and that handed the player's own clothing the job of deciding
        // how fast a fire worked. Above the comfort temperature the chill term
        // is zero, so what was left of the load was almost entirely the warmth
        // relief: four slots warmed at full speed, two at half, and someone
        // with no insulation and no frost resistance did not warm at all -
        // standing in a bonfire. The slider could not rescue that either; at
        // x30 the climb was still slow, because the number it multiplies had
        // collapsed to nothing.
        //
        // So warming is flat. fColdWarmRate is bar per game hour and means
        // exactly that. Clothing, the tent and being wet still decide WHERE the
        // bar settles - that is ColdTarget, and a soaked player still cannot
        // reach the top - but no longer how fast it travels there.
        if (ColdTarget() > a_current) {
            return Settings::fColdWarmRate;
        }
        return ChillSpeed(a_current);
    }
}
