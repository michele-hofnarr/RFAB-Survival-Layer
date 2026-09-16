#pragma once

// Thin wrappers over the Scaleform drawing API.
//
// createEmptyMovieClip, beginFill, moveTo/lineTo and clear are MovieClip
// built-ins, so they are available on a movie that contains no ActionScript of
// its own. That is what lets the HUD ship as a 30-byte empty stage instead of
// an authored SWF: there is nothing to author, because the plugin draws it.
//
// Everything here is UI-thread only. GFxValue must never be touched from a game
// or event thread - use RSLMenu::AddTask to get across.

namespace RSL::Draw
{
    // A colour as Scaleform wants it: 0xRRGGBB, with alpha given separately as
    // a percentage, which is what beginFill expects.
    struct Colour
    {
        std::uint32_t rgb{ 0 };
        float         alpha{ 100.0f };
    };

    inline RE::GFxValue Num(double a_v)
    {
        RE::GFxValue v;
        v.SetNumber(a_v);
        return v;
    }

    inline bool CreateClip(RE::GFxValue& a_parent, std::string_view a_name, std::int32_t a_depth,
        RE::GFxValue& a_out)
    {
        RE::GFxValue args[2];
        args[0].SetString(a_name);
        args[1].SetNumber(a_depth);
        return a_parent.Invoke("createEmptyMovieClip", &a_out, args, 2) && a_out.IsDisplayObject();
    }

    // Attach one of the sprites the stage SWF exports by linkage name.
    //
    // Everything the drawing API can state exactly is still drawn; this is the
    // way in for the two things it cannot - the icons and the corner vignette.
    // tools/make_assets_swf.py bakes each PNG in as DefineSprite + ExportAssets
    // under "ico_<stem>", which is what attachMovie looks up.
    inline bool AttachSprite(RE::GFxValue& a_parent, std::string_view a_linkage,
        std::string_view a_name, std::int32_t a_depth, RE::GFxValue& a_out)
    {
        RE::GFxValue args[3];
        args[0].SetString(a_linkage);
        args[1].SetString(a_name);
        args[2].SetNumber(a_depth);
        if (!a_parent.Invoke("attachMovie", &a_out, args, 3) || !a_out.IsDisplayObject()) {
            logger::error("attachMovie failed for {}", a_linkage);
            return false;
        }
        return true;
    }

    // Stretch a clip so its artwork covers exactly this many stage units. The
    // bitmaps are authored larger than they are shown, so this always scales
    // down, which is the direction that stays sharp.
    inline void SetSize(RE::GFxValue& a_clip, float a_w, float a_h)
    {
        a_clip.SetMember("_width", Num(a_w));
        a_clip.SetMember("_height", Num(a_h));
    }

    inline void SetVisible(RE::GFxValue& a_clip, bool a_visible)
    {
        RE::GFxValue v;
        v.SetBoolean(a_visible);
        a_clip.SetMember("_visible", v);
    }

    // Confine a_clip to the area drawn in a_mask. Used for the vignette, which
    // is built at the size of a full fill and then cropped rather than scaled,
    // so its corners keep a constant size as the value moves.
    inline void SetMask(RE::GFxValue& a_clip, RE::GFxValue& a_mask)
    {
        RE::GFxValue arg = a_mask;
        a_clip.Invoke("setMask", nullptr, &arg, 1);
    }

    inline void Clear(RE::GFxValue& a_clip)
    {
        a_clip.Invoke("clear");
    }

    // One axis-aligned rectangle. Four lineTo calls rather than a helper,
    // because the drawing API has no rectangle primitive.
    inline void Rect(RE::GFxValue& a_clip, float a_x, float a_y, float a_w, float a_h,
        const Colour& a_colour)
    {
        if (a_w <= 0.0f || a_h <= 0.0f) {
            return;
        }

        RE::GFxValue fill[2];
        fill[0].SetNumber(a_colour.rgb);
        fill[1].SetNumber(a_colour.alpha);
        a_clip.Invoke("beginFill", nullptr, fill, 2);

        const float x1 = a_x + a_w;
        const float y1 = a_y + a_h;
        RE::GFxValue pt[2];

        pt[0] = Num(a_x); pt[1] = Num(a_y);
        a_clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0] = Num(x1);  pt[1] = Num(a_y);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(x1);  pt[1] = Num(y1);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(a_x); pt[1] = Num(y1);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(a_x); pt[1] = Num(a_y);
        a_clip.Invoke("lineTo", nullptr, pt, 2);

        a_clip.Invoke("endFill");
    }

    // A real linear gradient.
    //
    // The first version approximated this with two dozen vertical strips, on
    // the theory that building a matrix object and three arrays per call was
    // not worth it. In a still frame the banding was invisible; in motion it
    // was awful - the strips are sized from the fill, so every one of them
    // stretched and squashed as the bar moved and the whole thing read as a
    // concertina. beginGradientFill costs a handful of marshalled values once
    // per redraw and has none of that.
    inline bool HorizontalRamp(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
        float a_x, float a_y, float a_w, float a_h,
        std::uint32_t a_left, std::uint32_t a_right)
    {
        if (!a_movie || a_w <= 0.0f || a_h <= 0.0f) {
            return false;
        }

        RE::GFxValue colours, alphas, ratios, matrix;
        a_movie->CreateArray(&colours);
        a_movie->CreateArray(&alphas);
        a_movie->CreateArray(&ratios);
        a_movie->CreateObject(&matrix);
        if (!colours.IsArray() || !alphas.IsArray() || !ratios.IsArray() || !matrix.IsObject()) {
            return false;
        }

        colours.PushBack(Num(a_left));
        colours.PushBack(Num(a_right));
        alphas.PushBack(Num(100));
        alphas.PushBack(Num(100));
        ratios.PushBack(Num(0));
        ratios.PushBack(Num(255));

        // The "box" matrix form: a gradient laid across the given rectangle,
        // which saves working out the 1/1638.4 scale factor the raw a/b/c/d
        // form needs.
        RE::GFxValue kind;
        kind.SetString("box");
        matrix.SetMember("matrixType", kind);
        matrix.SetMember("x", Num(a_x));
        matrix.SetMember("y", Num(a_y));
        matrix.SetMember("w", Num(a_w));
        matrix.SetMember("h", Num(a_h));
        matrix.SetMember("r", Num(0));

        RE::GFxValue args[5];
        args[0].SetString("linear");
        args[1] = colours;
        args[2] = alphas;
        args[3] = ratios;
        args[4] = matrix;
        a_clip.Invoke("beginGradientFill", nullptr, args, 5);

        const float x1 = a_x + a_w;
        const float y1 = a_y + a_h;
        RE::GFxValue pt[2];
        pt[0] = Num(a_x); pt[1] = Num(a_y);
        a_clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0] = Num(x1);  pt[1] = Num(a_y);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(x1);  pt[1] = Num(y1);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(a_x); pt[1] = Num(y1);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0] = Num(a_x); pt[1] = Num(a_y);
        a_clip.Invoke("lineTo", nullptr, pt, 2);
        a_clip.Invoke("endFill");
        return true;
    }

    inline void SetPosition(RE::GFxValue& a_clip, float a_x, float a_y, float a_scale = 1.0f)
    {
        RE::GFxValue::DisplayInfo info;
        info.SetPosition(a_x, a_y);
        info.SetScale(a_scale * 100.0f, a_scale * 100.0f);
        a_clip.SetDisplayInfo(info);
    }

    inline void SetAlpha(RE::GFxValue& a_clip, float a_alpha)
    {
        RE::GFxValue::DisplayInfo info;
        info.SetAlpha(a_alpha);
        a_clip.SetDisplayInfo(info);
    }
}
