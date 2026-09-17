#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Draw what is actually in the baked climate file. Both worldspaces.

Offline.

    python tools/draw_baked.py

WHY FROM THE FILE AND NOT FROM THE MODEL. bake_climate.py draws a picture on
its way past, which is the field as it computed it. This reads
SKSE/Plugins/_RSL_Climate.bin back and draws THAT - the same bytes the plugin
loads, through the same triangle interpolation the plugin uses. If the two
pictures ever disagree, the file is wrong, and that is exactly the thing worth
being able to see.

    docs/img/baked_tamriel.png
    docs/img/baked_solstheim.png

Terrain for the hillshade comes from Skyrim.esm and Dragonborn.esm, so the two
are read for that and nothing else.

EACH PICTURE IS CROPPED TO ITS LAND, and the colour scale is stretched over
what is inside that crop. Solstheim's worldspace is four fifths open sea, and
sea floor is flat and snowless, which makes it the warmest thing in the
worldspace - left in, it takes the whole warm end of the scale and squashes the
island into two shades. The threshold is read off the terrain rather than
guessed: a share of the way up from the lowest cell to the highest. For Tamriel
almost everything clears it and the crop does nothing.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT.parent.parent.parent / "Data"
OUT = ROOT / "docs" / "img"
ASSET = ROOT / "SKSE" / "Plugins" / "_RSL_Climate.bin"

sys.path.insert(0, str(ROOT / "tools"))
import bake_climate as B        # noqa: E402  - the colour ramp lives there
import esm                     # noqa: E402
import mesh_clouds             # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

CELL = 4096.0
SIDE = 33
PER = SIDE * SIDE
STEP = 2                      # a pixel every 256 units
PER_CELL = 32 // STEP

# How far up from the lowest cell a cell has to stand to count as land, and how
# many cells of sea to keep around it.
LAND_SHARE = 0.30
MARGIN = 2

WORLDS = {
    "TAMRIEL": ("Skyrim.esm", 0x0000003C, "baked_tamriel"),
    "SOLSTH": ("Dragonborn.esm", 0x02000800, "baked_solstheim"),
}


def read_asset(path):
    """[(tag, minX, minY, width, height, index, data)] in file order."""
    buf = path.read_bytes()
    magic, version, blocks = struct.unpack_from("<8sII", buf, 0)
    if magic != b"RSLCLIM2":
        raise SystemExit("%s is version %r, not RSLCLIM2 - rerun bake_climate.py"
                         % (path, magic))
    out = []
    at = 16
    for _ in range(blocks):
        tag = struct.unpack_from("<8s", buf, at)[0].rstrip(b"\0").decode()
        minX, minY, w, h, cells = struct.unpack_from("<iiiiI", buf, at + 8)
        at += 28
        index = struct.unpack_from("<%di" % (w * h), buf, at)
        at += w * h * 4
        data = struct.unpack_from("<%dh" % (cells * PER), buf, at)
        at += cells * PER * 2
        out.append((tag, minX, minY, w, h, index, data))
    return out


def main():
    if not ASSET.exists():
        raise SystemExit("%s is not there - run tools/bake_climate.py first"
                         % ASSET)
    blocks = read_asset(ASSET)
    print("%s: %d worldspace(s)" % (ASSET.name, len(blocks)))

    OUT.mkdir(parents=True, exist_ok=True)
    for tag, minX, minY, w, h, index, data in blocks:
        if tag not in WORLDS:
            print("  %s: no terrain to shade it with, skipped" % tag)
            continue
        plugin, worldspace, name = WORLDS[tag]
        print("  %s: %dx%d cells from (%d, %d)" % (tag, w, h, minX, minY))

        buf = (DATA / plugin).read_bytes()
        groups = esm.top_groups(buf)
        was = esm.TAMRIEL
        esm.TAMRIEL = worldspace
        try:
            heights, _, _ = esm.land(buf, groups, set())
        finally:
            esm.TAMRIEL = was
        del buf

        have = [(gx, gy) for (gx, gy) in heights
                if 0 <= gx - minX < w and 0 <= gy - minY < h
                and index[(gy - minY) * w + (gx - minX)] >= 0]
        if not have:
            print("    nothing of it is in the file")
            continue

        tops = {k: max(heights[k]) for k in have}
        low, high = min(tops.values()), max(tops.values())
        sea = low + (high - low) * LAND_SHARE
        land = [k for k, v in tops.items() if v > sea] or have
        gx0 = max(min(k[0] for k in land) - MARGIN, min(k[0] for k in have))
        gx1 = min(max(k[0] for k in land) + MARGIN, max(k[0] for k in have))
        gy0 = max(min(k[1] for k in land) - MARGIN, min(k[1] for k in have))
        gy1 = min(max(k[1] for k in land) + MARGIN, max(k[1] for k in have))
        # And no wider than where anything is placed. Solstheim's worldspace
        # carries a low-detail copy of Skyrim's coast for the view across
        # the sea: terrain, some of it snow-painted, and nothing on it. It
        # is bigger than the island and was the picture.
        buf = (DATA / plugin).read_bytes()
        refs, _ = mesh_clouds.world_refs(buf, esm.top_groups(buf), worldspace)
        del buf
        rx0, rx1, ry0, ry1 = mesh_clouds.window(refs, MARGIN)
        gx0, gx1 = max(gx0, rx0), min(gx1, rx1)
        gy0, gy1 = max(gy0, ry0), min(gy1, ry1)
        have = [k for k in have if gx0 <= k[0] <= gx1 and gy0 <= k[1] <= gy1]
        if len(land) < len(tops):
            print("    land taken as above %.0f: %d cells of %d"
                  % (sea, len(land), len(tops)))
        W = (gx1 - gx0 + 1) * PER_CELL
        H = (gy1 - gy0 + 1) * PER_CELL

        lo = hi = None
        for (gx, gy) in have:
            slot = index[(gy - minY) * w + (gx - minX)]
            base = slot * PER
            for k in range(PER):
                t = data[base + k] * 0.01
                lo = t if lo is None or t < lo else lo
                hi = t if hi is None or t > hi else hi
        print("    %d cells, %+.1f .. %+.1f degrees, image %dx%d"
              % (len(have), lo, hi, W, H))

        img = [bytearray(b"\x08\x0a\x14" * W) for _ in range(H)]
        for (gx, gy) in have:
            hs = heights[(gx, gy)]
            base = index[(gy - minY) * w + (gx - minX)] * PER
            for jj in range(PER_CELL):
                j = jj * STEP
                py = (gy1 - gy) * PER_CELL + (PER_CELL - 1 - jj)
                for ii in range(PER_CELL):
                    i = ii * STEP
                    k = j * SIDE + i
                    z = hs[k]
                    e = hs[k + 1] if i < 32 else z
                    n = hs[k + SIDE] if j < 32 else z
                    shade = 1.0 + ((z - e) + (z - n)) / 900.0
                    shade = 0.55 if shade < 0.55 else (1.35 if shade > 1.35 else shade)
                    r, g, b = B.ramp(data[base + k] * 0.01, lo, hi)
                    p = ((gx - gx0) * PER_CELL + ii) * 3
                    img[py][p:p + 3] = bytes((min(255, int(r * shade)),
                                              min(255, int(g * shade)),
                                              min(255, int(b * shade))))
        esm.png(OUT / (name + ".png"), W, H, img)
        print("    wrote %s.png" % name)


if __name__ == "__main__":
    main()
