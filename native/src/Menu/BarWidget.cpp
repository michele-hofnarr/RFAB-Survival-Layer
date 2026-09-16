#include "PCH.h"

#include "Menu/BarWidget.h"

#include "Core/Needs.h"
#include "Menu/Draw.h"

namespace RSL
{
    using namespace Layout;

    bool BarWidget::Build(RE::GFxMovieView* a_movie, RE::GFxValue& a_root)
    {
        Destroy();
        _movie = a_movie;

        // Depths well above anything the root timeline uses. The stage places
        // a sprite at depth 1 and has a single frame, so it replays that frame
        // forever; clips created down in the timeline's own depth range are
        // liable to be swept away by the replay.
        if (!Draw::CreateClip(a_root, "rslBars", 20000, _container)) {
            logger::error("could not create the bar container");
            return false;
        }

        _geo = Current();
        const auto& g = _geo;

        static constexpr const char* ICON_NAME[ROWS] = {
            "ico_axis_sleep", "ico_axis_food", "ico_axis_cold"
        };

        for (int i = 0; i < ROWS; ++i) {
            const auto name = std::format("row{}", i);
            if (!BuildRow(_container, name, 100 + i, static_cast<float>(i) * g.rowPitch,
                    ICON_NAME[i], _rows[i])) {
                Destroy();
                return false;
            }
        }

        // The inventory preview: the hunger and cold rows again, built by the
        // same code, on their own clip so they can be placed on their own.
        //
        // Food keeps y = 0, which is where the single preview always sat, and
        // cold goes one pitch above it. Anyone who has already nudged INV_Y to
        // taste keeps their position.
        if (!Draw::CreateClip(a_root, "rslInv", 20200, _invRoot)) {
            logger::error("could not create the inventory preview");
            Destroy();
            return false;
        }
        {
            static constexpr int   AXIS_OF[INV_SLOTS] = { 1, 2 };
            static constexpr float ROW_Y[INV_SLOTS] = { 0.0f, -1.0f };
            for (int i = 0; i < INV_SLOTS; ++i) {
                if (!BuildRow(_invRoot, std::format("row{}", i), 100 + i,
                        ROW_Y[i] * g.rowPitch, ICON_NAME[AXIS_OF[i]], _inv[i])) {
                    logger::error("could not create inventory preview row {}", i);
                    Destroy();
                    return false;
                }
                Draw::SetAlpha(_inv[i].clip, 0.0f);
                _drawnInvShown[i] = -1;
            }
        }
        Draw::SetVisible(_invRoot, false);

        // Its own clip on the root, not a child of the bar block: it has its
        // own position, and nesting it would drag it around with the bars.
        if (!Draw::CreateClip(a_root, "rslTemp", 20100, _temp)) {
            logger::error("could not create the temperature indicator");
            Destroy();
            return false;
        }
        _drawnFeel = -1;

        _built = true;
        logger::info("bar widget built");
        return true;
    }

    // One bar: the clip tree and the parts that are drawn once.
    bool BarWidget::BuildRow(RE::GFxValue& a_parent, std::string_view a_name,
        std::int32_t a_depth, float a_y, const char* a_icon, Row& a_row)
    {
        const auto& g = _geo;

        if (!Draw::CreateClip(a_parent, a_name, a_depth, a_row.clip)) {
            logger::error("could not create {}", a_name);
            return false;
        }
        Draw::SetPosition(a_row.clip, g.barX(), a_y);

        // Depth order inside a row is the layer order of the design: plate
        // first, then the fill over it, then the marker over that. The notch
        // lives on the plate but is drawn last within it, so it stays readable
        // at any fill.
        if (!Draw::CreateClip(a_row.clip, "plate", 1, a_row.plate) ||
            !Draw::CreateClip(a_row.clip, "fill", 2, a_row.fill) ||
            !Draw::CreateClip(a_row.clip, "hatch", 3, a_row.hatch) ||
            !Draw::CreateClip(a_row.clip, "hatchmask", 4, a_row.hatchMask) ||
            !Draw::CreateClip(a_row.clip, "vig", 5, a_row.vignette) ||
            !Draw::CreateClip(a_row.clip, "vigmask", 6, a_row.vigMask) ||
            !Draw::CreateClip(a_row.clip, "marker", 7, a_row.marker)) {
            logger::error("could not create the layers of {}", a_name);
            return false;
        }

        // The fast-food hatch: one strip the width of a FULL fill, attached at
        // that size and then cropped by a mask - never rebuilt at the width of
        // the share it covers. That is what holds the pattern still while the
        // bar drains; a pattern laid out per segment slides as the boundary
        // moves, which is what the first design sheet accidentally showed.
        RE::GFxValue ink;
        if (Draw::AttachSprite(a_row.hatch, "ico_hatch", "art", 1, ink)) {
            Draw::SetPosition(ink, INNER, INNER);
            Draw::SetSize(ink, g.innerW(), g.innerH());
            Draw::SetMask(a_row.hatch, a_row.hatchMask);
        }

        // The vignette is one bitmap the size of a FULL fill, cropped by a mask
        // rather than scaled. That is the rule the design reference follows:
        // scaling it would stretch and squash the corners as the value moves,
        // and the whole point of them is a constant size.
        RE::GFxValue art;
        if (Draw::AttachSprite(a_row.vignette, "ico_vignette", "art", 1, art)) {
            Draw::SetPosition(art, INNER, INNER);
            Draw::SetSize(art, g.innerW(), g.innerH());
            Draw::SetMask(a_row.vignette, a_row.vigMask);
        }

        // The axis glyph, left of the bar and vertically centred on it.
        //
        // The sprite carries its own black contour, baked at art size plus a
        // halo on every side, so what is attached is wider than the glyph by
        // exactly that much and has to be sized and placed for the whole sprite
        // rather than for the glyph alone.
        if (Draw::CreateClip(a_row.clip, "icon", 8, a_row.icon)) {
            RE::GFxValue glyph;
            if (Draw::AttachSprite(a_row.icon, a_icon, "art", 1, glyph)) {
                const float box = g.icon + HALO * 2.0f;
                Draw::SetSize(glyph, box, box);
                Draw::SetPosition(a_row.icon, -g.barX() - HALO, (g.barH - box) * 0.5f);
            }
        }

        DrawPlate(a_row);
        return true;
    }

    void BarWidget::Destroy()
    {
        // Dropping our handles is enough; the clips go with the movie.
        for (auto& row : _rows) {
            row = Row{};
        }
        for (int i = 0; i < INV_SLOTS; ++i) {
            _inv[i] = Row{};
            _drawnInvShown[i] = -1;
            _drawnInvProjected[i] = -2.0f;
            _drawnInvProjectedFast[i] = -1;
        }
        _invRoot = RE::GFxValue{};
        _container = RE::GFxValue{};
        _temp = RE::GFxValue{};
        _movie = nullptr;
        _drawnFeel = -1;
        _built = false;
    }

    void BarWidget::SetValue(int a_axis, float a_value)
    {
        if (a_axis < 0 || a_axis >= ROWS) {
            return;
        }
        auto& axis = _axes[a_axis];
        axis.value = std::clamp(a_value, 0.0f, 1.0f);

        // A rise takes the marker with it immediately - the marker only ever
        // shows loss, so there is nothing to trail behind a gain.
        if (axis.marker < axis.value) {
            axis.marker = axis.value;
        }
    }

    void BarWidget::SetFast(int a_axis, float a_fast)
    {
        if (a_axis >= 0 && a_axis < ROWS) {
            _axes[a_axis].fast = std::clamp(a_fast, 0.0f, 1.0f);
        }
    }

    void BarWidget::SetShown(int a_axis, bool a_shown)
    {
        if (a_axis >= 0 && a_axis < ROWS) {
            _axes[a_axis].shown = a_shown;
        }
    }

    void BarWidget::SetSafe(int a_axis, float a_safe)
    {
        if (a_axis >= 0 && a_axis < ROWS) {
            _axes[a_axis].safe = std::clamp(a_safe, 0.0f, 1.0f);
        }
    }

    void BarWidget::Advance(float a_dt)
    {
        if (a_dt <= 0.0f) {
            return;
        }

        // The temperature icon's crossfade. Two clips, opposite alphas.
        if (_tempFade < 1.0f) {
            _tempFade = std::min(1.0f, _tempFade + a_dt / TEMP_FADE_SECONDS);
            if (_tempIcon.IsDisplayObject()) {
                Draw::SetAlpha(_tempIcon, _tempFade * 100.0f);
            }
            if (_tempIconOut.IsDisplayObject()) {
                Draw::SetAlpha(_tempIconOut, (1.0f - _tempFade) * 100.0f);
                if (_tempFade >= 1.0f) {
                    Draw::SetVisible(_tempIconOut, false);
                }
            }
        }

        for (int i = 0; i < ROWS; ++i) {
            auto& axis = _axes[i];

            // The marker eases towards the value instead of snapping, so the
            // gap between them is a readout of how fast the axis is falling.
            // Exponential decay keeps that independent of frame rate.
            // fMarkerLinger stretches the tail: the white bar is the only
            // readout of HOW FAST an axis is falling, and at the shipped rate it
            // was gone before the eye found it. 1 is the old speed, 2 makes it
            // last twice as long.
            const float k = 1.0f - std::exp(
                -(MARKER_CATCHUP_PER_SECOND / std::max(0.1f, Settings::fMarkerLinger)) *
                a_dt * 60.0f);
            axis.marker += (axis.value - axis.marker) * k;
            if (axis.marker - axis.value < 1e-4f) {
                axis.marker = axis.value;
            }

            // THE RED READS THE RESERVE, NOT THE BAR. On the cold axis the
            // hatched part is bought time, and nothing downstream counts it:
            // penalties, hypothermia and the rest all read the reserve
            // underneath. A bar held up by a draught would otherwise sit calm
            // and blue while the player was taking the full cold penalty.
            //
            // value - fast IS that reserve: the bar is base + bought and the
            // hatch is the bought half. Only the danger axis needs it - on the
            // food bar the fast half feeds you like any other, and it is not
            // the danger axis anyway.
            const float reserve = axis.value - axis.fast;
            const float want = (i == DANGER_AXIS && reserve < DANGER_AT) ? 1.0f : 0.0f;
            const float rate = a_dt / DANGER_FADE_SECONDS;
            axis.danger = std::clamp(axis.danger + (want > axis.danger ? rate : -rate), 0.0f, 1.0f);
        }
    }

    void BarWidget::Redraw()
    {
        if (!_built) {
            return;
        }

        // What the widget is actually doing, at the rate the gameplay pass
        // reports itself.
        //
        // Until now the display side logged nothing at all, and that made "the
        // bars are gone" unanswerable: the needs line proves the model runs and
        // says nothing about whether anything was drawn. Everything that can
        // make a row invisible is here - the container's own visibility, each
        // row's shown flag, and the geometry the rows are laid out with - so a
        // missing bar can be told apart from a bar drawn off screen or at zero
        // width without guessing.
        if (Settings::bDebugLog) {
            static auto last = std::chrono::steady_clock::now() - std::chrono::hours(1);
            const auto  now = std::chrono::steady_clock::now();
            if (now - last >= std::chrono::seconds(5)) {
                last = now;
                logger::info("hud: visible={} mod={} undead={} | bar {:.0f}x{:.0f} "
                             "pitch {:.0f} icon {:.0f}+{:.0f} at {:.0f}/{:.0f} x{:.2f}",
                    _visible, Settings::bModEnabled, Needs::GetSingleton().Undead(),
                    _geo.barW, _geo.barH, _geo.rowPitch, _geo.icon,
                    _geo.iconGap, Settings::fHudX, Settings::fHudY,
                    Settings::fHudScale);
                for (int i = 0; i < ROWS; ++i) {
                    static constexpr const char* NAME[] = { "sleep", "hunger", "cold" };
                    logger::info("  {:<6} shown={} value={:.3f} drawn={:.3f}"
                                 " marker={:.3f} clip={}",
                        NAME[i], _axes[i].shown, _axes[i].value, _rows[i].drawnValue,
                        _axes[i].marker, !_rows[i].clip.IsUndefined());
                }
            }
        }

        for (int i = 0; i < ROWS; ++i) {
            auto&       row = _rows[i];
            const auto& axis = _axes[i];

            // Redrawing costs a handful of Invokes, so skip a row whose
            // appearance has not moved since the last frame.
            const bool moved =
                std::abs(axis.value - row.drawnValue) > 1e-4f ||
                std::abs(axis.fast - row.drawnFast) > 1e-4f ||
                std::abs(axis.marker - row.drawnMarker) > 1e-4f ||
                std::abs(axis.danger - row.drawnDanger) > 1e-3f ||
                std::abs(axis.safe - row.drawnSafe) > 1e-4f ||
                row.drawnShown != static_cast<int>(axis.shown);
            if (!moved) {
                continue;
            }

            Draw::SetAlpha(row.clip, axis.shown ? 100.0f : 0.0f);
            if (axis.shown) {
                DrawFill(row, axis, AXIS_COLOUR[i]);
            }
            row.drawnValue = axis.value;
            row.drawnFast = axis.fast;
            row.drawnMarker = axis.marker;
            row.drawnDanger = axis.danger;
            row.drawnSafe = axis.safe;
            row.drawnShown = static_cast<int>(axis.shown);
        }

        DrawTemp();
        DrawInv();
    }

    void BarWidget::DrawPlate(Row& a_row)
    {
        Draw::Clear(a_row.plate);

        const auto& g = _geo;

        // Black beyond the plate on every side, so the widget reads against a
        // snowfield as well as against a cave wall.
        Draw::Rect(a_row.plate, -HALO, -HALO, g.barW + HALO * 2, g.barH + HALO * 2,
            { PLATE_BLACK });
        Draw::Rect(a_row.plate, 0.0f, 0.0f, g.barW, g.barH, { PLATE_BLACK });

        // Frame as four bands rather than a stroked outline: lineStyle strokes
        // straddle the path, which puts half of the frame outside the plate.
        //
        // The long bands carry a gradient - 7a7c7a at both ends, dimmer towards
        // the middle - which takes two ramps each, because a ramp has two stops
        // and this has three. A failed ramp falls back to flat grey rather than
        // to nothing, so the frame is always there.
        const std::uint32_t mid = FrameMid(g.frameMid);
        const float half = g.barW * 0.5f;
        for (const float y : { 0.0f, g.barH - FRAME }) {
            if (!Draw::HorizontalRamp(_movie, a_row.plate, 0.0f, y, half, FRAME,
                    FRAME_GREY, mid) ||
                !Draw::HorizontalRamp(_movie, a_row.plate, half, y, half, FRAME,
                    mid, FRAME_GREY)) {
                Draw::Rect(a_row.plate, 0.0f, y, g.barW, FRAME, { FRAME_GREY });
            }
        }
        Draw::Rect(a_row.plate, 0.0f, 0.0f, FRAME, g.barH, { FRAME_GREY });
        Draw::Rect(a_row.plate, g.barW - FRAME, 0.0f, FRAME, g.barH, { FRAME_GREY });
    }

    void BarWidget::DrawFill(Row& a_row, const Axis& a_axis, std::uint32_t a_base)
    {
        const auto& axis = a_axis;

        const std::uint32_t colour =
            axis.danger > 0.0f ? Mix(a_base, COL_DANGER, axis.danger) : a_base;

        Draw::Clear(a_row.fill);
        Draw::Clear(a_row.marker);

        const auto& g = _geo;
        const float fw = g.innerW() * axis.value;
        if (fw > 0.0f) {
            // Fall back to a flat fill if the gradient cannot be built, so a
            // failure shows up as a plain bar rather than as no bar at all.
            if (!Draw::HorizontalRamp(_movie, a_row.fill, INNER, INNER, fw, g.innerH(),
                    RampLeft(colour), RampRight(colour))) {
                Draw::Rect(a_row.fill, INNER, INNER, fw, g.innerH(), { colour });
            }
        }

        // Crop the vignette to the fill. Nothing is redrawn - only the mask
        // changes - so the corners stay exactly the size they were authored.
        Draw::Clear(a_row.vigMask);
        if (fw > 0.0f) {
            Draw::Rect(a_row.vigMask, INNER, INNER, fw, g.innerH(), { MARKER_WHITE });
        }

        // ...and crop the hatch to the fast-food share, which sits at the RIGHT
        // of the fill: the last thing eaten, and the first to go. Same rule as
        // the vignette - the mask moves, the artwork does not.
        Draw::Clear(a_row.hatchMask);
        const float fastW = g.innerW() * std::min(axis.fast, axis.value);
        if (fastW > 0.0f) {
            Draw::Rect(a_row.hatchMask, INNER + fw - fastW, INNER, fastW, g.innerH(),
                { MARKER_WHITE });
        }

        // The marker sits between the current value and where the bar was, so
        // it grows with the rate of loss and shrinks as the bar catches up.
        const float mw = g.innerW() * (axis.marker - axis.value);
        if (mw > 0.01f) {
            Draw::Rect(a_row.marker, INNER + fw, INNER,
                std::max(mw, MARKER_MIN_W), g.innerH(), { MARKER_WHITE });
        }

        // The notch marks where this axis stops being safe, and that differs
        // per axis: v0.4.0's grace values are 16 of 48 for sleep, 12 of 48 for
        // hunger and 25 of 100 for cold. Only the last two land on 75%.
        const float nx = INNER + g.innerW() * axis.safe;
        Draw::Rect(a_row.fill, nx - NOTCH_BLACK_W * 0.5f, INNER, NOTCH_BLACK_W, g.innerH(),
            { PLATE_BLACK });
        Draw::Rect(a_row.fill, nx - NOTCH_GREY_W * 0.5f, INNER, NOTCH_GREY_W, g.innerH(),
            { FRAME_GREY });
    }

    void BarWidget::SetInvShown(int a_slot, bool a_shown)
    {
        if (a_slot >= 0 && a_slot < INV_SLOTS) {
            _invShown[a_slot] = a_shown;
        }
    }

    void BarWidget::SetInvValue(int a_slot, float a_value, float a_safe, float a_projected,
        float a_fast, bool a_projectedFast)
    {
        if (a_slot < 0 || a_slot >= INV_SLOTS) {
            return;
        }
        auto& axis = _invAxis[a_slot];
        axis.value = std::clamp(a_value, 0.0f, 1.0f);
        axis.safe = std::clamp(a_safe, 0.0f, 1.0f);
        axis.fast = std::clamp(a_fast, 0.0f, 1.0f);
        _invProjectedFast[a_slot] = a_projectedFast;

        // No loss marker on a preview: the marker means "this much just went",
        // and nothing has gone. Keeping it level with the value is what stops
        // DrawFill from drawing one.
        axis.marker = axis.value;
        _invProjected[a_slot] = a_projected;
    }

    void BarWidget::SetInvPlacement(float a_x, float a_y, float a_scale)
    {
        if (_built) {
            Draw::SetPosition(_invRoot, a_x, a_y, a_scale);
        }
    }

    void BarWidget::DrawInv()
    {
        if (!_built) {
            return;
        }

        static constexpr std::uint32_t SLOT_COLOUR[INV_SLOTS] = { COL_FOOD, COL_COLD };

        bool anyShown = false;
        for (int i = 0; i < INV_SLOTS; ++i) {
            anyShown = anyShown || _invShown[i];
        }
        Draw::SetVisible(_invRoot, anyShown);

        for (int i = 0; i < INV_SLOTS; ++i) {
            auto&       row = _inv[i];
            const auto& axis = _invAxis[i];

            const bool moved =
                _drawnInvShown[i] != static_cast<int>(_invShown[i]) ||
                std::abs(axis.value - row.drawnValue) > 1e-4f ||
                std::abs(axis.fast - row.drawnFast) > 1e-4f ||
                std::abs(axis.safe - row.drawnSafe) > 1e-4f ||
                std::abs(_invProjected[i] - _drawnInvProjected[i]) > 1e-4f ||
                _drawnInvProjectedFast[i] != static_cast<int>(_invProjectedFast[i]);
            if (!moved) {
                continue;
            }

            Draw::SetAlpha(row.clip, _invShown[i] ? 100.0f : 0.0f);
            _drawnInvShown[i] = static_cast<int>(_invShown[i]);
            row.drawnValue = axis.value;
            row.drawnFast = axis.fast;
            row.drawnSafe = axis.safe;
            _drawnInvProjected[i] = _invProjected[i];
            _drawnInvProjectedFast[i] = static_cast<int>(_invProjectedFast[i]);

            if (!_invShown[i]) {
                continue;
            }

            DrawFill(row, axis, SLOT_COLOUR[i]);

            // What the highlighted item would do. A negative projection means
            // it would do nothing to THIS axis - a drink on the food bar, an
            // apple on the cold bar - and then the bar is just the bar.
            if (_invProjected[i] < 0.0f) {
                continue;
            }

            // Always a gain, never a loss: the preview promises what the item
            // is worth and says nothing about whether it will stay down, so the
            // projection is never below the current value and there is no
            // second branch here to leave untested.
            //
            // The axis colour as it is, at half alpha, over the empty part of
            // the bar. Not lightened and not darkened - the colour is what says
            // which axis, and the transparency is what says "not yet".
            const auto& g = _geo;
            const float here = INNER + g.innerW() * axis.value;
            const float there = INNER + g.innerW() * _invProjected[i];
            Draw::Rect(row.fill, here, INNER, there - here, g.innerH(),
                { SLOT_COLOUR[i], 50.0f });

            // Hatch the promise too when what it offers is not the lasting kind
            // - fast food on the one bar, bought time on the other - so the bar
            // says which KIND of fullness it is offering and not only how much.
            // The mask takes in the projected part on top of whatever share of
            // the current fill is already hatched.
            if (_invProjectedFast[i] && there > here) {
                Draw::Rect(row.hatchMask, here, INNER, there - here, g.innerH(),
                    { MARKER_WHITE });
            }
        }
    }

    void BarWidget::SetFeel(int a_feel)
    {
        _feel = std::clamp(a_feel, 0, 4);
    }

    void BarWidget::SetFeelShown(bool a_shown)
    {
        if (_built) {
            Draw::SetVisible(_temp, a_shown);
        }
    }

    void BarWidget::SetTempPlacement(float a_x, float a_y, float a_scale)
    {
        if (_built) {
            Draw::SetPosition(_temp, a_x, a_y, a_scale);
        }
    }

    void BarWidget::DrawTemp()
    {
        if (_feel == _drawnFeel) {
            return;
        }
        _drawnFeel = _feel;

        // v0.4.0's five sliced icons, now that the stage carries bitmaps.
        // Re-attached rather than kept and re-pointed: a sprite is bound to its
        // character, so showing a different step means a different sprite.
        static constexpr const char* FEEL_ICON[5] = {
            "ico_temp0",   // freezing
            "ico_temp1",
            "ico_temp2",   // neutral
            "ico_temp3",
            "ico_temp4",   // warm
        };

        const int feel = std::clamp(_feel, 0, 4);

        // The one that was showing becomes the one fading out, and the new step
        // arrives at zero alpha into the other slot. Attaching over a clip with
        // the same name and depth would replace it, so the two alternate.
        _tempIconOut = _tempIcon;
        _tempSlot = 1 - _tempSlot;

        const char* const  slotName = _tempSlot == 0 ? "artA" : "artB";
        const std::int32_t slotDepth = _tempSlot == 0 ? 1 : 2;

        if (!Draw::AttachSprite(_temp, FEEL_ICON[feel], slotName, slotDepth, _tempIcon)) {
            return;
        }
        Draw::SetSize(_tempIcon, _geo.tempSize, _geo.tempSize);

        // The very first icon has nothing to fade from, so it simply appears.
        if (_tempIconOut.IsDisplayObject()) {
            Draw::SetVisible(_tempIconOut, true);
            Draw::SetAlpha(_tempIcon, 0.0f);
            _tempFade = 0.0f;
        } else {
            Draw::SetAlpha(_tempIcon, 100.0f);
            _tempFade = 1.0f;
        }
    }

    void BarWidget::SetVisible(bool a_visible)
    {
        if (!_built || _visible == a_visible) {
            return;
        }
        _visible = a_visible;
        Draw::SetVisible(_container, a_visible);
        Draw::SetVisible(_temp, a_visible);
    }

    void BarWidget::SetPlacement(float a_x, float a_y, float a_scale)
    {
        if (_built) {
            Draw::SetPosition(_container, a_x, a_y, a_scale);
        }
    }
}
