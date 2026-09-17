#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fit the baking formula to the hand-set control points.

Offline.

    python tools/fit_climate.py

WHAT IT DOES. docs/control_points.xlsx says what the cold bar should settle at
in a named place, with a named amount of clothing on. That inverts to a
temperature (see the Справка sheet). This reads those, and finds the formula
that comes closest to all of them at once:

    T = base(region) + a * height + b * relief + c * snow

Least squares - the coefficients are not chosen, they are the ones that make
the total squared error smallest. Then the leftover at each point is reported,
because that is what the residual spreading will have to carry, and a big one
is either an anomaly on purpose or a number worth a second look.

WHY NOT JUST BAKE THE POINTS. Thirty points cannot cover a province. The
formula carries the shape between them and the residuals carry the rest.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

BOOK = ROOT / "docs" / "control_points.xlsx"

# Settings.h, the same numbers the workbook's own formula uses.
WARMTH = {
    "голым": 0.0,
    "4 слота, без резиста": 58.68,
    "4 слота, резист 25%": 79.13,
    "4 слота, резист 50%": 99.58,
    "4 слота, резист 75%": 120.03,
    "4 слота, резист 100%": 140.48,
}
COMFORT = 12.0
LOAD_PER_DEGREE = 0.055

# Places that are not among Skyrim.esm's own map markers - Castle Volkihar is
# Dawnguard's - used to live here. They are rows of the workbook now: one sheet,
# one truth, and nothing answered in two places at once.
EXTRA = []

# Ground-texture families that carry a temperature of their own, on top of
# what snow says. Bethesda named the province's textures after its regions -
# LFallForest*, LPineForest*, LVolcanicTundra* - so a family is the word the
# editor ids share. Each one is a column of the workbook and a term of the
# trend, in this order. Measured exactly as snow is: painted per vertex and
# blurred with the same radius, so a control point is read the way the field
# around it is built.
#
# Only the three whose sign is beyond argument: the Rift's autumn forest, the
# pine forest of Falkreath and the hot springs of Eastmarch. The Reach, the
# tundra and the field grass were tried and left out - two textures paint
# the same plains and the fit split hairs between them on two points each.
#
# The word has to be the whole of "volcanictundra", not "volcanic":
# Dragonborn names Solstheim's ash wastes LVolcanicAsh*, and the shorter
# word put four of its points - cold ones - under the hot-spring family,
# which took the springs from +30 to +8.
#
# A FAMILY BELONGS TO A WORLDSPACE. Each entry is (column, word, worldspace):
# the column is measured only in that worldspace and is zero everywhere
# else, so its coefficient is decided by that worldspace's points alone.
# Solstheim uses Skyrim's LPineForest textures for its own woods, and with
# one shared pine term Kagrumez was being warmed by Falkreath: +17 for a
# forest that is -10.7 in the workbook. Its ash was tried as a family and
# adds nothing (-1.4) - no snow already says where its south is. What it
# does need is snow of its own: the island's north is 6.5 degrees colder
# than Skyrim's snow coefficient makes it, and one extra term takes its
# nine points from rms 5.97 to 3.06.
TAMRIEL, SOLSTHEIM = 0x0000003C, 0x02000800
FAMILIES = [
    ("fallforest",     "fallforest",     TAMRIEL),
    ("pineforest",     "pineforest",     TAMRIEL),
    ("volcanictundra", "volcanictundra", TAMRIEL),
    ("snow_solstheim", "snow",           SOLSTHEIM),
]
FAMILY_NAMES = [f[0] for f in FAMILIES]


def families_for(worldspace, ids):
    """{column: LTEX ids} for the families measured in this worldspace.

    `ids` is {column: ids} over every plugin - the texture a family is
    painted with may belong to Skyrim.esm even on Solstheim."""
    return {name: ids[name] for name, _, ws in FAMILIES
            if ws == worldspace and ids.get(name)}


def temperature(protection, bar):
    """The workbook's inversion: a wanted bar reading becomes degrees."""
    return COMFORT - (1.0 - bar + WARMTH[protection] / 100.0) / LOAD_PER_DEGREE


def solve(a, b):
    """Gaussian elimination with partial pivoting. a is n x n, b is n."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-12:
            return None
        m[col], m[piv] = m[piv], m[col]
        p = m[col][col]
        for r in range(n):
            if r == col:
                continue
            f = m[r][col] / p
            if f == 0.0:
                continue
            for c in range(col, n + 1):
                m[r][c] -= f * m[col][c]
    return [m[i][n] / m[i][i] for i in range(n)]


def least_squares(rows, columns):
    """rows: [(covariates, y)]. Returns (coefficients, fitted values)."""
    n = len(columns)
    ata = [[0.0] * n for _ in range(n)]
    atb = [0.0] * n
    for xs, y in rows:
        for i in range(n):
            atb[i] += xs[i] * y
            for j in range(n):
                ata[i][j] += xs[i] * xs[j]
    coef = solve(ata, atb)
    if coef is None:
        return None, None
    fit = [sum(c * x for c, x in zip(coef, xs)) for xs, _ in rows]
    return coef, fit


def report(title, points, columns, build):
    rows = [(build(p), p["T"]) for p in points]
    coef, fit = least_squares(rows, columns)
    print()
    print("=" * 78)
    print(title)
    print("=" * 78)
    if coef is None:
        print("  the columns are not independent - cannot solve")
        return None
    resid = [p["T"] - f for p, f in zip(points, fit)]
    ss_res = sum(r * r for r in resid)
    mean = sum(p["T"] for p in points) / len(points)
    ss_tot = sum((p["T"] - mean) ** 2 for p in points)
    print("  %-26s %10s" % ("term", "value"))
    for name, c in zip(columns, coef):
        print("  %-26s %+10.3f" % (name, c))
    print()
    print("  points %d, parameters %d" % (len(points), len(columns)))
    print("  R2 %.3f,  worst miss %.1f deg,  average miss %.1f deg"
          % (1.0 - ss_res / ss_tot if ss_tot else 0.0,
             max(abs(r) for r in resid),
             sum(abs(r) for r in resid) / len(resid)))
    print()
    print("  %-30s %7s %7s %7s" % ("", "wanted", "formula", "left"))
    for p, f, r in sorted(zip(points, fit, resid), key=lambda t: -abs(t[2])):
        print("  %-30s %+7.1f %+7.1f %+7.1f" % (p["name"][:30], p["T"], f, r))
    return coef


def main():
    from openpyxl import load_workbook
    wb = load_workbook(BOOK)

    # A sheet per worldspace. The physics is the same in both, so the fit is
    # over all of them at once; only the residual spreading is per worldspace,
    # and that is bake_climate.py's job.
    points = []
    for sheet in wb.sheetnames:
        if sheet == "Справка":
            continue
        ws = wb[sheet]
        for i in range(2, ws.max_row + 1):
            v = [ws.cell(i, c).value for c in range(1, 11)]
            if v[7] is None or v[8] is None:
                continue
            points.append({
                "name": v[0], "world": sheet, "x": float(v[1]),
                "y": float(v[2]), "z": float(v[3]), "rel": float(v[4]),
                "snow": float(v[5]), "region": v[6],
                "T": temperature(v[7], float(v[8])),
            })
    for name, x, y, z, rel, snow, region, prot, bar in EXTRA:
        points.append({"name": name, "x": x, "y": y, "z": z, "rel": rel,
                       "snow": snow, "region": region,
                       "T": temperature(prot, bar)})

    regions = sorted({p["region"] for p in points})
    print("%d control points, %d regions" % (len(points), len(regions)))
    counts = {r: sum(1 for p in points if p["region"] == r) for r in regions}
    print("  " + ", ".join("%s %d" % (r, counts[r]) for r in regions))
    print("  temperatures from %+.1f to %+.1f"
          % (min(p["T"] for p in points), max(p["T"] for p in points)))

    # Heights are in tens of thousands of units; scaling keeps the normal
    # equations well conditioned and makes the coefficients readable as
    # degrees per 10000 units.
    def cont(p):
        return [p["z"] / 10000.0, p["rel"] / 10000.0, p["snow"]]

    report("1. one intercept, height, relief, snow - no regions at all",
           points, ["intercept", "height /10k", "relief /10k", "snow"],
           lambda p: [1.0] + cont(p))

    report("2. a value per region, nothing else",
           points, regions,
           lambda p: [1.0 if r == p["region"] else 0.0 for r in regions])

    report("3. a value per region, plus height, relief and snow",
           points, regions + ["height /10k", "relief /10k", "snow"],
           lambda p: [1.0 if r == p["region"] else 0.0 for r in regions] + cont(p))

    report("4. a value per region, plus relief and snow - no absolute height",
           points, regions + ["relief /10k", "snow"],
           lambda p: [1.0 if r == p["region"] else 0.0 for r in regions]
           + [p["rel"] / 10000.0, p["snow"]])


if __name__ == "__main__":
    main()
