#!/usr/bin/env python3
"""Render the widget artwork that has to arrive as pixels.

Everything the Scaleform drawing API can state exactly - rectangles, the frame,
the notch, the horizontal ramp - is still drawn from C++. This produces only the
things it cannot:

    vignette.png   the corner falloff. It is per-pixel, and the strip
                   approximation the drawing API would need looked like banding.

The vignette is rendered at the size of a FULL fill and attached at that size;
the plugin then masks it down to the current fill. That is the same rule the
design reference follows - built for the full bar, cropped, never stretched - so
the corners keep a constant size as the value moves.

widget_design.py stays the single source of the falloff itself: this imports
vignette_layer from it rather than restating the maths.

Usage:
    python tools/make_widget_assets.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import widget_art as art  # noqa: E402
import widget_design as design  # noqa: E402

OUT = ROOT / "widget" / "generated"

# Rendered above its on-screen size so Scaleform scales down rather than up.
SUPERSAMPLE = 4

# The axis glyph's size on screen. Settings.h calls it fIconSize, and this has
# to track it - not for resolution, which the supersampling covers, but for the
# RATIO of glyph to halo baked into the sprite. The C++ scales the whole sprite
# to fIconSize + 2 * HALO, so a glyph baked at 21 against a halo of 1 comes out
# slightly small when that is squeezed into 19 + 2.
ICON_ON_SCREEN = 19


def bar_on_screen():
    """The bar's real size, read off Settings.h rather than restated here.

    widget_design still carries 249x14 from the pass that matched TrueHUD, and
    the bar has been 150x13 for a while. The vignette does not mind - it is a
    falloff and survives being stretched - but the hatch is one-unit lines, and
    squeezing 245 columns into 146 turns a diagonal into a grey smear. So this
    one asset is baked at the size it will actually be drawn at, and the size
    comes from the file that owns it.
    """
    text = (ROOT / "native" / "src" / "Settings.h").read_text(encoding="utf-8")
    want = {}
    for name in ("fBarWidth", "fBarHeight", "fFrameMid"):
        m = re.search(r"static inline float\s+" + name + r"\{\s*([0-9.]+)f?\s*\}", text)
        if not m:
            raise SystemExit(f"{name} not found in Settings.h")
        want[name] = float(m.group(1))
    return int(want["fBarWidth"]), int(want["fBarHeight"])


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    inner_w = design.BAR_W - (design.FRAME + design.PAD) * 2
    inner_h = design.BAR_H - (design.FRAME + design.PAD) * 2

    # The fast-food hatch, at EXACTLY its on-screen size. Unlike the vignette
    # this is not supersampled: a one-unit line is the whole point, and
    # resampling a one-pixel diagonal turns it into a grey smear.
    bar_w, bar_h = bar_on_screen()
    hatch_w = bar_w - (design.FRAME + design.PAD) * 2
    hatch_h = bar_h - (design.FRAME + design.PAD) * 2
    strip = art.hatch(hatch_w, hatch_h)
    path = OUT / "hatch.png"
    strip.save(path)
    print(f"  {path.relative_to(ROOT)}  {strip.size[0]}x{strip.size[1]}"
          f"  (1:1 with the fill of a {bar_w}x{bar_h} bar, step {art.HATCH_STEP})")

    vignette = design.vignette_layer((inner_w * SUPERSAMPLE, inner_h * SUPERSAMPLE))
    path = OUT / "vignette.png"
    vignette.save(path)
    print(f"  {path.relative_to(ROOT)}  {vignette.size[0]}x{vignette.size[1]}"
          f"  (fill is {inner_w}x{inner_h} on screen)")

    # The axis glyphs, recoloured and given the black contour that follows their
    # own outline. Named apart from the raw sources in widget/icons so both can
    # sit in the SWF without one linkage name shadowing the other.
    icons = ROOT / "widget" / "icons"
    for name in ("sleep", "food", "cold"):
        glyph = art.axis_glyph(icons / f"{name}.png",
                               ICON_ON_SCREEN * SUPERSAMPLE,
                               art.HALO * SUPERSAMPLE)
        path = OUT / f"axis_{name}.png"
        glyph.save(path)
        print(f"  {path.relative_to(ROOT)}  {glyph.size[0]}x{glyph.size[1]}"
              f"  (glyph is {ICON_ON_SCREEN} on screen)")

    # The knot caps are NOT baked. They were drawn, rendered, tuned over several
    # passes and then cut: in game they read as an ornament stuck beside the bar
    # rather than as part of it, and no amount of overlap or layering fixed
    # that. The shapes stay in widget_art and the renders stay in widget/design,
    # because the next idea for the ends will want to start from them rather
    # than from nothing - but nothing about them ships.
    for stale in ("knotl.png", "knotr.png"):
        path = OUT / stale
        if path.exists():
            path.unlink()
            print(f"  removed {path.relative_to(ROOT)} (knots are not shipped)")


if __name__ == "__main__":
    main()
