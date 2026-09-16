#!/usr/bin/env python3
"""Cut the five temperature-feel icons out of one artwork sheet.

The source is a single strip of five glow-on-black glyphs, coldest first:
snowflake, frost knot, wind, warm star, sun. Black is the backdrop, not part of
the art, so it has to become transparency before the icons can sit on the HUD.

Glow art on black is effectively premultiplied against black already: the
brightest channel is how much glyph is there. So alpha = max(r,g,b), and the
colour is divided back out so the blues stay blue instead of washing to grey.

Unlike the needs icons these keep their own colours - Rfab_SurvivalWidget does
not tint them.

Usage:
    python3 tools/slice_temp_icons.py [source.png]

Writes widget/icons/temp0.png .. temp4.png (128x128 RGBA), which
tools/make_assets_swf.py then bakes into RSL_SurvivalHUD.swf as
"ico_temp0".."ico_temp4".
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = Path(r"R:\Games\Skyrim Addons\New-Cold-Icons.png")
OUT_DIR = ROOT / "widget" / "icons"

COUNT = 5
SIZE = 128          # matches the temp icons already in widget/icons
ALPHA_FLOOR = 10    # below this the pixel is backdrop, not faint glow
BBOX_FLOOR = 45     # what counts as the glyph when measuring it; the halo
                    # reaches the tile edges and would swallow the whole strip
MARGIN = 0.05       # padding around the glyph, as a fraction of its box


def max_channel(img):
    """Brightest channel per pixel - how much glyph is there, on a black field."""
    r, g, b = img.convert("RGB").split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)


def columns(mask):
    """Split the strip into COUNT glyph columns at the darkest seams.

    Cutting at exact fifths does not work: the glyphs are not evenly spaced and
    their glow crosses the boundaries, so a fifth clips its neighbour's halo in.
    Thresholding does not work either - the halos bridge every gap, and the
    whole strip comes back as one blob. What does work is the column brightness
    profile: the seams between glyphs are its minima. Look for one near each
    fifth boundary and cut there.
    """
    w, h = mask.size
    px = mask.load()
    profile = [sum(px[x, y] for y in range(0, h, 4)) for x in range(w)]

    cuts = [0]
    window = w // (COUNT * 2)
    for k in range(1, COUNT):
        centre = k * w // COUNT
        lo, hi = max(1, centre - window), min(w - 1, centre + window)
        cuts.append(min(range(lo, hi), key=lambda x: profile[x]))
    cuts.append(w)
    return [(cuts[i], cuts[i + 1]) for i in range(COUNT)]


def to_alpha(tile):
    """Glow-on-black -> RGBA, with the backdrop knocked out."""
    tile = tile.convert("RGB")
    r, g, b = tile.split()
    px_r, px_g, px_b = r.load(), g.load(), b.load()
    w, h = tile.size
    out = Image.new("RGBA", tile.size)
    px = out.load()
    for y in range(h):
        for x in range(w):
            cr, cg, cb = px_r[x, y], px_g[x, y], px_b[x, y]
            a = max(cr, cg, cb)
            if a < ALPHA_FLOOR:
                px[x, y] = (0, 0, 0, 0)
                continue
            # divide the backdrop back out so the hue survives the cutout
            k = 255.0 / a
            px[x, y] = (min(255, int(cr * k)), min(255, int(cg * k)),
                        min(255, int(cb * k)), a)
    return out


def square(img, mask):
    """Crop to the glyph, then pad to a square so the set reads as one size.

    The box comes from the mask at BBOX_FLOOR - measuring the faint halo instead
    would leave every glyph shrunk inside a box of empty glow.
    """
    box = mask.point(lambda v: 255 if v >= BBOX_FLOOR else 0).getbbox()
    if box:
        img = img.crop(box)
    w, h = img.size
    side = int(max(w, h) * (1 + MARGIN * 2))
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2))
    return canvas


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SRC
    if not src.is_file():
        raise SystemExit(f"source not found: {src}")

    sheet = Image.open(src)
    mask = max_channel(sheet)
    cols = columns(mask)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for i, (x0, x1) in enumerate(cols):
        box = (x0, 0, x1, sheet.size[1])
        icon = square(to_alpha(sheet.crop(box)), mask.crop(box))
        icon = icon.resize((SIZE, SIZE), Image.LANCZOS)
        out = OUT_DIR / f"temp{i}.png"
        icon.save(out)
        print(f"  {out.relative_to(ROOT)}  <- x {x0}..{x1}")


if __name__ == "__main__":
    main()
