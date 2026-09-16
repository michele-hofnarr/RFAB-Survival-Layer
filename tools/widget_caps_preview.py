#!/usr/bin/env python3
"""Render end-cap ornaments for the bars, and the icons with a hard black edge.

The bar itself is not touched: it stays the straight rectangle it is. What is
being tried here is an ornament at each end that reaches out past it, the way
TrueHUD hangs a pointed cap off each end of its bars.

Five shapes, because the brief is "something like a celtic knot" and at this
size that is a bet rather than a plan. The bar is fourteen pixels tall, so a cap
is about sixteen by twelve, and whether real knotwork survives that has to be
looked at rather than argued about - KNOT is in here precisely to settle it.

    SPIKE   a pointed chevron, closest to the TrueHUD reference
    DIAMOND a diamond with a diamond cut out of it
    WEAVE   two strands crossing twice, broken at the crossings for over-under
    KNOT    a triquetra, the real celtic answer
    FRET    a stepped nordic key, the one that is native to Skyrim's own art

The icons keep the contour-following black from the last pass, but the dilated
alpha is thresholded before it is downsampled, so the edge lands hard instead of
fading out as a glow.

Usage:
    python tools/widget_caps_preview.py
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "widget" / "icons"
OUT = ROOT / "widget" / "design"

SS = 8
ZOOM = 8

BAR_W, BAR_H = 150, 14
ICON = 21
PAD = 1
import widget_art as _art  # noqa: E402

HALO = _art.HALO
OVERLAP = _art.CAP_OVERLAP

CAP_W = 16          # how far the ornament reaches out past the bar
CAP_H = 20          # taller than the bar, so it reads as a cap and not an edge

COL_COLD = (0x28, 0x4A, 0x5A)
FRAME_GREY = (0x7A, 0x7C, 0x7A)
BLACK = (0, 0, 0)
GLYPH = (0xE6, 0xE6, 0xE6)

H_SHADE_DARK = 0.34
H_SHADE_LIGHT = 0.18
NOTCH_BLACK_W = 3
NOTCH_GREY_W = 1


def px(v):
    return int(round(v * SS))


def frame_colour_at(x, w):
    t = abs((x / max(1, w - 1)) * 2.0 - 1.0)
    return tuple(int(round(BLACK[i] + (FRAME_GREY[i] - BLACK[i]) * t)) for i in range(3))


def halo(shape, radius, hard=True):
    """Black following the contour, radius out.

    Thresholding the grown alpha is what makes the edge an edge: without it the
    glyph's own antialiasing is grown along with the shape and the black fades
    out over three pixels instead of stopping.
    """
    alpha = shape.getchannel("A")
    if hard:
        alpha = alpha.point(lambda v: 255 if v > 110 else 0)
    grown = alpha.filter(ImageFilter.MaxFilter(radius * 2 + 1))
    if hard:
        grown = grown.point(lambda v: 255 if v > 110 else 0)
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


# --- the ornaments ---------------------------------------------------------
#
# Each is drawn into a CAP_W x CAP_H box whose RIGHT edge butts against the left
# end of the bar. The right cap is the same image mirrored, so the pair is
# always symmetrical and there is only ever one shape to judge.

def cap_spike(d, w, h, c):
    mid = h / 2
    d.polygon([(0, mid), (w * 0.62, 0), (w, mid * 0.45),
               (w, h - mid * 0.45), (w * 0.62, h)], fill=c)
    d.polygon([(w * 0.62, mid * 0.45), (w * 0.86, mid), (w * 0.62, h - mid * 0.45)],
              fill=BLACK + (255,))


def cap_diamond(d, w, h, c):
    mid = h / 2
    d.polygon([(0, mid), (w * 0.5, mid - w * 0.5), (w, mid), (w * 0.5, mid + w * 0.5)],
              fill=c)
    k = 0.45
    d.polygon([(w * (0.5 - k * 0.5), mid), (w * 0.5, mid - w * k * 0.5),
               (w * (0.5 + k * 0.5), mid), (w * 0.5, mid + w * k * 0.5)],
              fill=BLACK + (255,))
    d.line([(w, mid), (w * 1.0, mid)], fill=c, width=int(SS))


def cap_weave(d, w, h, c, thick):
    """Two sine strands crossing twice, with the under-strand broken."""
    mid = h / 2
    amp = h * 0.32

    def strand(phase):
        return [(x, mid + amp * math.sin(x / w * 2.0 * math.pi * 1.0 + phase))
                for x in np.linspace(0, w, 60)]

    a = strand(0.0)
    b = strand(math.pi)
    # The one that goes under is drawn first, then broken where they cross.
    d.line(b, fill=c, width=thick, joint="curve")
    for x in (w * 0.25, w * 0.75):
        d.ellipse((x - thick, mid - thick, x + thick, mid + thick),
                  fill=BLACK + (255,))
    d.line(a, fill=c, width=thick, joint="curve")


def cap_knot(d, w, h, c, thick, radius=0.42, stem=0.0):
    """A triquetra: three circles on a common centre, drawn as outlines.

    Square canvas on purpose - the left cap is turned a quarter clockwise and
    the right one a quarter anticlockwise, and a rotation must not change the
    box it occupies or the two would not line up with the bar.
    """
    # radius is how much of the box the knot fills, 1.0 meaning it just
    # touches the edges. The circle radius follows from that rather than being
    # the same number: three circles whose centres sit r/2 from the middle
    # reach r/2 + r = 1.5r, so a circle radius taken straight from the box gets
    # the outer rings sliced flat by it - which is what the first render did.
    extent = min(w, h) * 0.5 * radius
    r = extent / 1.5
    cx, cy = w / 2, h / 2
    for k in range(3):
        ang = math.pi / 2 + k * 2.0 * math.pi / 3.0
        ox, oy = cx + r * 0.5 * math.cos(ang), cy + r * 0.5 * math.sin(ang)
        d.ellipse((ox - r, oy - r, ox + r, oy + r), outline=c, width=thick)
    if stem > 0.0:
        # A short bar from the knot to the frame, so the two read as one piece
        # rather than as an ornament floating beside a rectangle.
        d.rectangle((w - w * stem, cy - thick / 2, w, cy + thick / 2), fill=c)


def cap_fret(d, w, h, c, thick):
    """A stepped key, the angular pattern Skyrim's own borders use."""
    mid = h / 2
    pts = [(0, mid), (w * 0.22, mid), (w * 0.22, mid - h * 0.34),
           (w * 0.55, mid - h * 0.34), (w * 0.55, mid + h * 0.34),
           (w * 0.86, mid + h * 0.34), (w * 0.86, mid), (w, mid)]
    d.line(pts, fill=c, width=thick, joint="curve")


CAPS = {
    "SPIKE": cap_spike,
    "DIAMOND": cap_diamond,
    "WEAVE": cap_weave,
    "KNOT": cap_knot,
    "FRET": cap_fret,
}


def cap(style, side, thick=1.6, radius=0.42, stem=0.0):
    """The ornament for one end, already turned the way it will be worn.

    KNOT is drawn on a square canvas and then given a quarter turn - clockwise
    on the left, anticlockwise on the right. Turning it rather than mirroring it
    is what was asked for, and the two are not the same thing for a shape with
    three-fold symmetry: a mirror would give back the same knot, a quarter turn
    in opposite directions gives a pair that answer each other.
    """
    square = style == "KNOT"
    w = px(CAP_H if square else CAP_W)
    h = px(CAP_H)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = FRAME_GREY + (255,)
    t = max(1, px(thick))

    if style in ("SPIKE", "DIAMOND"):
        CAPS[style](d, w, h, c)
    elif style == "KNOT":
        cap_knot(d, w, h, c, t, radius=radius, stem=stem)
    else:
        CAPS[style](d, w, h, c, t)

    if square:
        # PIL turns anticlockwise for a positive angle.
        img = img.rotate(-90 if side == "left" else 90, resample=Image.BICUBIC)
    elif side == "right":
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    return img


def black_ramp(width, mode):
    """Alpha multiplier across the widget: solid at both ends, clear in the middle.

    mode "none" leaves the black alone. "halo" fades only what is outside the
    bar. "all" fades the bar's own plate with it, which makes the empty part of
    the bar see-through in the middle - worth looking at before deciding.
    """
    if mode == "none":
        return None
    t = np.abs(np.linspace(-1.0, 1.0, width, dtype=np.float32))
    return t


def bar(colour, fill_frac, style, thick=1.1, radius=0.86, fade="none"):
    """The straight bar, untouched, with an ornament against each end border."""
    margin = HALO + 2
    cap_room = CAP_H if style else 0

    w = px(BAR_W + (margin + cap_room) * 2)
    h = px(max(BAR_H, CAP_H) + margin * 2)
    shape = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    plate = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(shape)
    dp = ImageDraw.Draw(plate)

    bx = px(margin + cap_room)
    by = (h - px(BAR_H)) // 2
    x1, y1 = bx + px(BAR_W) - 1, by + px(BAR_H) - 1

    # The bar's black plate is kept on its own layer so its alpha can be faded
    # separately from the frame and the fill drawn over it.
    dp.rectangle((bx, by, x1, y1), fill=BLACK + (255,))

    for i in range(px(BAR_W)):
        c = frame_colour_at(i, px(BAR_W)) + (255,)
        d.rectangle((bx + i, by, bx + i, by + px(1) - 1), fill=c)
        d.rectangle((bx + i, y1 - px(1) + 1, bx + i, y1), fill=c)
    endL = frame_colour_at(0, px(BAR_W)) + (255,)
    endR = frame_colour_at(px(BAR_W) - 1, px(BAR_W)) + (255,)
    d.rectangle((bx, by, bx + px(1) - 1, y1), fill=endL)
    d.rectangle((x1 - px(1) + 1, by, x1, y1), fill=endR)

    ix, iy = bx + px(1 + PAD), by + px(1 + PAD)
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

    if style:
        # Flush against the bar's END BORDER - the vertical line - rather than
        # centred in a box with slack around it. The art is cropped to what was
        # actually drawn, so the knot's ring meets that border with no gap and
        # the two read as one piece.
        for side, anchor in (("left", bx), ("right", x1 + 1)):
            art = cap(style, side, thick, radius, 0.0)
            box = art.getbbox()
            if not box:
                continue
            art = art.crop(box)
            # A pixel further in than flush, so the knot bites into the end
            # border rather than merely touching it.
            over = px(OVERLAP)
            x = anchor - art.width + over if side == "left" else anchor - over
            shape.alpha_composite(art, (x, (h - art.height) // 2))

    # The black behind everything: the plate, plus the contour halo.
    grown = shape.getchannel("A").point(lambda v: 255 if v > 110 else 0)
    grown = grown.filter(ImageFilter.MaxFilter(px(HALO) * 2 + 1))
    grown = grown.point(lambda v: 255 if v > 110 else 0)

    black = Image.new("RGBA", (w, h), BLACK + (0,))
    merged = np.maximum(np.array(grown, dtype=np.uint8),
                        np.array(plate.getchannel("A"), dtype=np.uint8))

    ramp = black_ramp(w, "none" if fade == "none" else "on")
    if ramp is not None:
        merged = (merged.astype(np.float32) * ramp[None, :]).astype(np.uint8)
    black.putalpha(Image.fromarray(merged, "L"))

    out = black
    if fade != "all":
        # The plate stays solid; only what is outside the bar fades.
        solid = Image.new("RGBA", (w, h), BLACK + (0,))
        solid.putalpha(plate.getchannel("A"))
        out.alpha_composite(solid)
    out.alpha_composite(shape)
    return out.resize((out.width // SS, out.height // SS), Image.LANCZOS)


def icon(name, hard):
    margin = HALO + 2
    src = Image.open(ICONS / f"{name}.png").convert("RGBA")
    src = src.resize((px(ICON), px(ICON)), Image.LANCZOS)
    flat = Image.new("RGBA", src.size, GLYPH + (255,))
    flat.putalpha(src.getchannel("A"))

    shape = Image.new("RGBA", (px(ICON + margin * 2),) * 2, (0, 0, 0, 0))
    shape.alpha_composite(flat, (px(margin), px(margin)))
    out = halo(shape, px(HALO), hard=hard)
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

    # The knot is the chosen one; these are the knobs worth a second look
    # before it is baked into an asset.
    rows = [
        ("APPROVED   knot 0.86 of the cap, thick 1.1, one pixel inside the "
         "end border, solid black", "none"),
    ]

    f = font(15)
    small = font(12)
    pad = 20
    head = 26

    sample = bar(COL_COLD, 0.62, "KNOT", 1.1, 0.86, "none")
    block = head + sample.height * ZOOM + 14
    ic = icon("sleep", True)

    width = pad * 2 + sample.width * ZOOM
    height = pad + block * len(rows) + head + ic.height * ZOOM + 56
    sheet = Image.new("RGB", (width, height), (24, 24, 26))
    d = ImageDraw.Draw(sheet)

    for ri, (label, fade) in enumerate(rows):
        y0 = pad + ri * block
        d.text((pad, y0), label, font=f, fill=(235, 235, 235))
        b = zoom(bar(COL_COLD, 0.62, "KNOT", 1.1, 0.86, fade))
        sheet.paste(b, (pad, y0 + head), b)

    y = pad + block * len(rows)
    d.text((pad, y), "the three axis icons, hard black edge", font=f, fill=(235, 235, 235))
    x = pad
    for name in ("sleep", "food", "cold"):
        im = zoom(icon(name, True))
        sheet.paste(im, (x, y + head), im)
        x += im.width + 10

    d.text((pad, y + head + ic.height * ZOOM + 14),
           f"left cap turned a quarter clockwise, right a quarter anticlockwise.  "
           f"bar {BAR_W}x{BAR_H} unchanged, cap {CAP_H}x{CAP_H}, halo {HALO} px.  "
           f"whole widget is now {BAR_W + CAP_H * 2} wide.",
           font=small, fill=(170, 170, 170))

    path = OUT / "knot.png"
    sheet.save(path)
    print(f"  {path}  ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
