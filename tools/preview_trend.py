#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""What the baked map would look like under other sets of covariates.

    python tools/preview_trend.py            # Tamriel
    python tools/preview_trend.py SOLSTH     # Solstheim

Every option in OPTIONS is fitted on the worldspace's own control points -
intercept, relief and snow plus the covariates listed - and drawn as
trend + the same inverse-distance residual field the bake builds. All the
maps share ONE colour scale, the first option's, so they can be read against
each other; a map that stretches the scale reads colder everywhere, and that
is not a change in the model.

Covariates are texture-family words (measured as snow is: painted per vertex,
blurred with snow_field's radius) or the coordinates "x" and "y", fed in
units of 100k so their coefficients read as degrees per ~24 cells.

This fits one worldspace on its own, where the bake fits both together with
the base shared, so the numbers are close to the bake's and not the bake's.
"""
import array
import io
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import esm                     # noqa: E402
import snow_field              # noqa: E402
import bake_climate as B       # noqa: E402
import fit_climate as F        # noqa: E402
import mesh_clouds as M        # noqa: E402
import draw_families as D      # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DATA = B.DATA
OUT = ROOT / "docs" / "img"
CELL, SIDE, PER, STEP = 4096.0, 33, 33 * 33, 2
PER_CELL = 32 // STEP
AROUND, RESID_STEP, COORD = 4, 512.0, 100000.0

OPTIONS = {
    "base":     [],
    "families": ["fallforest", "pineforest", "volcanictundra"],
    "lat":      ["fallforest", "pineforest", "volcanictundra", "y"],
}


def main():
    tag = sys.argv[1].upper() if len(sys.argv) > 1 else "TAMRIEL"
    plugin, worldspace = M.world(tag)
    sheet = next(w[3] for w in B.WORLDS if w[0] == tag)
    suffix = "" if tag == "TAMRIEL" else "_" + tag.lower()
    words = sorted({w for use in OPTIONS.values() for w in use if w not in ("x", "y")})

    famids = {w: set() for w in words}
    snowids, iceids = set(), {}
    for name in ("Skyrim.esm", plugin):
        buf = (DATA / name).read_bytes()
        groups = esm.top_groups(buf)
        snowids |= esm.snow_textures(buf, groups)
        iceids.update(esm.ice_statics(buf, groups))
        for w in words:
            famids[w] |= esm.textures_named(buf, groups, w)
        del buf

    buf = (DATA / plugin).read_bytes()
    groups = esm.top_groups(buf)
    refs, _ = M.world_refs(buf, groups, worldspace)
    was = esm.TAMRIEL
    esm.TAMRIEL = worldspace
    try:
        heights, snow, fams = esm.land(buf, groups, snowids, iceids, famids)
    finally:
        esm.TAMRIEL = was
    del buf
    print("%s: %d cells; smoothing snow and %s ..." % (tag, len(heights), ", ".join(fams)))
    snow = snow_field.smooth(snow)
    fields = {w: snow_field.smooth(g) for w, g in fams.items()}

    cellmean = {k: sum(hs[i] for i in range(0, PER, 37)) / 30 for k, hs in heights.items()}
    near = {}
    for (gx, gy) in heights:
        tot = n = 0.0
        for dy in range(-AROUND, AROUND + 1):
            for dx in range(-AROUND, AROUND + 1):
                m = cellmean.get((gx + dx, gy + dy))
                if m is not None:
                    tot += m
                    n += 1
        near[(gx, gy)] = tot / n if n else 0.0

    def covariate(w, cell, k, x, y):
        if w == "x":
            return x / COORD
        if w == "y":
            return y / COORD
        g = fields.get(w)
        return g[cell][k] if g and cell in g else 0.0

    def at(w, x, y):
        cell = (int(x // CELL), int(y // CELL))
        i = min(32, max(0, int((x - cell[0] * CELL) / 128.0)))
        j = min(32, max(0, int((y - cell[1] * CELL) / 128.0)))
        return covariate(w, cell, j * SIDE + i, x, y)

    pts = B.load_points().get(sheet, [])
    if not pts:
        raise SystemExit("no control points on sheet %s" % sheet)
    rows = [(p["x"], p["y"], [1.0, p["rel"] / 10000.0, p["snow"]], p["T"]) for p in pts]
    print("%d control points\n" % len(rows))

    fits = {}
    for opt, use in OPTIONS.items():
        data = [(base + [at(w, x, y) for w in use], T) for x, y, base, T in rows]
        coef, _ = F.least_squares(data, ["c", "relief", "snow"] + use)
        if coef is None:
            print("%-10s singular" % opt)
            continue
        resid = [(x, y, T - sum(c * f for c, f in zip(coef, feats)))
                 for (feats, T), (x, y, _, _) in zip(data, rows)]
        rmsr = math.sqrt(sum(r[2] ** 2 for r in resid) / len(resid))
        fits[opt] = (use, coef, resid)
        print("%-10s rms %.2f   %s" % (opt, rmsr, "  ".join(
            "%s %+.2f" % (n, c) for n, c in zip(["c", "relief", "snow"] + use, coef))))

    gx0, gx1, gy0, gy1 = M.window(refs)
    W, H = (gx1 - gx0 + 1) * PER_CELL, (gy1 - gy0 + 1) * PER_CELL
    draw = [k for k in heights if gx0 <= k[0] <= gx1 and gy0 <= k[1] <= gy1]

    def residual_field(resid):
        wx0, wy0 = gx0 * CELL, gy0 * CELL
        rw = int((gx1 + 1) * CELL - wx0) // int(RESID_STEP) + 1
        rh = int((gy1 + 1) * CELL - wy0) // int(RESID_STEP) + 1
        grid = [0.0] * (rw * rh)
        for j in range(rh):
            y = wy0 + j * RESID_STEP
            for i in range(rw):
                x = wx0 + i * RESID_STEP
                num = den = 0.0
                for rx, ry, rv in resid:
                    w = 1.0 / ((x - rx) ** 2 + (y - ry) ** 2 + 1.0)
                    num += w * rv
                    den += w
                grid[j * rw + i] = num / den

        def value(x, y):
            fx, fy = (x - wx0) / RESID_STEP, (y - wy0) / RESID_STEP
            i = min(rw - 2, max(0, int(fx)))
            j = min(rh - 2, max(0, int(fy)))
            u, v = fx - i, fy - j
            a, b = grid[j * rw + i], grid[j * rw + i + 1]
            c, d = grid[(j + 1) * rw + i], grid[(j + 1) * rw + i + 1]
            return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v
        return value

    maps = {}
    for opt, (use, coef, resid) in fits.items():
        print("field %s ..." % opt)
        res = residual_field(resid)
        vals, lo, hi = {}, None, None
        for cell in draw:
            gx, gy = cell
            hs, base = heights[cell], near[cell]
            out = array.array("f", [0.0] * (PER_CELL * PER_CELL))
            for jj in range(PER_CELL):
                j = jj * STEP
                y = gy * CELL + j * 128.0
                for ii in range(PER_CELL):
                    i = ii * STEP
                    k = j * SIDE + i
                    x = gx * CELL + i * 128.0
                    feats = [1.0, (hs[k] - base) / 10000.0, snow[cell][k]]
                    feats += [covariate(w, cell, k, x, y) for w in use]
                    t = sum(c * f for c, f in zip(coef, feats)) + res(x, y)
                    out[jj * PER_CELL + ii] = t
                    lo = t if lo is None or t < lo else lo
                    hi = t if hi is None or t > hi else hi
            vals[cell] = out
        maps[opt] = vals
        print("   %+.1f .. %+.1f" % (lo, hi))
        if opt == next(iter(OPTIONS)):
            LO, HI = lo, hi

    for opt, vals in maps.items():
        def paint(cell, k, vals=vals):
            v = vals.get(cell)
            if v is None:
                return None
            jj, ii = (k // SIDE) // STEP, (k % SIDE) // STEP
            return B.ramp(v[jj * PER_CELL + ii], LO, HI)
        img, W, H = D.hillshade(heights, gx0, gx1, gy0, gy1, paint)
        esm.png(OUT / ("preview_%s%s.png" % (opt, suffix)), W, H, img)
        print("wrote preview_%s%s.png" % (opt, suffix))


if __name__ == "__main__":
    main()
