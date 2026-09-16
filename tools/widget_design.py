#!/usr/bin/env python3
"""Draw the survival HUD and save it as PNG.

This is the approved bar design, and the reference the SWF and the C++ side are
expected to match. It renders without launching the game, which is how the whole
widget rework was iterated.

Everything is authored at SS times supersampling and downsampled with LANCZOS,
because 1 px strokes at final size do not survive.

The bar, bottom to top:

    black plate                 the full BAR_W x BAR_H rectangle
    grey frame                  FRAME px, FRAME_GREY
    flat axis colour            the fill, PAD px inside the frame
    horizontal shade            dark at the left, light at the right
    white loss marker           what was just lost, drawn plain white
    corner vignette             built for the FULL bar, then cropped to the
                                fill, so the corners keep a constant size
    threshold notch at 75%      wider black line, grey frame colour over it

There is no end ornament. Three woven-cap designs were drawn and all three were
rejected; the ornament is dropped rather than carried around unfinished.

The white gloss layer is off. The code is kept because turning it back on is one
constant, but the approved look has no gloss.

Two behaviours are design decisions that this still renderer cannot show, and
they belong to whoever implements the widget:

  - The danger colour applies to the COLD axis only, below DANGER_AT, which is
    the hypothermia threshold. It must crossfade rather than snap.
  - The white loss marker must animate, and its motion should read as the rate
    the axis is deteriorating - not just appear and disappear.

Usage:
    python tools/widget_design.py
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "widget" / "design"

SS = 4  # supersampling factor

# --- geometry, in 1280x720 stage units -------------------------------------
# Matched to TrueHUD's player bar. Its coloured strip is a 100x2 shape placed
# with a x1/x5 matrix (100x10), and Bar._xscale = fPlayerWidgetBarWidth = 245,
# so what the player sees there is 245x10. Ours carries its frame and pad inside
# the plate - 2px a side - so 249x14 puts our FILL at exactly their 245x10.
BAR_W, BAR_H = 249, 14   # outer size, black plate included
FRAME = 1                # grey frame thickness
PAD = 1                  # black gap between the frame and the coloured bar
ICON = 18
GAP_ICON = 7
ROW_PITCH = 26
N_ROWS = 3

ROW_W = ICON + GAP_ICON + BAR_W
ROW_H = ROW_PITCH * N_ROWS

# --- palette ---------------------------------------------------------------
COL_SLEEP = (0x2C, 0x37, 0x5A)   # dark, almost venous blue
COL_FOOD = (0x54, 0x38, 0x1E)    # dark brown
COL_COLD = (0x28, 0x4A, 0x5A)    # dark icy blue
COL_DANGER = (0x6E, 0x11, 0x11)  # blood red

BED_DARK = (10, 11, 13)
FRAME_GREY = (122, 124, 122)

# The danger colour is not a general low-value state: it belongs to the cold
# axis alone, at the hypothermia threshold. Sleep and hunger keep their own
# colour all the way down.
DANGER_AXIS = 2
DANGER_AT = 0.10


def px(v):
    return int(round(v * SS))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def vgrad(size, top, bottom):
    """Vertical gradient as an RGB image."""
    w, h = size
    ramp = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None]
    top_a = np.array(top, dtype=np.float32)
    bot_a = np.array(bottom, dtype=np.float32)
    band_rows = top_a + (bot_a - top_a) * ramp
    return Image.fromarray(np.repeat(band_rows[:, None, :], w, axis=1).astype(np.uint8), "RGB")


# --- the bar, as four stacked layers ---------------------------------------
#
#   1. flat colour
#   2. horizontal shade, dark at the left and light at the right
#   3. white gloss over the top half only, meeting the vertical centre
#   4. dark vignette pulling the corners down
#
# All four are properties of the coloured rectangle itself, so they are built at
# the size of the fill and move with it. The empty part of the bar is just the
# black plate showing through.

# Which end of the gloss is opaque. "edge" is bright at the top edge and fades
# to nothing at the centre; "centre" is the other way round. Both are plausible
# readings of the brief, so both get rendered.
GLOSS_FROM = "none"

H_SHADE_DARK = 0.34   # how far the left end is pushed towards black
H_SHADE_LIGHT = 0.18  # how far the right end is pushed towards white
GLOSS_ALPHA = 130     # peak whiteness of the gloss, 0..255
VIGNETTE = 0.78       # peak corner darkening, 0..1

# Threshold marks across the bar, drawn in the same two layers as the frame:
# a wider black line with the grey frame colour on top of it.
NOTCH_AT = (0.75,)
NOTCH_BLACK_W = 3
NOTCH_GREY_W = 1


def hshade(size, color):
    """Layer 1 + 2: flat colour under a left-to-right shade."""
    w, h = size
    ramp = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :, None]
    base = np.array(color, dtype=np.float32)[None, None, :]
    left = base * (1.0 - H_SHADE_DARK)
    right = base + (255.0 - base) * H_SHADE_LIGHT
    rows = left + (right - left) * ramp
    return Image.fromarray(np.clip(np.repeat(rows, h, axis=0), 0, 255).astype(np.uint8), "RGB")


def gloss_layer(size, direction=None):
    """Layer 3: white, confined to the top half, meeting the vertical centre."""
    w, h = size
    if (direction or GLOSS_FROM) == "none":
        return Image.new("RGBA", (w, h), (0, 0, 0, 0))
    half = max(1, h // 2)
    t = np.linspace(0.0, 1.0, half, dtype=np.float32)  # 0 at the top edge
    a = (1.0 - t) if (direction or GLOSS_FROM) == "edge" else t
    a = a ** 1.5  # bias the falloff so the bright end stays tight

    alpha = np.zeros((h, w), dtype=np.float32)
    alpha[:half, :] = (a * GLOSS_ALPHA)[:, None]

    out = np.zeros((h, w, 4), dtype=np.float32)
    out[:, :, :3] = 255.0
    out[:, :, 3] = alpha
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def vignette_layer(size):
    """Layer 4: black in the corners, and only in the corners.

    Strictly a product of the two axes, so a pixel is darkened only where it is
    near an extreme of BOTH. An earlier version added a max() term to keep the
    long edges from looking flat; that darkened the whole top and bottom edge
    and made the fill look like it had a symmetric top-and-bottom gradient,
    which is not what the gloss is supposed to do.
    """
    w, h = size
    nx = np.abs(np.linspace(-1.0, 1.0, w, dtype=np.float32))[None, :]
    ny = np.abs(np.linspace(-1.0, 1.0, h, dtype=np.float32))[:, None]

    darken = np.clip((nx ** 2.5) * (ny ** 2.0), 0.0, 1.0) * VIGNETTE

    out = np.zeros((h, w, 4), dtype=np.float32)
    out[:, :, 3] = darken * 255.0
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def bar(color, fill_frac, delta_frac=0.0, danger=False, gloss=None):
    """Black plate, grey frame, and the four-layer bar inside it."""
    img = Image.new("RGBA", (px(BAR_W), px(BAR_H)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = COL_DANGER if danger else color

    # black backing
    d.rectangle((0, 0, px(BAR_W) - 1, px(BAR_H) - 1), fill=(0, 0, 0, 255))
    # grey frame
    d.rectangle((0, 0, px(BAR_W) - 1, px(BAR_H) - 1), outline=FRAME_GREY + (255,), width=px(FRAME))

    ix, iy = px(FRAME + PAD), px(FRAME + PAD)
    iw = px(BAR_W) - px((FRAME + PAD) * 2)
    ih = px(BAR_H) - px((FRAME + PAD) * 2)

    fw = int(iw * max(0.0, min(1.0, fill_frac)))
    if fw > 0:
        fill = hshade((fw, ih), c).convert("RGBA")
        fill.alpha_composite(gloss_layer((fw, ih), gloss))
        # The vignette is built for the full bar and then cropped to the fill,
        # so its corners keep a constant size instead of stretching and
        # squashing as the value moves. Only a full bar shows all four.
        fill.alpha_composite(vignette_layer((iw, ih)).crop((0, 0, fw, ih)))
        img.alpha_composite(fill, (ix, iy))

    # What was just lost, drawn plain white immediately after the fill. Only
    # losses are marked; a gain gets no marker at all. There is a one-pixel
    # floor because a slow tick moves the bar by less than a pixel.
    if delta_frac < -1e-4:
        dw = max(px(1), int(iw * abs(delta_frac)))
        dx = ix + fw
        dw = min(dw, ix + iw - dx)
        if dw > 0:
            img.paste(Image.new("RGB", (dw, ih), (255, 255, 255)), (dx, iy))

    # Threshold marks, over everything so they stay readable at any fill.
    for frac in NOTCH_AT:
        nx = ix + int(iw * frac)
        d.rectangle((nx - px(NOTCH_BLACK_W) // 2, iy,
                     nx + px(NOTCH_BLACK_W) // 2, iy + ih - 1), fill=(0, 0, 0, 255))
        d.rectangle((nx - px(NOTCH_GREY_W) // 2, iy,
                     nx + px(NOTCH_GREY_W) // 2, iy + ih - 1), fill=FRAME_GREY + (255,))

    return img


def load_font(size):
    for name in ("seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(name, px(size))
        except OSError:
            continue
    return ImageFont.load_default()


def widget(values, deltas, penalties, gloss=None):
    """The three-row block, at 1x stage units.

    No end ornament: the woven caps are parked until we decide what to do with
    them, and judging the bar itself is easier without them in the way.
    """
    img = Image.new("RGBA", (px(ROW_W), px(ROW_H)), (0, 0, 0, 0))
    colors = [COL_SLEEP, COL_FOOD, COL_COLD]
    f = load_font(9)
    d = ImageDraw.Draw(img)

    for i, (v, dv, pen) in enumerate(zip(values, deltas, penalties)):
        y = i * ROW_PITCH
        bx = ICON + GAP_ICON
        by = y + (ROW_PITCH - BAR_H) // 2

        in_danger = (i == DANGER_AXIS) and v < DANGER_AT
        img.alpha_composite(bar(colors[i], v, dv, danger=in_danger, gloss=gloss), (px(bx), px(by)))

        # placeholder for the real axis icon
        d.ellipse((px(1), px(y + 5), px(1 + ICON - 4), px(y + 5 + ICON - 4)),
                  outline=lerp(colors[i], (255, 255, 255), 0.2) + (220,), width=px(1.2))

        if pen:
            t = str(pen)
            tw = d.textlength(t, font=f)
            tx, ty = px(bx + BAR_W - 5) - tw, px(by + 2)
            d.text((tx + px(0.7), ty + px(0.7)), t, font=f, fill=(0, 0, 0, 210))
            d.text((tx, ty), t, font=f, fill=(238, 238, 232, 255))

    return img


def downscale(img):
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # Two states worth looking at: ordinary values, and the cold axis under the
    # hypothermia threshold where the danger colour takes over.
    states = [
        ("normal", [0.88, 0.46, 0.62], [0.0, -0.02, -0.03], [0, 34, 18]),
        ("cold in danger", [0.55, 0.30, 0.07], [-0.01, -0.02, -0.04], [22, 48, 71]),
    ]
    renders = [(name, downscale(widget(v, d, p_))) for name, v, d, p_ in states]

    pad, head = 10, 16
    sw = max(r.width for _, r in renders) + pad * 2
    sh = sum(r.height + head for _, r in renders) + pad * 2
    sheet = Image.new("RGB", (sw, sh), (32, 33, 36))
    sd = ImageDraw.Draw(sheet)
    y = pad
    for name, r in renders:
        sd.text((pad, y), name, font=ImageFont.load_default(), fill=(170, 172, 176))
        sheet.paste(r, (pad, y + head - 3), r)
        y += r.height + head
    sheet.save(OUT / "bars_1x.png")
    sheet.resize((sw * 4, sh * 4), Image.NEAREST).save(OUT / "bars_4x.png")

    # Placement check: the same stage coordinates at two aspect ratios. kShowAll
    # scales the 1280x720 stage uniformly and centres it, so the block has to
    # land in the same visual spot in both.
    hud = renders[0][1]
    for cw, ch in ((2560, 1440), (3440, 1440)):
        scale = min(cw / 1280.0, ch / 720.0)
        canvas = Image.new("RGB", (cw, ch), (26, 27, 29))
        dc = ImageDraw.Draw(canvas)
        ox, oy = (cw - 1280 * scale) / 2.0, (ch - 720 * scale) / 2.0
        dc.rectangle((ox, oy, ox + 1280 * scale, oy + 720 * scale), outline=(60, 62, 66))
        s = hud.resize((int(hud.width * scale), int(hud.height * scale)), Image.LANCZOS)
        canvas.paste(s, (int(ox + 0.355 * 1280 * scale), int(oy + 0.535 * 720 * scale)), s)
        canvas.resize((cw // 2, ch // 2), Image.LANCZOS).save(OUT / f"placement_{cw}x{ch}.png")

    print("wrote " + ", ".join(p_.name for p_ in sorted(OUT.glob("*.png"))))


if __name__ == "__main__":
    main()
