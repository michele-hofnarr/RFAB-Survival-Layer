#!/usr/bin/env python3
"""Emit the stage the HUD menu draws onto, with the bitmap artwork baked in.

This replaces make_stage_swf.py. The stage part is unchanged and still matters
for the same reason: the menu loads with ScaleModeType::kShowAll and the plugin
positions everything against GFxMovieDef::GetWidth()/GetHeight(), so the stage
has to be exactly 1280x720 or HUD elements drift between 16:9 and 21:9.

What is new is the artwork. Bars, frames and notches are still drawn from C++
through the Scaleform drawing API, because they are rectangles and gradients and
the API expresses those exactly. Two things it cannot express are icons and the
corner vignette - the vignette is a per-pixel falloff, and an icon is a picture -
so those arrive as bitmaps instead, and C++ attaches them by name.

Each PNG becomes three characters, which is the shape TrueHUD_Assets0.swf uses
for exactly the same job:

    DefineBitsLossless2   the pixels
    DefineShape3          a rectangle filled with them (clipped bitmap fill)
    DefineSprite          a one-frame clip holding that shape
    ExportAssets          gives the sprite a linkage name, "ico_<stem>"

attachMovie() then works from the plugin with no ActionScript in the file at
all, which keeps the property that made this approach worth having: there is no
.fla, no Adobe Animate, and nobody else's SWF to rebuild.

Usage:
    python tools/make_assets_swf.py
"""
import struct
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "widget" / "icons"
GENERATED = ROOT / "widget" / "generated"
OUT = ROOT / "Interface" / "RSL_SurvivalHUD.swf"

STAGE_W, STAGE_H = 1280, 720
TWIPS = 20
FRAME_RATE = 30
SWF_VERSION = 15

# Character ids. 1 is the root sprite; artwork starts well clear of it.
FIRST_CHAR = 100


class BitWriter:
    def __init__(self):
        self._bits = []

    def write(self, value, nbits):
        for i in range(nbits - 1, -1, -1):
            self._bits.append((value >> i) & 1)
        return self

    def signed(self, value, nbits):
        return self.write(value & ((1 << nbits) - 1), nbits)

    def bytes(self):
        while len(self._bits) % 8:
            self._bits.append(0)
        out = bytearray()
        for i in range(0, len(self._bits), 8):
            byte = 0
            for bit in self._bits[i:i + 8]:
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)


def bits_for_signed(*values):
    """Smallest bit width that holds every value, sign bit included."""
    n = 1
    while any(v >= (1 << (n - 1)) or v < -(1 << (n - 1)) for v in values):
        n += 1
    return n


def rect(xmin, xmax, ymin, ymax):
    nbits = bits_for_signed(xmin, xmax, ymin, ymax)
    w = BitWriter().write(nbits, 5)
    for v in (xmin, xmax, ymin, ymax):
        w.signed(v, nbits)
    return w.bytes()


def tag(code, payload=b""):
    if len(payload) < 0x3F:
        return struct.pack("<H", (code << 6) | len(payload)) + payload
    return struct.pack("<HI", (code << 6) | 0x3F, len(payload)) + payload


def scale_matrix(scale):
    """MATRIX with a uniform scale and no rotation or translation."""
    fixed = int(round(scale * 65536.0))
    nbits = bits_for_signed(fixed)
    w = BitWriter()
    w.write(1, 1).write(nbits, 5).signed(fixed, nbits).signed(fixed, nbits)
    w.write(0, 1)      # no rotate
    w.write(0, 5)      # no translate bits
    return w.bytes()


def identity_matrix():
    w = BitWriter()
    w.write(0, 1).write(0, 1).write(0, 5)
    return w.bytes()


def define_bits_lossless2(char_id, rgba, w, h):
    """Tag 36. PIX32 is alpha-first and the colour is premultiplied."""
    out = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        if a != 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        out += bytes((a, r, g, b))
    body = struct.pack("<HBHH", char_id, 5, w, h) + zlib.compress(bytes(out), 9)
    return tag(36, body)


def define_shape_bitmap(shape_id, bitmap_id, w, h):
    """Tag 32. A w x h rectangle filled with the bitmap, one pixel to one pixel.

    The fill matrix scales by 20 because a bitmap fill maps one image pixel to
    one twip by default, and the shape is measured in twips.
    """
    fill = bytes((0x41,)) + struct.pack("<H", bitmap_id) + scale_matrix(TWIPS)

    body = BitWriter()
    body.write(1, 4).write(0, 4)          # NumFillBits = 1, NumLineBits = 0

    # One style change, setting fill style 1 only.
    #
    # Setting BOTH sides of every edge to the same style renders nothing: with
    # the same fill left and right there is no boundary for the winding rule to
    # find, and the shape comes out fully transparent. The edges below run
    # clockwise on a y-down stage, so the interior is to the right of the
    # direction of travel, which is what fill style 1 means.
    body.write(0, 1)                      # not an edge record
    body.write(0, 1).write(0, 1)          # no new styles, no line style
    body.write(1, 1)                      # StateFillStyle1
    body.write(0, 1)                      # no StateFillStyle0
    body.write(0, 1)                      # no move: the pen starts at 0,0
    body.write(1, 1)                      # FillStyle1 = 1

    tw, th = w * TWIPS, h * TWIPS
    n = bits_for_signed(tw, th, -tw, -th)
    for dx, dy in ((tw, 0), (0, th), (-tw, 0), (0, -th)):
        body.write(1, 1).write(1, 1)      # edge record, straight
        body.write(n - 2, 4).write(1, 1)  # general line: both deltas follow
        body.signed(dx, n).signed(dy, n)

    body.write(0, 6)                      # EndShapeRecord

    payload = (struct.pack("<H", shape_id)
               + rect(0, tw, 0, th)
               + bytes((1,)) + fill        # one fill style
               + bytes((0,))               # no line styles
               + body.bytes())
    return tag(32, payload)


def define_sprite(sprite_id, shape_id):
    place = (bytes((0x06,))                 # has character, has matrix
             + struct.pack("<H", 1)         # depth
             + struct.pack("<H", shape_id)
             + identity_matrix())
    body = struct.pack("<HH", sprite_id, 1) + tag(26, place) + tag(1) + tag(0)
    return tag(39, body)


def export_assets(entries):
    body = struct.pack("<H", len(entries))
    for char_id, name in entries:
        body += struct.pack("<H", char_id) + name.encode("ascii") + b"\x00"
    return tag(56, body)


def collect_pngs():
    """Every PNG the widget can attach, newest source first."""
    found = []
    for folder in (ICONS, GENERATED):
        if folder.is_dir():
            found += sorted(folder.glob("*.png"))
    return found


def main():
    art = []
    exports = []
    char = FIRST_CHAR
    listed = []

    for path in collect_pngs():
        img = Image.open(path).convert("RGBA")
        w, h = img.size
        bitmap_id, shape_id, sprite_id = char, char + 1, char + 2
        char += 3

        art.append(define_bits_lossless2(bitmap_id, img.tobytes(), w, h))
        art.append(define_shape_bitmap(shape_id, bitmap_id, w, h))
        art.append(define_sprite(sprite_id, shape_id))

        name = "ico_" + path.stem
        exports.append((sprite_id, name))
        listed.append(f"{name} {w}x{h}")

    root_sprite = struct.pack("<HH", 1, 1) + tag(1) + tag(0)
    place_root = (bytes((0x06,)) + struct.pack("<H", 1) + struct.pack("<H", 1)
                  + identity_matrix())

    body = b"".join([
        rect(0, STAGE_W * TWIPS, 0, STAGE_H * TWIPS),
        struct.pack("<H", FRAME_RATE << 8),
        struct.pack("<H", 1),
        tag(69, struct.pack("<I", 0)),      # FileAttributes, always first
        tag(9, bytes((0, 0, 0))),           # SetBackgroundColor
        b"".join(art),
        export_assets(exports) if exports else b"",
        tag(39, root_sprite),
        tag(26, place_root),
        tag(1),
        tag(0),
    ])

    header = b"FWS" + bytes((SWF_VERSION,))
    data = header + struct.pack("<I", len(header) + 4 + len(body)) + body

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)

    print(f"  {OUT.relative_to(ROOT)}  {len(data)} bytes, stage {STAGE_W}x{STAGE_H}")
    for line in listed:
        print(f"    {line}")


if __name__ == "__main__":
    main()
