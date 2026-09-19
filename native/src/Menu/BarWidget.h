#pragma once

#include "Menu/HudLayout.h"

namespace RSL
{
    // The three needs bars, drawn straight onto the movie from C++.
    //
    // The static parts of a bar - plate, frame, notch - are drawn once when the
    // clips are built. Only the fill and the loss marker are redrawn per frame,
    // and only when their geometry actually changed, so a steady HUD costs
    // nothing beyond the frame it was last touched on.
    class BarWidget
    {
    public:
        struct Axis
        {
            float value{ 1.0f };   // 0..1, what the bar shows

            // Of that value, how much is fast food. Hunger only; the hatch is
            // drawn over the rightmost part of the fill, so this is a share of
            // the SAME number rather than a second bar.
            float fast{ 0.0f };
            float marker{ 1.0f };  // 0..1, trailing edge of the white marker
            float danger{ 0.0f };  // 0..1 crossfade towards the danger colour
            float safe{ 0.75f };   // where this axis stops being safe
            bool  shown{ true };
        };

        // Builds the clip tree under a_root. Safe to call again; it rebuilds.
        bool Build(RE::GFxMovieView* a_movie, RE::GFxValue& a_root);
        void Destroy();

        [[nodiscard]] bool Ready() const { return _built; }

        // The geometry is tunable from the MCM while the game runs, and the
        // clip tree is built against it. Rather than rebuild every frame, the
        // menu asks whether it went stale and rebuilds only then.
        [[nodiscard]] bool GeometryStale() const
        {
            return _built && !(_geo == Layout::Current());
        }

        void SetValue(int a_axis, float a_value);

        // How much of that axis is fast food, 0..1 of the whole bar.
        void SetFast(int a_axis, float a_fast);
        void SetShown(int a_axis, bool a_shown);
        void SetSafe(int a_axis, float a_safe);
        void Advance(float a_deltaSeconds);
        void Redraw();

        void SetPlacement(float a_x, float a_y, float a_scale);

        // How solid the whole thing is, 0..1. The bars, the temperature
        // indicator and the inventory preview together - fHudOpacity says
        // "the widget", and those three are the widget.
        //
        // It is set on the three ROOTS, so everything inside keeps the alpha
        // it was drawn with: a parent's alpha multiplies its children's, so
        // the hatch stays a hatch and a hidden row stays hidden.
        //
        // The slider, its help text and its ini default have all existed
        // since the widget did. Nothing read the value.
        void SetOpacity(float a_opacity);

        // The whole widget, bars and indicator alike. Used to take it off
        // screen while a menu owns the screen.
        void SetVisible(bool a_visible);

        // The temperature indicator: a separate piece with its own position,
        // exactly as v0.4.0 split it out of the bar block. 0 freezing .. 4 warm.
        void SetFeel(int a_feel);

        // The indicator is its own clip on the root, so hiding the bars does
        // not hide it. The master switch has to say so explicitly.
        void SetFeelShown(bool a_shown);
        void SetTempPlacement(float a_x, float a_y, float a_scale);

        // The preview over the inventory: what the highlighted item would
        // do, on two bars.
        //
        // The same bars as the HUD rows - same plate, frame, notch, glyph,
        // built by the same code - with their own placement and one thing
        // added: a translucent segment showing where each would land. They are
        // the one piece of the widget visible only while a menu owns the
        // screen, so their visibility is not part of SetVisible.
        //
        // Two of them because frost resistance moves the cold bar and a meal
        // moves the food bar, and RFAB has dishes that do both. The
        // food bar keeps the anchor it always had; cold sits one row pitch
        // ABOVE it, so nothing the player already placed by hand moves.
        enum InvSlot
        {
            INV_FOOD = 0,
            INV_COLD = 1,
            INV_SLOTS = 2
        };

        void SetInvShown(int a_slot, bool a_shown);
        // a_fast is the fast share the bar has now; a_projectedFast the one it
        // would have after. The second is not a kind but a size, because a
        // meal can take the fast share away as well as add to it.
        void SetInvValue(int a_slot, float a_value, float a_safe, float a_projected,
            float a_fast, float a_projectedFast);
        void SetInvPlacement(float a_x, float a_y, float a_scale);

    private:
        struct Row
        {
            RE::GFxValue clip;
            RE::GFxValue plate;     // black plate, grey frame, notch - drawn once
            RE::GFxValue fill;      // axis colour with its horizontal ramp
            RE::GFxValue hatch;     // fast-food ink, cropped to its share
            RE::GFxValue hatchMask; // what does that cropping
            RE::GFxValue vignette;  // corner falloff, cropped to the fill
            RE::GFxValue vigMask;   // what does the cropping
            RE::GFxValue marker;    // white, what was just lost
            RE::GFxValue icon;      // the axis glyph, left of the bar

            float drawnValue{ -1.0f };
            float drawnFast{ -1.0f };
            float drawnMarker{ -1.0f };
            float drawnDanger{ -1.0f };
            float drawnSafe{ -1.0f };
            int   drawnShown{ -1 };
        };

        bool BuildRow(RE::GFxValue& a_parent, std::string_view a_name, std::int32_t a_depth,
            float a_y, const char* a_icon, Row& a_row);

        void DrawPlate(Row& a_row);
        void DrawFill(Row& a_row, const Axis& a_axis, std::uint32_t a_base);

        void DrawTemp();
        void DrawInv();

        RE::GFxMovieView* _movie{ nullptr };
        RE::GFxValue      _container;
        RE::GFxValue      _temp;

        // Two icon clips, not one: a crossfade needs the outgoing step to still
        // be on screen while the incoming one comes up. They alternate, so the
        // linkage names and depths alternate with them.
        RE::GFxValue _tempIcon;
        RE::GFxValue _tempIconOut;
        float        _tempFade{ 1.0f };   // 1 = settled
        int          _tempSlot{ 0 };

        Layout::Geometry _geo{};
        bool             _visible{ true };

        // Last applied, as alpha 0..100. Negative means "not yet", so the
        // first pass after a build always writes it.
        float _opacity{ -1.0f };

        int _feel{ 2 };
        int _drawnFeel{ -1 };
        Row          _rows[Layout::ROWS];
        Axis         _axes[Layout::ROWS];

        // The inventory preview. Its own clip on the root rather than a child
        // of the bar block, for the same reason the temperature icon is: it
        // has its own position, and nesting it would drag it around.
        RE::GFxValue _invRoot;
        Row          _inv[INV_SLOTS];
        Axis         _invAxis[INV_SLOTS];
        float        _invProjected[INV_SLOTS]{ -1.0f, -1.0f };
        float        _invProjectedFast[INV_SLOTS]{ 0.0f, 0.0f };
        bool         _invShown[INV_SLOTS]{ false, false };
        float        _drawnInvProjected[INV_SLOTS]{ -2.0f, -2.0f };
        float        _drawnInvProjectedFast[INV_SLOTS]{ -2.0f, -2.0f };
        int          _drawnInvShown[INV_SLOTS]{ -1, -1 };

        bool         _built{ false };
    };
}
