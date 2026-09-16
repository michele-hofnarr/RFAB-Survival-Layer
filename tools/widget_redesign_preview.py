#!/usr/bin/env python3
"""Render the proposed bar and icon changes beside what is in game now.

Two things in the brief read more than one way, so each is rendered both ways
rather than guessed at:

  * The frame. "7a7c7a -> 2d2d2d" and "a gradient on that frame, 7a7c7a at the
    ends and 000000 in the middle" cannot both be the whole story - the gradient
    would paint over 2d2d2d entirely. A keeps 2d2d2d as the base and lays the
    gradient over it at half strength; B is the gradient alone.

  * The icon padding. "a black background with padding = 3" is either a
    background three pixels larger than the glyph on every side, or a background
    the size of the glyph with the glyph inset by three.

Everything is drawn at 1x stage units and then blown up with nearest-neighbour,
so what is on screen is the actual pixels rather than a smoothed idea of them.

Usage:
    python tools/widget_redesign_preview.py
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "widget" / "icons"
OUT = ROOT / "widget" / "design"

ZOOM = 8

# The numbers settled in game this session.
BAR_W, BAR_H = 150, 14
ICON = 21
FRAME = 1
PAD_NOW = 1        # black gap between frame and fill - was 2, asked for 1
OUTER_BLACK = 3    # new: black border outside the frame

COL_SLEEP = (0x2C, 0x37, 0x5A)
COL_FOOD = (0x54, 0x38, 0x1E)
COL_COLD = (0x28, 0x4A, 0x5A)

FRAME_GREY = (0x7A, 0x7C, 0x7A)
FRAME_DARK = (0x2D, 0x2D, 0x2D)
BLACK = (0, 0, 0)
GLYPH = (0xE6, 0xE6, 0xE6)

H_SHADE_DARK = 0.34
H_SHADE_LIGHT = 0.18
NOTCH_BLACK_W = 3
NOTCH_GREY_W = 1


def hshade(size, color):
    w, h = size
    ramp = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :, None]
    base = np.array(color, dtype=np.float32)[None, None, :]
    left = base * (1.0 - H_SHADE_DARK)
    right = base + (255.0 - base) * H_SHADE_LIGHT
    rows = left + (right - left) * ramp
    return Image.fromarray(
        np.clip(np.repeat(rows, h, axis=0), 0, 255).astype(np.uint8), "RGB")


def frame_colour_at(x, w, mode):
    """The frame's colour at column x of w, per the two readings of the brief."""
    # 1 at the ends, 0 in the middle.
    t = abs((x / max(1, w - 1)) * 2.0 - 1.0)
    if mode == "A":
        # 2d2d2d as the base, the grey/black gradient laid over it at half
        # strength so the base still shows.
        grad = tuple(int(round(BLACK[i] + (FRAME_GREY[i] - BLACK[i]) * t)) for i in range(3))
        return tuple(int(round(FRAME_DARK[i] * 0.5 + grad[i] * 0.5)) for i in range(3))
    # B: the gradient alone, black in the middle to 7a7c7a at both ends.
    return tuple(int(round(BLACK[i] + (FRAME_GREY[i] - BLACK[i]) * t)) for i in range(3))


def bar(colour, fill_frac, mode, outer_black, pad):
    """One bar. mode None renders exactly what is in game now."""
    border = outer_black
    w = BAR_W + border * 2
    h = BAR_H + border * 2
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # The black border outside the frame, then the plate.
    d.rectangle((0, 0, w - 1, h - 1), fill=BLACK + (255,))

    px0, py0 = border, border
    px1, py1 = border + BAR_W - 1, border + BAR_H - 1

    if mode is None:
        d.rectangle((px0, py0, px1, py1), outline=FRAME_GREY + (255,), width=FRAME)
    else:
        # A frame that changes colour along its length has to be drawn column
        # by column; outline= takes one colour.
        for x in range(BAR_W):
            c = frame_colour_at(x, BAR_W, mode) + (255,)
            d.point((px0 + x, py0), fill=c)
            d.point((px0 + x, py1), fill=c)
        for y in range(BAR_H):
            left = frame_colour_at(0, BAR_W, mode) + (255,)
            right = frame_colour_at(BAR_W - 1, BAR_W, mode) + (255,)
            d.point((px0, py0 + y), fill=left)
            d.point((px1, py0 + y), fill=right)

    ix, iy = px0 + FRAME + pad, py0 + FRAME + pad
    iw = BAR_W - (FRAME + pad) * 2
    ih = BAR_H - (FRAME + pad) * 2

    fw = int(iw * fill_frac)
    if fw > 0:
        img.paste(hshade((fw, ih), colour), (ix, iy))

    nx = ix + int(iw * 0.75)
    d.rectangle((nx - NOTCH_BLACK_W // 2, iy,
                 nx + NOTCH_BLACK_W // 2, iy + ih - 1), fill=BLACK + (255,))
    grey = FRAME_GREY if mode is None else frame_colour_at(BAR_W // 2, BAR_W, mode)
    d.rectangle((nx - NOTCH_GREY_W // 2, iy,
                 nx + NOTCH_GREY_W // 2, iy + ih - 1), fill=grey + (255,))
    return img


def tinted(name, size, colour):
    """An icon PNG scaled to size, recoloured, alpha kept."""
    src = Image.open(ICONS / f"{name}.png").convert("RGBA")
    src = src.resize((size, size), Image.LANCZOS)
    flat = Image.new("RGBA", src.size, colour + (255,))
    flat.putalpha(src.getchannel("A"))
    return flat


def icon(name, mode):
    """mode None is what is in game now: white glyph, no background."""
    if mode is None:
        return tinted(name, ICON, (255, 255, 255))

    if mode == "A":
        # Background three pixels larger than the glyph on every side.
        bg, glyph = ICON + 6, ICON
    else:
        # Background the size of the glyph, glyph inset by three.
        bg, glyph = ICON, ICON - 6

    img = Image.new("RGBA", (bg, bg), BLACK + (255,))
    img.alpha_composite(tinted(name, glyph, GLYPH), ((bg - glyph) // 2, (bg - glyph) // 2))
    return img


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

    # Stacked, not side by side. The first attempt laid the three variants in a
    # row, came out 3510 px wide, and every viewer scaled it down - which threw
    # away the one-pixel frame that the whole question is about.
    variants = [
        ("NOW   frame 7a7c7a solid, pad 2, no outer border", None, 2, 0),
        ("A     base 2d2d2d + gradient over it, pad 1, 3px black outside",
         "A", PAD_NOW, OUTER_BLACK),
        ("B     gradient alone: 7a7c7a at the ends, 000000 in the middle",
         "B", PAD_NOW, OUTER_BLACK),
    ]
    names = ["sleep", "food", "cold"]
    colours = [COL_SLEEP, COL_FOOD, COL_COLD]

    f = font(15)
    small = font(12)
    pad = 20
    head = 30

    bar_h = (BAR_H + OUTER_BLACK * 2) * ZOOM
    icon_h = (ICON + 6) * ZOOM
    block = head + bar_h + 10 + icon_h + 30

    width = pad * 2 + (BAR_W + OUTER_BLACK * 2) * ZOOM + (ICON + 6) * ZOOM + 10 * ZOOM
    sheet = Image.new("RGB", (width, pad + block * len(variants) + 40), (24, 24, 26))
    d = ImageDraw.Draw(sheet)

    for vi, (label, mode, pad_px, border) in enumerate(variants):
        y0 = pad + vi * block
        d.text((pad, y0), label, font=f, fill=(235, 235, 235))

        # One bar is enough to judge a frame; the cold axis, because its notch
        # and its colour are the ones that get looked at.
        b = zoom(bar(COL_COLD, 0.62, mode, border, pad_px))
        sheet.paste(b, (pad, y0 + head), b)

        # All three icons, since the question there is the background.
        x = pad
        for ri, name in enumerate(names):
            ic = zoom(icon(name, mode))
            sheet.paste(ic, (x, y0 + head + bar_h + 10), ic)
            x += ic.width + 8

        note = "white glyph, no background" if mode is None else (
            "background 3px larger than the glyph" if mode == "A"
            else "background the size of the glyph, glyph inset 3px")
        d.text((x + 10, y0 + head + bar_h + 10 + icon_h // 2 - 8), note,
               font=small, fill=(170, 170, 170))

    d.text((pad, pad + block * len(variants) + 8),
           f"1x stage units at {ZOOM}x nearest-neighbour.  bar {BAR_W}x{BAR_H}, "
           f"icon {ICON}, glyph e6e6e6.  Frame centre: A 23,23,23   B 1,1,1.",
           font=small, fill=(170, 170, 170))

    path = OUT / "redesign.png"
    sheet.save(path)
    print(f"  {path}  ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
