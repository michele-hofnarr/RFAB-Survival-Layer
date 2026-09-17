#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bake the climate into the grid the plugin reads. Skyrim and Solstheim.

Offline. Reads docs/control_points.xlsx, Skyrim.esm and Dragonborn.esm, writes

    SKSE/Plugins/_RSL_Climate.bin

Draw it with tools/draw_baked.py - that reads the file back rather than the
model, so the picture is of what shipped.

    python tools/bake_climate.py

THE MODEL, and why it has this shape.

    T = C + a * relief + b * snow + residual(x, y)

  RELIEF is how far a point stands above the land around it - not its altitude.
  Absolute height was measured and dropped: fit_climate.py priced it at four
  thousandths of R-squared for a whole parameter. Riften stands high and is
  warm; a pass stands low and is not. What decides is top or bottom.

  SNOW is the ground texture the artists painted, 0..1, from the ATXT/VTXT
  layers of every LAND record. It is the one input that knows a particular
  place rather than a region of them.

  THE RESIDUAL is what the formula cannot explain at each hand-set point,
  spread outward by inverse distance squared. The field passes exactly through
  every temperature in the workbook.

  NO REGION TERM, and that is the point of the whole exercise. A region is a
  polygon, so its value lands as a slab with a straight edge - the same defect
  as the hand-drawn rectangles it would have replaced. And it explains the same
  thing snow does: with regions in the fit snow came out at +0.86 (nonsense -
  "snow warms"), without them at -8.95.

SOLSTHEIM IS PROVISIONAL, and knowingly so. Its worldspace has its own
coordinates, so Skyrim's control points say nothing about any place in it - the
residual field cannot cross over and does not. What crosses is the formula
itself: the same three coefficients, applied to Solstheim's own terrain and its
own snow. That is a guess with a shape rather than a flat number, which is
better than what it replaces, and worse than the control points it is waiting
for. Its snow map (docs/img/map_solstheim_snow.png) says why it will need them:
snow covers the north-west and the spine and nothing else, so the ash wastes of
the south have no signal at all.

THE FILE. Little-endian throughout.

    char   magic[8]   "RSLCLIM2"
    uint32 version
    uint32 blocks
    per block:
      char   tag[8]                     "TAMRIEL" / "SOLSTH"
      int32  minX, minY, width, height  the cell grid this covers
      uint32 cells                      how many of those have terrain
      int32  index[width * height]      slot of that cell, or -1
      int16  data[cells][33 * 33]       temperature in hundredths of a degree

33 x 33 per cell is Skyrim's own height grid: a vertex every 128 units. The
plugin reads it by index, not by search - the grid is regular, so the cell and
the triangle within it are arithmetic.
"""
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT.parent.parent.parent / "Data"
ASSET = ROOT / "SKSE" / "Plugins" / "_RSL_Climate.bin"

sys.path.insert(0, str(ROOT / "tools"))
import esm                     # noqa: E402
import fit_climate as F         # noqa: E402
import snow_field               # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

CELL = 4096.0
SIDE = 33
PER = SIDE * SIDE
AROUND = 4                     # cells each way when measuring relief
RESID_STEP = 512.0             # the residual field is smooth; this is plenty

# tag in the file, plugin, worldspace, the workbook sheet its points are on
WORLDS = [
    ("TAMRIEL", "Skyrim.esm", 0x0000003C, "Skyrim"),
    ("SOLSTH", "Dragonborn.esm", 0x02000800, "Solstheim"),
]

STOPS = [(0.00, (10, 16, 66)), (0.18, (28, 62, 150)), (0.36, (72, 140, 210)),
         (0.50, (176, 214, 238)), (0.60, (242, 236, 206)),
         (0.74, (243, 190, 96)), (0.88, (226, 106, 46)), (1.00, (150, 22, 18))]


def ramp(t, lo, hi):
    """Stretched over whatever a field turns out to span, so nothing saturates
    and every anomaly stays visible. Used by tools/draw_baked.py."""
    f = 0.0 if hi <= lo else (t - lo) / (hi - lo)
    f = 0.0 if f < 0.0 else (1.0 if f > 1.0 else f)
    for i in range(len(STOPS) - 1):
        a, ca = STOPS[i]
        b, cb = STOPS[i + 1]
        if a <= f <= b:
            k = (f - a) / (b - a)
            return (int(ca[0] + (cb[0] - ca[0]) * k),
                    int(ca[1] + (cb[1] - ca[1]) * k),
                    int(ca[2] + (cb[2] - ca[2]) * k))
    return STOPS[-1][1]


def load_points():
    """{sheet: [points]} - every row that has both a protection and a bar.

    A sheet per worldspace, and that division matters: Solstheim has its own
    coordinates, so a control point in Skyrim says nothing about a place there.
    The trend is fitted over all of them together, because the physics does not
    change between worldspaces; the residuals are spread only within the
    worldspace they were measured in.
    """
    from openpyxl import load_workbook
    wb = load_workbook(F.BOOK)
    out = {}
    for sheet in wb.sheetnames:
        if sheet == "Справка":
            continue
        ws = wb[sheet]
        # By header, never by position: the columns have moved before and
        # will again, and an answer read off the wrong one is not an error
        # anybody sees.
        head = [ws.cell(1, c).value for c in range(1, ws.max_column + 1)]
        col = {h: c + 1 for c, h in enumerate(head) if h}
        need = ["место", "X", "Y", "превышение", "снег", "защита",
                "равновесие"] + F.FAMILIES
        missing = [h for h in need if h not in col]
        if missing:
            raise SystemExit("%s: sheet %s has no column %s - regenerate the "
                             "workbook with tools/control_points.py --xlsx"
                             % (F.BOOK.name, sheet, ", ".join(missing)))
        get = lambda i, h: ws.cell(i, col[h]).value
        pts = []
        for i in range(2, ws.max_row + 1):
            prot, bar = get(i, "защита"), get(i, "равновесие")
            if prot is None or bar is None:
                continue
            pts.append({"name": get(i, "место"),
                        "x": float(get(i, "X")), "y": float(get(i, "Y")),
                        "rel": float(get(i, "превышение")),
                        "snow": float(get(i, "снег")),
                        "fams": [float(get(i, n) or 0.0) for n in F.FAMILIES],
                        "T": F.temperature(prot, float(bar))})
        out[sheet] = pts
    return out


def snow_textures():
    ids = set()
    for plugin in ("Skyrim.esm", "Dragonborn.esm"):
        buf = (DATA / plugin).read_bytes()
        groups = esm.top_groups(buf)
        if b"LTEX" in groups:
            ids |= esm.snow_textures(buf, groups)
        del buf
    return ids


def family_textures():
    """{family: LTEX ids}, over both plugins, for fit_climate.FAMILIES."""
    ids = {n: set() for n in F.FAMILIES}
    for plugin in ("Skyrim.esm", "Dragonborn.esm"):
        buf = (DATA / plugin).read_bytes()
        groups = esm.top_groups(buf)
        for n in F.FAMILIES:
            ids[n] |= esm.textures_named(buf, groups, n)
        del buf
    return ids


def ice_statics():
    ids = {}
    for plugin in ("Skyrim.esm", "Dragonborn.esm"):
        buf = (DATA / plugin).read_bytes()
        groups = esm.top_groups(buf)
        if b"STAT" in groups:
            ids.update(esm.ice_statics(buf, groups))
        del buf
    return ids


def block(tag, plugin, worldspace, resid, trend, snowids, iceids, famids):
    """Bake one worldspace and return its bytes."""
    print("  %s, from %s" % (tag, plugin))
    buf = (DATA / plugin).read_bytes()
    groups = esm.top_groups(buf)
    was = esm.TAMRIEL
    esm.TAMRIEL = worldspace
    try:
        heights, snow, fams = esm.land(buf, groups, snowids, iceids, famids)
    finally:
        esm.TAMRIEL = was
    del buf
    print("    %d cells with terrain" % len(heights))
    snow = snow_field.smooth(snow)
    fams = {n: snow_field.smooth(g) for n, g in fams.items()}
    if fams:
        print("    families here: %s" % ", ".join(sorted(fams)))
    print("    snow smoothed, radius %d vertices x %d passes"
          % (snow_field.RADIUS, snow_field.PASSES))

    order = sorted(heights)
    gx0 = min(k[0] for k in order)
    gx1 = max(k[0] for k in order)
    gy0 = min(k[1] for k in order)
    gy1 = max(k[1] for k in order)
    W, H = gx1 - gx0 + 1, gy1 - gy0 + 1

    cellmean = {k: sum(hs[i] for i in range(0, PER, 37)) / 30
                for k, hs in heights.items()}
    near = {}
    for (gx, gy) in order:
        total = n = 0.0
        for dy in range(-AROUND, AROUND + 1):
            for dx in range(-AROUND, AROUND + 1):
                m = cellmean.get((gx + dx, gy + dy))
                if m is not None:
                    total += m
                    n += 1
        near[(gx, gy)] = total / n if n else 0.0

    # The residual field, on a coarse grid; smooth by construction, so reading
    # it back bilinearly loses nothing.
    rgrid = None
    if resid:
        wx0, wy0 = gx0 * CELL, gy0 * CELL
        rw = int((gx1 + 1) * CELL - wx0) // int(RESID_STEP) + 1
        rh = int((gy1 + 1) * CELL - wy0) // int(RESID_STEP) + 1
        rgrid = [0.0] * (rw * rh)
        for j in range(rh):
            y = wy0 + j * RESID_STEP
            row = j * rw
            for i in range(rw):
                x = wx0 + i * RESID_STEP
                num = den = 0.0
                for rx, ry, rv in resid:
                    w = 1.0 / ((x - rx) ** 2 + (y - ry) ** 2 + 1.0)
                    num += w * rv
                    den += w
                rgrid[row + i] = num / den

        def residual_at(x, y):
            fx = (x - wx0) / RESID_STEP
            fy = (y - wy0) / RESID_STEP
            i = min(rw - 2, max(0, int(fx)))
            j = min(rh - 2, max(0, int(fy)))
            u, v = fx - i, fy - j
            a = rgrid[j * rw + i]
            b = rgrid[j * rw + i + 1]
            c = rgrid[(j + 1) * rw + i]
            d = rgrid[(j + 1) * rw + i + 1]
            return (a * (1 - u) * (1 - v) + b * u * (1 - v)
                    + c * (1 - u) * v + d * u * v)
    else:
        def residual_at(x, y):
            return 0.0

    index = [-1] * (W * H)
    for slot, (gx, gy) in enumerate(order):
        index[(gy - gy0) * W + (gx - gx0)] = slot

    out = bytearray()
    out += struct.pack("<8s", tag.encode())
    out += struct.pack("<iiiiI", gx0, gy0, W, H, len(order))
    out += struct.pack("<%di" % (W * H), *index)

    lo = hi = None
    for n, (gx, gy) in enumerate(order):
        hs = heights[(gx, gy)]
        cover = snow[(gx, gy)]
        base = near[(gx, gy)]
        # a family this worldspace does not paint is zero everywhere
        here = [fams[n][(gx, gy)] if n in fams else None for n in F.FAMILIES]
        ox, oy = gx * CELL, gy * CELL
        vals = []
        for j in range(SIDE):
            y = oy + j * 128.0
            for i in range(SIDE):
                k = j * SIDE + i
                t = trend(hs[k] - base, cover[k],
                          [g[k] if g is not None else 0.0 for g in here]) \
                    + residual_at(ox + i * 128.0, y)
                lo = t if lo is None or t < lo else lo
                hi = t if hi is None or t > hi else hi
                vals.append(max(-32000, min(32000, int(round(t * 100.0)))))
        out += struct.pack("<%dh" % PER, *vals)
        if (n + 1) % 5000 == 0:
            print("    %d / %d cells" % (n + 1, len(order)))

    print("    grid %dx%d from (%d, %d), field %+.1f .. %+.1f%s"
          % (W, H, gx0, gy0, lo, hi,
             "" if resid else "  (no control points here - formula only)"))
    return out


def main():
    sheets = load_points()
    everything = [p for pts in sheets.values() for p in pts]
    if not everything:
        raise SystemExit("no control points in the workbook - nothing to fit")
    print("control points: %s"
          % ", ".join("%s %d" % (k, len(v)) for k, v in sheets.items()))
    print("  %+.1f .. %+.1f degrees"
          % (min(p["T"] for p in everything), max(p["T"] for p in everything)))

    names = ["intercept", "relief", "snow"] + F.FAMILIES
    coef, _ = F.least_squares(
        [([1.0, p["rel"] / 10000.0, p["snow"]] + p["fams"], p["T"])
         for p in everything], names)
    if coef is None:
        raise SystemExit("the fit is singular - a covariate is constant over "
                         "every control point")
    C0, CR, CS = coef[:3]
    CF = coef[3:]
    print("  T = %+.3f %+.3f * relief/10000 %+.3f * snow %s"
          % (C0, CR, CS, " ".join("%+.3f * %s" % (c, n)
                                    for c, n in zip(CF, F.FAMILIES))))
    rms = math.sqrt(sum(
        (p["T"] - sum(c * f for c, f in zip(
            coef, [1.0, p["rel"] / 10000.0, p["snow"]] + p["fams"]))) ** 2
        for p in everything) / len(everything))
    print("  rms residual %.2f over %d points" % (rms, len(everything)))

    def trend(rel, sc, fams):
        return (C0 + CR * rel / 10000.0 + CS * sc
                + sum(c * f for c, f in zip(CF, fams)))

    resid = {sheet: [(p["x"], p["y"],
                      p["T"] - trend(p["rel"], p["snow"], p["fams"]))
                     for p in pts]
             for sheet, pts in sheets.items()}
    for sheet, rs in resid.items():
        if rs:
            print("  %s residuals %+.1f .. %+.1f"
                  % (sheet, min(r[2] for r in rs), max(r[2] for r in rs)))

    snowids = snow_textures()
    iceids = ice_statics()
    famids = family_textures()
    print("  %d ground textures named as snow, %d ice records, families %s"
          % (len(snowids), len(iceids),
             ", ".join("%s %d" % (n, len(v)) for n, v in famids.items())))

    print("baking ...")
    blocks = [block(tag, plugin, ws, resid.get(sheet, []), trend,
                    snowids, iceids, famids)
              for tag, plugin, ws, sheet in WORLDS]

    blob = bytearray()
    blob += b"RSLCLIM2"
    blob += struct.pack("<II", 2, len(blocks))
    for b in blocks:
        blob += b
    ASSET.parent.mkdir(parents=True, exist_ok=True)
    ASSET.write_bytes(bytes(blob))
    print("wrote %s, %.1f MB" % (ASSET, len(blob) / 1048576.0))
    print("draw it with:  python tools/draw_baked.py")


if __name__ == "__main__":
    main()
