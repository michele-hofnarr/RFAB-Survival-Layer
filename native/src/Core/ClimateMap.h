#pragma once

// The baked climate of Skyrim: a temperature at every vertex of the terrain.
//
// WHAT REPLACED WHAT. Until now the temperature of a place was worked out live
// from hand-drawn shapes: nine outlines to say which region a point fell in,
// six rectangles for the cold pockets an outline cannot see, and one height
// line at 16000 with a blend under it. None of that was expensive. All of it
// was coarse, and the rectangles were the coarsest: the one drawn round Bleak
// Falls Barrow was 61,900 units wide and declared the Guardian Stones a land
// of permanent snow.
//
// This is a grid instead, built offline by tools/bake_climate.py. Offline it
// can afford what a tick never could: the snow the artists painted on the
// ground, read from the texture layers of every LAND record, and how far each
// point stands above the land around it - a measurement that needs a
// neighbourhood, not a point. The field is fitted to hand-set temperatures at
// named places (docs/control_points.xlsx) and passes through them exactly.
//
// WHAT IT IS NOT. It is not a lookup table of the old model. Regions are gone
// from the answer entirely, boxes with them, and so is the height line. There
// is no step anywhere in the field: the grid is Skyrim's own 33x33 per cell, a
// vertex every 128 units, and between vertices the answer is interpolated
// across the triangle the point stands on.
//
// WHY THAT IS CHEAPER RATHER THAN DEARER. The grid is regular, so finding the
// cell and the triangle is arithmetic - two divisions and a couple of
// subtractions - not a search. What used to be 253 vertex iterations per
// sample is now one index.

namespace RSL::ClimateMap
{
    // The file carries one grid per worldspace, because Solstheim has its own
    // coordinates: the same numbers there and in Tamriel are different places,
    // so one grid could not hold both.
    //
    // Solstheim's grid is PROVISIONAL and knowingly so - it is the formula
    // fitted on Skyrim's control points, applied to Solstheim's own terrain and
    // snow, with no residuals, because a control point in Skyrim says nothing
    // about a coordinate in Solstheim. A shape rather than a flat number, which
    // is better than what it replaces and worse than the points it awaits.
    enum class World
    {
        Tamriel,
        Solstheim
    };

    // Read Data/SKSE/Plugins/_RSL_Climate.bin. Once, at startup. Returns false
    // and says why in the log if it is missing or malformed - the mod then
    // falls back to the region model, so a missing file is a worse climate
    // rather than no mod.
    bool Load();

    [[nodiscard]] bool Ready();

    // The surface temperature at a point, or nothing where that worldspace's
    // grid has no terrain there - the border, the open sea.
    //
    // This is the temperature OF THE GROUND THERE, before weather, the hour,
    // fires, or the air above it.
    [[nodiscard]] std::optional<float> Surface(World a_world, float a_x, float a_y);

    // How many degrees are left of that once the player is up in the air.
    //
    // Near the ground the temperature is the ground's - that is what a surface
    // layer is, and it is why the floor of the College of Winterhold is as warm
    // as the town below it rather than three thousand units colder. Above the
    // layer the air takes over and everything converges on one number, so that
    // a Dovahkiin lifted into the sky is in the sky wherever he was standing.
    //
    // a_ground is the terrain under the player, a_z the player's own height.
    [[nodiscard]] float WithAltitude(float a_surface, float a_ground, float a_z);
}
