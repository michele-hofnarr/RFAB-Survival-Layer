#!/usr/bin/env python3
"""Render the bar frame and the axis icons with a contour-following black halo.

Two things are being asked here.

The black behind both the bar and the icons is not a rectangle any more: it
follows the CONTOUR of the thing, three pixels out. That is one operation for
both - dilate the shape's alpha by three, fill it black, put the shape back on
top - so the icons and the bar get it from the same code.

And the grey frame is not a plain rectangle any more either. The brief gives an
approximate formula, "(10*sin(t), sin(7t + pi/2))", and says it is only meant to
break the straight line up, so three readings of it are rendered rather than one
guessed at:

    WAVE    the edge rides a slow sine along the bar
    BEAT    a fast ripple inside a slow envelope, which is the formula read
            literally: 10*sin(t) is the envelope, sin(7t + pi/2) the ripple
    THICK   the edge stays put and the frame thickens and thins instead

None of this is drawn per frame. The frame is baked once, the way the corner
vignette already is, and attached as a sprite - which is also why it can afford
to be a curve at all.

Everything is drawn at SS times size and downsampled, because a one-pixel edge
carrying a curve does not survive being drawn at final size. The preview is then
blown up with nearest-neighbour, so what is on screen is the actual pixels.

Usage:
    python tools/widget_frame_preview.py
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "widget" / "icons"
OUT = ROOT / "widget" / "design"

SS = 6      # supersampling while drawing
ZOOM = 8    # nearest-neighbour blow-up for the sheet

BAR_W, BAR_H = 150, 14
ICON = 21
PAD = 1        # black gap between the frame and the fill
HALO = 3       # the black that follows the contour

COL_COLD = (0x28, 0x4A, 0x5A)
COL_SLEEP = (0x2C, 0x37, 0x5A)
COL_FOOD = (0x54, 0x38, 0x1E)

FRAME_GREY = (0x7A, 0x7C, 0x7A)
BLACK = (0, 0, 0)
GLYPH = (0xE6, 0xE6, 0xE6)

H_SHADE_DARK = 0.34
H_SHADE_LIGHT = 0.18
NOTCH_BLACK_W = 3
NOTCH_GREY_W = 1

# How far the frame's edge is allowed to wander, in 1x pixels, and how many
# times it does so across the bar.
WAVE_AMP = 1.6
WAVE_CYCLES = 7.0
ENVELOPE_CYCLES = 1.0


def px(v):
    return int(round(v * SS))


def frame_colour_at(x, w):
    """7a7c7a at both ends, black in the middle - the brief's gradient."""
    t = abs((x / max(1, w - 1)) * 2.0 - 1.0)
    return tuple(int(round(BLACK[i] + (FRAME_GREY[i] - BLACK[i]) * t)) for i in range(3))


def edge_offset(x, w, style):
    """How far the frame's edge leaves the straight line at column x, in SS px.

    Returned as (outward displacement, extra thickness).
    """
    t = (x / max(1, w - 1)) * 2.0 * math.pi
    if style == "WAVE":
        return WAVE_AMP * math.sin(WAVE_CYCLES * t), 0.0
    if style == "BEAT":
        # The formula as written: a slow envelope times a fast ripple.
        envelope = math.sin(ENVELOPE_CYCLES * t)
        return WAVE_AMP * envelope * math.sin(WAVE_CYCLES * t + math.pi / 2), 0.0
    if style == "THICK":
        # The edge holds still; the frame gains and loses weight instead.
        return 0.0, WAVE_AMP * (1.0 + math.sin(WAVE_CYCLES * t)) * 0.5
    return 0.0, 0.0


def halo(shape, radius):
    """Black following the contour of shape, radius pixels out.

    A MaxFilter over the alpha grows the silhouette in every direction at once,
    which is what "follow the contour" means - a rectangle grown this way is
    still a rectangle, and a glyph grown this way is still that glyph.
    """
    alpha = shape.getchannel("A")
    grown = alpha.filter(ImageFilter.MaxFilter(radius * 2 + 1))
    out = Image.new("RGBA", shape.size, BLACK + (0,))
    out.putalpha(grown)
    out.alpha_composite(shape)
    return out


def hshade(size, colour):
    w, h = size
    ramp = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :, None]
    base = np.array(colour, dtype=np.float32)[None, None, :]
    left = base * (1.0 - H_SHADE_DARK)
    right = base + (255.0 - base) * H_SHADE_LIGHT
    rows = left + (right - left) * ramp
    return Image.fromarray(
        np.clip(np.repeat(rows, h, axis=0), 0, 255).astype(np.uint8), "RGB")


def bar(colour, fill_frac, style):
    """The plate, the frame and the fill, with a margin left for the halo."""
    margin = HALO + 2
    w = px(BAR_W + margin * 2)
    h = px(BAR_H + margin * 2)
    shape = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(shape)

    x0, y0 = px(margin), px(margin)
    x1, y1 = x0 + px(BAR_W) - 1, y0 + px(BAR_H) - 1

    # The black plate, column by column, because the top and bottom edges move.
    for i in range(px(BAR_W)):
        off, extra = edge_offset(i, px(BAR_W), style)
        top = y0 + px(off)
        bot = y1 + px(off)
        d.rectangle((x0 + i, top, x0 + i, bot), fill=BLACK + (255,))

    # The frame, on the same moving edges and carrying the gradient.
    for i in range(px(BAR_W)):
        off, extra = edge_offset(i, px(BAR_W), style)
        c = frame_colour_at(i, px(BAR_W)) + (255,)
        thick = px(1 + extra)
        top = y0 + px(off)
        bot = y1 + px(off)
        d.rectangle((x0 + i, top, x0 + i, top + thick - 1), fill=c)
        d.rectangle((x0 + i, bot - thick + 1, x0 + i, bot), fill=c)

    # The ends.
    for j in range(px(BAR_H)):
        left = frame_colour_at(0, px(BAR_W)) + (255,)
        right = frame_colour_at(px(BAR_W) - 1, px(BAR_W)) + (255,)
        offL, _ = edge_offset(0, px(BAR_W), style)
        offR, _ = edge_offset(px(BAR_W) - 1, px(BAR_W), style)
        d.rectangle((x0, y0 + j + px(offL), x0 + px(1) - 1, y0 + j + px(offL)), fill=left)
        d.rectangle((x1 - px(1) + 1, y0 + j + px(offR), x1, y0 + j + px(offR)), fill=right)

    # The fill, inside the frame and its pad. It stays a plain rectangle: the
    # wave belongs to the frame, and a fill that wobbled would make the value
    # harder to read, which is the one thing a bar must not do.
    ix, iy = x0 + px(1 + PAD), y0 + px(1 + PAD)
    iw = px(BAR_W - (1 + PAD) * 2)
    ih = px(BAR_H - (1 + PAD) * 2)
    fw = int(iw * fill_frac)
    if fw > 0:
        shape.paste(hshade((fw, ih), colour), (ix, iy))

    nx = ix + int(iw * 0.75)
    d.rectangle((nx - px(NOTCH_BLACK_W) // 2, iy,
                 nx + px(NOTCH_BLACK_W) // 2, iy + ih - 1), fill=BLACK + (255,))
    d.rectangle((nx - px(NOTCH_GREY_W) // 2, iy,
                 nx + px(NOTCH_GREY_W) // 2, iy + ih - 1),
                fill=frame_colour_at(px(BAR_W) // 2, px(BAR_W)) + (255,))

    out = halo(shape, px(HALO))
    return out.resize((out.width // SS, out.height // SS), Image.LANCZOS)


def icon(name, with_halo):
    """The glyph at its own size, with black following its contour."""
    margin = HALO + 2
    src = Image.open(ICONS / f"{name}.png").convert("RGBA")
    src = src.resize((px(ICON), px(ICON)), Image.LANCZOS)

    flat = Image.new("RGBA", src.size, GLYPH + (255,))
    flat.putalpha(src.getchannel("A"))

    shape = Image.new("RGBA", (px(ICON + margin * 2), px(ICON + margin * 2)), (0, 0, 0, 0))
    shape.alpha_composite(flat, (px(margin), px(margin)))

    out = halo(shape, px(HALO)) if with_halo else shape
    return out.resize((out.width // SS, out.height // SS), Image.LANCZOS)


def font(size):
    for candidate in ("seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def zoom(img):
    return img.resize((img.width * ZOOM, img.height * ZOOM), Image.NEAREST)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    styles = [
        ("STRAIGHT   no wave, for comparison", "NONE"),
        ("WAVE       edge rides a slow sine, 7 cycles, +/-1.6 px", "WAVE"),
        ("BEAT       the formula read literally: envelope x ripple", "BEAT"),
        ("THICK      edge holds, the frame gains and loses weight", "THICK"),
    ]

    f = font(15)
    small = font(12)
    pad = 20
    head = 28

    sample = bar(COL_COLD, 0.62, "NONE")
    bar_h = sample.height * ZOOM
    icon_img = icon("sleep", True)
    icon_h = icon_img.height * ZOOM

    block = head + bar_h + 18
    width = pad * 2 + sample.width * ZOOM
    height = pad + block * len(styles) + head + icon_h + 60

    sheet = Image.new("RGB", (width, height), (24, 24, 26))
    d = ImageDraw.Draw(sheet)

    for si, (label, style) in enumerate(styles):
        y0 = pad + si * block
        d.text((pad, y0), label, font=f, fill=(235, 235, 235))
        b = zoom(bar(COL_COLD, 0.62, style))
        sheet.paste(b, (pad, y0 + head), b)

    y = pad + block * len(styles)
    d.text((pad, y), "icons: black follows the glyph's own contour, 3 px out",
           font=f, fill=(235, 235, 235))
    x = pad
    for name in ("sleep", "food", "cold"):
        ic = zoom(icon(name, True))
        sheet.paste(ic, (x, y + head), ic)
        x += ic.width + 10
    for name in ("sleep", "food", "cold"):
        ic = zoom(icon(name, False))
        sheet.paste(ic, (x, y + head), ic)
        x += ic.width + 10
    d.text((x + 6, y + head + icon_h // 2 - 8), "<- with halo        without ->",
           font=small, fill=(170, 170, 170))

    d.text((pad, y + head + icon_h + 16),
           f"drawn at {SS}x and downsampled, shown at {ZOOM}x nearest-neighbour.  "
           f"bar {BAR_W}x{BAR_H}, glyph {ICON} in e6e6e6, halo {HALO} px.  "
           f"frame gradient 7a7c7a at the ends to 000000 in the middle.",
           font=small, fill=(170, 170, 170))

    path = OUT / "frame.png"
    sheet.save(path)
    print(f"  {path}  ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
