#!/usr/bin/env python3
"""The approved widget artwork, as one implementation.

Settled in the design pass and rendered by tools/widget_caps_preview.py before
being accepted; this is where the shapes live now so the asset generator and
any future preview draw the same thing rather than two things that agree for a
while.

What is baked and what is drawn:

    baked here      the axis glyphs with their black contour, and the two knot
                    caps - anything with a curve or a silhouette in it
    drawn in C++    the plate, the frame, the fill, the notch and the marker -
                    rectangles and gradients, which the Scaleform drawing API
                    states exactly and which have to follow the bar's width

The caps are turned, not mirrored: a quarter clockwise on the left and a quarter
anticlockwise on the right. A triquetra has three-fold symmetry, so a mirror
would give the same knot back, while opposite quarter turns give a pair that
answer each other.
"""
from PIL import Image, ImageDraw, ImageFilter

import math

# --- the approved numbers --------------------------------------------------
GLYPH_RGB = (0xE6, 0xE6, 0xE6)
FRAME_GREY = (0x7A, 0x7C, 0x7A)   # tracks HudLayout::FRAME_GREY
BLACK = (0, 0, 0)

# Black following a contour, in stage pixels. One, not three: three read as a
# moat in game and put the widget out of keeping with RFAB's own icons, which
# carry a single-pixel outline.
HALO = 1
CAP = 20            # the knot's box, square so a quarter turn keeps its size
KNOT_FILL = 0.86    # how much of that box the knot occupies
KNOT_THICK = 1.1    # line weight
# How far the cap sits inside the bar's end border.
#
# It has to clear two things before the ring can touch the frame: the halo baked
# into the sprite, and the gap between the knot's ink and its own box, which is
# (1 - KNOT_FILL) / 2 of the box. Less than that and the knot is a picture
# sitting near the bar rather than the frame carrying on into a knot.
CAP_OVERLAP = 4


def hard_halo(shape, radius):
    """Black following the contour, radius out, with an edge that stops.

    Thresholding the grown alpha is the whole point: without it the glyph's own
    antialiasing is grown along with the shape and the black fades out over
    three pixels as a glow instead of ending.
    """
    alpha = shape.getchannel("A").point(lambda v: 255 if v > 110 else 0)
    grown = alpha.filter(ImageFilter.MaxFilter(radius * 2 + 1))
    grown = grown.point(lambda v: 255 if v > 110 else 0)
    out = Image.new("RGBA", shape.size, BLACK + (0,))
    out.putalpha(grown)
    out.alpha_composite(shape)
    return out


def axis_glyph(source, size, halo, colour=GLYPH_RGB):
    """One axis icon: recoloured, with black around its own outline."""
    src = Image.open(source).convert("RGBA").resize((size, size), Image.LANCZOS)
    flat = Image.new("RGBA", src.size, colour + (255,))
    flat.putalpha(src.getchannel("A"))

    # Exactly the halo, no slack: the sprite's on-screen size is then simply
    # glyph + 2 * halo, and the C++ side can work that out rather than being
    # told a magic ratio.
    shape = Image.new("RGBA", (size + halo * 2,) * 2, (0, 0, 0, 0))
    shape.alpha_composite(flat, (halo, halo))
    return hard_halo(shape, halo)


# The fast-food hatch, in stage units of the fill area.
#
# Baked rather than drawn: a line every HATCH_STEP over 146 units is ~36 shapes
# and ~250 Scaleform calls per redraw, and the bar redraws whenever it moves.
# As one bitmap under a mask it costs nothing per frame.
#
# Built across the FULL fill width and then cropped by the mask, never built at
# the segment's width. That is what makes the pattern hold still while the bar
# drains - the edge uncovers less of a fixed pattern instead of the pattern
# re-laying itself each time. The first design sheet got this wrong and the
# diagonal appeared to crawl.
#
# Diagonal at a step of 3, at 110 of 255. Step 2 read as a checkerboard on a
# nine-unit strip and step 4 read as a diagonal but shouted; this is the middle
# one, chosen in game.
HATCH_STEP = 3
HATCH_ALPHA = 110


def hatch(width, height, step=HATCH_STEP, alpha=HATCH_ALPHA, scale=1):
    """Diagonal ink over a transparent field, in pixels at `scale` per unit."""
    img = Image.new("RGBA", (width * scale, height * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    h = img.height
    for x in range(-h, img.width + h, step * scale):
        d.line((x, h, x + h, 0), fill=BLACK + (alpha,), width=scale)
    return img


def knot(box, thick, fill=KNOT_FILL, colour=FRAME_GREY):
    """A triquetra on a square canvas.

    fill is how much of the box the knot occupies, 1.0 meaning it just touches
    the edges. The circle radius follows from that rather than being the same
    number: three circles whose centres sit r/2 from the middle reach 1.5r, so
    taking the radius straight from the box slices the outer rings flat - which
    is exactly what the first render did.
    """
    img = Image.new("RGBA", (box, box), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    extent = box * 0.5 * fill
    r = extent / 1.5
    cx = cy = box / 2
    for k in range(3):
        ang = math.pi / 2 + k * 2.0 * math.pi / 3.0
        ox, oy = cx + r * 0.5 * math.cos(ang), cy + r * 0.5 * math.sin(ang)
        d.ellipse((ox - r, oy - r, ox + r, oy + r),
                  outline=colour + (255,), width=max(1, int(round(thick))))
    return img


def knot_cap(side, box, thick, halo, fill=KNOT_FILL):
    """The knot for one end, turned and given its black contour."""
    art = knot(box, thick, fill)
    art = art.rotate(-90 if side == "left" else 90, resample=Image.BICUBIC)

    shape = Image.new("RGBA", (art.width + halo * 2, art.height + halo * 2),
                      (0, 0, 0, 0))
    shape.alpha_composite(art, (halo, halo))
    return hard_halo(shape, halo)
