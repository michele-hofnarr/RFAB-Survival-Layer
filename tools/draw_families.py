#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The region families of a worldspace, drawn, and tried on its control points.

    python tools/draw_families.py            # Tamriel
    python tools/draw_families.py SOLSTH     # Solstheim

Two pictures in docs/img, on the same hillshade and projection as
draw_baked.py (a pixel every 256 units):

    families_textures[_<tag>].png   every vertex coloured by the dominant
                                    texture family painted on it
    families_objects[_<tag>].png    every placed object of a named family,
                                    as a dot

And a table: for the worldspace's control points, does any family explain
what relief + snow leave as residual. The families here are the candidates
that were looked at, not the model - the model's are fit_climate.FAMILIES.
The test samples raw coverage over the 3x3 cells around a point, without
the blur the bake applies, so it is a first look rather than the fit.

The window is the bounding box of the placed references, not of the terrain:
Solstheim's worldspace carries a low-detail copy of Skyrim's coast for the
view across the sea, and it has terrain but nothing placed on it.
"""
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import esm                     # noqa: E402
import bake_climate as B       # noqa: E402
import fit_climate as F        # noqa: E402
import mesh_clouds as M        # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DATA = B.DATA
OUT = ROOT / "docs" / "img"
CELL, SIDE, STEP = 4096.0, 33, 2
PER_CELL = 32 // STEP
MARGIN = 2

# Texture families: name, test on the lower-cased LTEX editor id, colour.
# First match wins, so the specific words go before the general ones.
TEXTURES = [
    ("volcanictundra", lambda e: "volcanictundra" in e,                (255, 140, 0)),
    ("volcanicash",    lambda e: "volcanicash" in e,                   (200, 60, 40)),
    ("snow",           lambda e: "snow" in e,                          (255, 255, 255)),
    ("frozenmarsh",    lambda e: "frozenmarsh" in e,                   (190, 220, 255)),
    ("fallforest",     lambda e: "fallforest" in e,                    (255, 190, 40)),
    ("reach",          lambda e: "reach" in e,                         (200, 60, 220)),
    ("field",          lambda e: "field" in e,                         (245, 225, 60)),
    ("tundra",         lambda e: "tundra" in e and "volcanic" not in e, (170, 150, 40)),
    ("pineforest",     lambda e: "pineforest" in e,                    (40, 190, 60)),
    ("coast",          lambda e: "coast" in e,                         (110, 170, 255)),
]

# Object families: name, words in the mesh editor id, colour. Same rule.
OBJECTS = [
    ("volcanic",   ("volcanic", "jazbay", "dragonstongue", "creep", "sulfur"), (255, 140, 0)),
    ("ash",        ("ash", "scathecraw", "trama"),                              (200, 60, 40)),
    ("snow/ice",   ("snow", "ice", "frozen", "glacier"),                       (255, 255, 255)),
    ("aspen/fall", ("aspen", "fallforest"),                                     (255, 190, 40)),
    ("reach",      ("reach", "juniper"),                                        (200, 60, 220)),
    ("marsh",      ("marsh", "swamp"),                                          (0, 210, 230)),
    ("tundra",     ("tundra", "fieldgrass", "lavender"),                        (245, 225, 60)),
    ("pine",       ("pineforest", "pine"),                                      (40, 190, 60)),
    ("coast",      ("coast", "beach"),                                          (110, 170, 255)),
]


def ltex_edids(buf, groups):
    """{formid: editor id, lower-cased} for a plugin's landscape textures."""
    out = {}
    if b"LTEX" not in groups:
        return out
    gp, gs = groups[b"LTEX"]
    p, e = gp + 24, gp + gs
    while p + 24 <= e:
        size, flags, fid = struct.unpack_from("<III", buf, p + 4)
        for s, v in esm.subrecords(esm.payload(buf, p, size, flags)):
            if s == b"EDID":
                out[fid] = v.rstrip(b"\0").decode("cp1252", "replace").lower()
        p += 24 + size
    return out


def hillshade(heights, gx0, gx1, gy0, gy1, paint):
    """Render the window; paint(cell, k) gives a colour or None for bare."""
    W, H = (gx1 - gx0 + 1) * PER_CELL, (gy1 - gy0 + 1) * PER_CELL
    img = [bytearray(b"\x08\x0a\x14" * W) for _ in range(H)]
    for (gx, gy), hs in heights.items():
        if not (gx0 <= gx <= gx1 and gy0 <= gy <= gy1):
            continue
        for jj in range(PER_CELL):
            j = jj * STEP
            row = img[(gy1 - gy) * PER_CELL + (PER_CELL - 1 - jj)]
            for ii in range(PER_CELL):
                i = ii * STEP
                k = j * SIDE + i
                z = hs[k]
                e = hs[k + 1] if i < 32 else z
                n = hs[k + SIDE] if j < 32 else z
                shade = 1.0 + ((z - e) + (z - n)) / 900.0
                shade = 0.55 if shade < 0.55 else (1.35 if shade > 1.35 else shade)
                rgb = paint((gx, gy), k)
                if rgb is None:
                    rgb = (150, 150, 150) if z > -14000.0 else (60, 60, 60)
                p = ((gx - gx0) * PER_CELL + ii) * 3
                row[p:p + 3] = bytes(min(255, int(c * shade)) for c in rgb)
    return img, W, H


def main():
    tag = sys.argv[1].upper() if len(sys.argv) > 1 else "TAMRIEL"
    plugin, worldspace = M.world(tag)
    sheet = next(w[3] for w in B.WORLDS if w[0] == tag)
    suffix = "" if tag == "TAMRIEL" else "_" + tag.lower()

    # Texture ids from every plugin the worldspace may paint with.
    edids = {}
    for name in ("Skyrim.esm", plugin):
        buf = (DATA / name).read_bytes()
        edids.update(ltex_edids(buf, esm.top_groups(buf)))
        del buf
    famids = {}
    for fid, e in edids.items():
        for name, test, _ in TEXTURES:
            if test(e):
                famids.setdefault(name, set()).add(fid)
                break
    print("%s: %s; texture families with ids: %s"
          % (tag, plugin, ", ".join("%s %d" % (n, len(v)) for n, v in famids.items())))

    buf = (DATA / plugin).read_bytes()
    groups = esm.top_groups(buf)
    bases = M.base_records(buf, groups)
    refs, _ = M.world_refs(buf, groups, worldspace)
    was = esm.TAMRIEL
    esm.TAMRIEL = worldspace
    try:
        heights, _, fams = esm.land(buf, groups, set(), None, famids)
    finally:
        esm.TAMRIEL = was
    del buf
    gx0, gx1, gy0, gy1 = M.window(refs, MARGIN)
    print("  %d cells, %d placed references, window (%d..%d, %d..%d)"
          % (len(heights), len(refs), gx0, gx1, gy0, gy1))

    # --- textures -----------------------------------------------------------
    order = [n for n, _, _ in TEXTURES if n in fams]
    colour = {n: c for n, _, c in TEXTURES}

    def paint_tex(cell, k):
        best, bv = None, 0.15
        for n in order:
            v = fams[n][cell][k]
            if v > bv:
                best, bv = n, v
        return colour[best] if best else None

    img, W, H = hillshade(heights, gx0, gx1, gy0, gy1, paint_tex)
    esm.png(OUT / ("families_textures%s.png" % suffix), W, H, img)
    inside = [k for k in heights if gx0 <= k[0] <= gx1 and gy0 <= k[1] <= gy1]
    share = {n: sum(sum(fams[n][k]) for k in inside) for n in order}
    total = len(inside) * SIDE * SIDE
    print("  wrote families_textures%s.png (%dx%d): %s" % (suffix, W, H, ", ".join(
        "%s %.1f%%" % (n, 100.0 * share[n] / total) for n in order)))

    # --- objects ------------------------------------------------------------
    family_of = {}
    for base, (_, edid) in bases.items():
        e = edid.lower()
        for name, words, rgb in OBJECTS:
            if any(w in e for w in words):
                family_of[base] = (name, rgb)
                break
    img, W, H = hillshade(heights, gx0, gx1, gy0, gy1, lambda cell, k: None)
    counts = {n: 0 for n, _, _ in OBJECTS}
    for base, x, y in refs:
        fam = family_of.get(base)
        if fam is None:
            continue
        name, rgb = fam
        px = int((x / CELL - gx0) * PER_CELL)
        py = int((gy1 + 1 - y / CELL) * PER_CELL) - 1
        if 0 <= px < W - 1 and 0 <= py < H - 1:
            counts[name] += 1
            for dy in (0, 1):
                for dx in (0, 1):
                    p = (px + dx) * 3
                    img[py + dy][p:p + 3] = bytes(rgb)
    esm.png(OUT / ("families_objects%s.png" % suffix), W, H, img)
    print("  wrote families_objects%s.png: %s" % (suffix, ", ".join(
        "%s %d" % (n, c) for n, c in counts.items() if c)))

    # --- at the control points ---------------------------------------------
    pts = B.load_points().get(sheet, [])
    if not pts:
        print("\nno control points on sheet %s" % sheet)
        return

    def around(x, y, n):
        gx, gy = int(x // CELL), int(y // CELL)
        tot = cnt = 0.0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                g = fams[n].get((gx + dx, gy + dy))
                if g is not None:
                    tot += sum(g) / (SIDE * SIDE)
                    cnt += 1
        return tot / cnt if cnt else 0.0

    rows = [([1.0, p["rel"] / 10000.0, p["snow"]],
             {n: around(p["x"], p["y"], n) for n in order}, p["T"]) for p in pts]
    base_rows = [(f, t) for f, _, t in rows]
    coef, _ = F.least_squares(base_rows, ["c", "relief", "snow"])

    def rms(data, c):
        return math.sqrt(sum((t - sum(a * b for a, b in zip(c, f))) ** 2
                             for f, t in data) / len(data))

    print("\n%d control points on %s; relief + snow alone: rms %.2f"
          % (len(rows), sheet, rms(base_rows, coef)))
    print("   %-15s %13s %9s   %s" % ("family", "rms residual", "coef",
                                       "points with > 0.2 under them"))
    for n in order:
        hit = sum(1 for _, fa, _ in rows if fa[n] > 0.2)
        if not hit:
            continue        # nothing to learn it from; the fit would be noise
        data = [(f + [fa[n]], t) for f, fa, t in rows]
        c, _ = F.least_squares(data, ["c", "relief", "snow", n])
        if c is None:
            continue
        print("   %-15s %13.2f %+9.2f   %d" % (n, rms(data, c), c[3], hit))


if __name__ == "__main__":
    main()
