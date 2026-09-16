#pragma once

#include "Settings.h"

// The approved bar design, in stage units (the fixed 1280x720 basis).
//
// These numbers are the same ones tools/widget_design.py renders, and that
// script is the reference: if the two disagree, the script is right and this
// header is stale. Keeping them side by side is deliberate - the design was
// settled by looking at PNGs, and being able to re-render the exact thing the
// game draws is what made that possible.
//
// Layer order for one bar, bottom to top:
//
//     black plate            the full BAR_W x BAR_H rectangle
//     grey frame             FRAME units, FRAME_GREY
//     axis colour + ramp     the fill, PAD units inside the frame
//     white loss marker      what was just lost
//     threshold notch        at the axis safe mark, black under grey
//
// The corner vignette from the design is not drawn yet. It is a per-pixel
// falloff, which the strip approach cannot express; it wants baked artwork and
// comes after the rest is confirmed working in game.

namespace RSL::Layout
{
    inline constexpr float FRAME = 1.0f;
    inline constexpr float PAD = 1.0f;
    inline constexpr int   ROWS = 3;

    inline constexpr float INNER = FRAME + PAD;

    // Black that follows the contour of whatever it is behind. The baked
    // artwork carries its own, so this is both the width of the band drawn
    // around the plate and the margin already inside those sprites -
    // tools/widget_art.py renders them at art size + 2 * HALO for that reason,
    // and the two numbers have to agree.
    //
    // One, not three. Three read as a moat in game and put the widget out of
    // keeping with RFAB's own icons, which carry a single-pixel outline.
    inline constexpr float HALO = 1.0f;


    // The sizes that are still being dialled in come from Settings rather than
    // from here, so they can be moved in game and written back as defaults
    // afterwards. The defaults in Settings.h are the numbers that used to sit
    // in this file.
    //
    // Geometry() is what the widget compares against to notice a change; the
    // bar block rebuilds when it does.
    struct Geometry
    {
        float barW{ 0.0f };
        float barH{ 0.0f };
        float rowPitch{ 0.0f };
        float icon{ 0.0f };
        float iconGap{ 0.0f };
        float tempSize{ 0.0f };

        // Not a size, but it belongs here for the same reason the sizes do:
        // the plate is drawn once at build time, so a change to it has to be
        // something Geometry notices and rebuilds on.
        float frameMid{ 0.0f };

        [[nodiscard]] float barX() const { return icon + iconGap; }
        [[nodiscard]] float innerW() const { return barW - INNER * 2.0f; }
        [[nodiscard]] float innerH() const { return barH - INNER * 2.0f; }

        [[nodiscard]] bool operator==(const Geometry&) const = default;
    };

    [[nodiscard]] inline Geometry Current()
    {
        return { Settings::fBarWidth, Settings::fBarHeight, Settings::fRowPitch,
                 Settings::fIconSize, Settings::fIconGap, Settings::fTempIconSize,
                 Settings::fFrameMid };
    }

    // The food preview's place on the inventory screen. Settled in game and
    // frozen here rather than left as three debug sliders: it is anchored to
    // the inventory's own layout, not to the player's taste, and the stage it
    // is placed on is a fixed 1280x720 at every resolution.
    inline constexpr float INV_X = 707.0f;
    inline constexpr float INV_Y = 400.0f;
    inline constexpr float INV_SCALE = 1.0f;

    inline constexpr float NOTCH_BLACK_W = 3.0f;
    inline constexpr float NOTCH_GREY_W = 1.0f;

    // How far the ramp pushes each end of the fill away from the axis colour.
    inline constexpr float RAMP_DARK = 0.34f;
    inline constexpr float RAMP_LIGHT = 0.18f;

    inline constexpr std::uint32_t PLATE_BLACK = 0x000000;

    // The frame is not one colour along its length: it is FRAME_GREY at both
    // ends and dimmer towards the middle. Two ramps, meeting at the centre; how
    // far down the middle goes is Settings::fFrameMid, see FrameMid() below.
    //
    // Back to 7a7c7a, where it started. 2d2d2d lost the bar's edge against a
    // dark background and e6e6e6 made the fade to the middle shout; this sits
    // between them and was the right answer all along. Each was looked at in
    // game, which is the only place a colour on a one-pixel line can be judged.
    inline constexpr std::uint32_t FRAME_GREY = 0x7A7C7A;
    inline constexpr std::uint32_t MARKER_WHITE = 0xFFFFFF;

    // Row order top to bottom: sleep, food, cold.
    inline constexpr std::uint32_t COL_SLEEP = 0x2C375A;   // dark, almost venous blue
    inline constexpr std::uint32_t COL_FOOD = 0x54381E;    // dark brown
    inline constexpr std::uint32_t COL_COLD = 0x284A5A;    // dark icy blue
    inline constexpr std::uint32_t COL_DANGER = 0x6E1111;  // blood red

    inline constexpr std::uint32_t AXIS_COLOUR[ROWS] = { COL_SLEEP, COL_FOOD, COL_COLD };

    // The danger colour is not a general low-value state. It belongs to the
    // cold axis alone, at the hypothermia threshold, and it crossfades in
    // rather than snapping.
    inline constexpr int   DANGER_AXIS = 2;
    inline constexpr float DANGER_AT = 0.10f;
    inline constexpr float DANGER_FADE_SECONDS = 0.6f;

    // The loss marker trails the fill and catches up, so the width of the gap
    // reads as how fast the axis is dropping rather than as one tick's delta.
    // A tick moves the bar by a fraction of a pixel, so showing the raw delta
    // would be invisible or would flicker on the pixel floor.
    // The temperature icon crossfades between steps rather than snapping -
    // v0.4.0's TEMP_FADE, in seconds, with the old icon fading out as the new
    // one fades in.
    inline constexpr float TEMP_FADE_SECONDS = 0.5f;

    inline constexpr float MARKER_CATCHUP_PER_SECOND = 0.22f;
    inline constexpr float MARKER_MIN_W = 1.0f;

    inline std::uint32_t Mix(std::uint32_t a_from, std::uint32_t a_to, float a_t)
    {
        const auto chan = [](std::uint32_t c, int shift) {
            return static_cast<float>((c >> shift) & 0xFF);
        };
        const auto mix = [&](int shift) {
            const float v = chan(a_from, shift) + (chan(a_to, shift) - chan(a_from, shift)) * a_t;
            return static_cast<std::uint32_t>(v + 0.5f) & 0xFFu;
        };
        return (mix(16) << 16) | (mix(8) << 8) | mix(0);
    }

    inline std::uint32_t RampLeft(std::uint32_t a_colour) { return Mix(a_colour, 0x000000, RAMP_DARK); }
    inline std::uint32_t RampRight(std::uint32_t a_colour) { return Mix(a_colour, 0xFFFFFF, RAMP_LIGHT); }

    // The colour the frame's gradient meets at, from Geometry::frameMid. It is
    // FRAME_GREY dimmed rather than a grey of its own, so the frame keeps one
    // hue from end to middle however far down the setting is taken.
    inline std::uint32_t FrameMid(float a_frac)
    {
        return Mix(0x000000, FRAME_GREY, std::clamp(a_frac, 0.0f, 1.0f));
    }
}
