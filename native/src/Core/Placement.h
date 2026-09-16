#pragma once

// Where a thing dropped in front of the player should sit, and how it should
// lie on the ground.
//
// One havok ray, straight down at the spot, which answers both questions at
// once: where the surface is, and which way it faces.
//
// Campfire samples three points in a triangle and reads the slope off the
// height differences, because Papyrus cannot cast a ray - it fires spell
// projectiles downwards and catches where they land. Copying that shape here
// was a mistake: three samples a hundred units apart collect every rock and
// step between them, and the log showed the tilt pinned at the limit with the
// roll swinging from one extreme to the other. The ray hands over the surface
// normal with the hit and has none of that in it.
//
// GetLandHeight remains the fallback for a ray that hits nothing, and it knows
// only the terrain - no jetty, no bridge, no rock. When even that has no
// answer the spot falls back to the player's own feet with no rotation, which
// is what v0.4.0 did in every case, so the worst outcome is the old behaviour
// rather than a tent in a wall.
//
// The two angles are NOT worked out from first principles. Which way a positive
// pitch tips a reference is a convention, and guessing it is how the first
// version came out backwards on a slope without anyone noticing on flat ground.
// The magnitudes come from the slope; the signs are settled by building all
// four combinations with the engine's own EulerAnglesToAxesZXY and keeping the
// one whose up axis actually points along the surface normal.

namespace RSL::Placement
{
    // RADIANS, all three, because that is what TESObjectREFR::data.angle holds
    // and what GetAngleZ hands back - Papyrus is the one that works in degrees,
    // and mixing the two silently points everything the wrong way.
    struct Spot
    {
        RE::NiPoint3 position{};
        float        pitch{ 0.0f };     // X
        float        roll{ 0.0f };      // Y
        float        heading{ 0.0f };   // Z, taken from the player
        bool         grounded{ false }; // false means the fallback was used
    };

    // The tilt limit lives in Settings (fMaxPlacementTilt) - it is something to
    // look at in game and decide, not a constant. Campfire's 25 was too tight:
    // a fire on a real hillside sat visibly off the slope.
    //
    // Campfire's sampling triangle is gone too. It solved the slope from three
    // sampled heights because Papyrus had no rays to cast; one havok ray
    // returns the surface normal with the hit, and three samples a hundred
    // units apart only collected the rocks between them.

    // Is something solid between the player and where a thing would go?
    //
    // This is the whole "do not build it inside the cliff" check, and it has to
    // be its own question rather than a field on Spot: the caller asks it
    // BEFORE it spends anything - firewood, a dropped bedroll - because the
    // answer decides whether any of that happens.
    //
    // A level line of sight at chest height, not a ray that follows the ground.
    // Level is what makes it independent of the slope: at the distances things
    // are placed here, ground rising far enough to cross chest height is a
    // slope past 45 degrees, which is well beyond the tilt clamp and not
    // somewhere a camp belongs anyway. A ray that followed the ground would
    // need the ground first, and the ground is found by probing the very spot
    // whose validity is in question.
    //
    // That the ray starts inside the player and does not hit him is not an
    // assumption: Shelter::CastUp has cast from the same place on the same
    // layer since the climate model went in, and it reports open sky outdoors -
    // which it could not if the player's own body were in the way.
    [[nodiscard]] bool BlockedInFront(float a_distance);

    // a_distance is how far in front of the player to put it.
    // a_maxTiltDegrees is the caller's, because the answer is not the same
    // for everything that gets put down. A bedroll is LAID on the ground and
    // should follow it. A fire is BUILT - the stones are levelled first - and a
    // campfire lying along a hillside reads as broken however faithfully it
    // matches the slope.
    [[nodiscard]] Spot InFront(float a_distance, float a_maxTiltDegrees);
}
