#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check that the snow covariate blur does what it says.

    python tools/test_snow_field.py

Needs nothing but the module, so it runs in a second and without the game's
data. Worth having: the blur is the one piece of index arithmetic in these
tools that is easy to get quietly wrong, and it is quietly wrong in a way that
would show up as a stripe of ice across the map three hours into a bake. The
first run of this caught exactly that - the vertex row shared between two rows
of cells was written by the upper cell and never by the lower one.
"""
import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import snow_field as S     # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

SIDE = S.SIDE
FAILED = []


def check(what, ok):
    print("  %-58s %s" % (what, "ok" if ok else "FAILED"))
    if not ok:
        FAILED.append(what)


def field(cells, value=1.0):
    return {k: array("f", [value] * (SIDE * SIDE)) for k in cells}


def main():
    print("a field of nothing but snow, 3x3 cells")
    out = S.smooth(field([(gx, gy) for gx in range(3) for gy in range(3)]))
    lo = min(min(v) for v in out.values())
    hi = max(max(v) for v in out.values())
    check("stays 1.0 everywhere, seams and edges included (%.6f .. %.6f)"
          % (lo, hi), abs(lo - 1.0) < 1e-6 and abs(hi - 1.0) < 1e-6)

    print("a field with no snow anywhere")
    out = S.smooth(field([(gx, gy) for gx in range(3) for gy in range(3)], 0.0))
    check("stays 0.0", max(max(v) for v in out.values()) == 0.0)

    print("snow over the west half of 4x1 cells, bare over the east")
    half = {(gx, 0): array("f", [1.0 if gx < 2 else 0.0] * (SIDE * SIDE))
            for gx in range(4)}
    out = S.smooth(half)
    line = [out[(gx, 0)][16 * SIDE + i]
            for gx in range(4) for i in range(S.STRIDE)]
    check("never leaves 0..1 (%.4f .. %.4f)" % (min(line), max(line)),
          -1e-6 <= min(line) and max(line) <= 1.0 + 1e-6)
    check("falls the whole way and never climbs back",
          all(line[i] >= line[i + 1] - 1e-6 for i in range(len(line) - 1)))
    # The seam lies between two vertices, so the vertex at it sits just on the
    # bare side and reads a shade under a half. 6/13 is exactly right for it.
    seam = 2 * S.STRIDE
    check("about half way across at the seam (%.4f)" % line[seam],
          abs(line[seam] - 0.5) < 0.05)
    ramp = sum(1 for v in line if 1e-4 < v < 1.0 - 1e-4)
    check("the ramp is %d vertices wide, against 4 * radius = %d"
          % (ramp, 4 * S.RADIUS), abs(ramp - 4 * S.RADIUS) <= 2)

    print("a clearing of bare ground the size of Ingol's, seven vertices")
    one = field([(0, 0)])
    for j in range(13, 20):
        for i in range(13, 20):
            one[(0, 0)][j * SIDE + i] = 0.0
    out = S.smooth(one)
    middle = out[(0, 0)][16 * SIDE + 16]
    check("its middle reads %.2f rather than 0.00 - no cliff left" % middle,
          middle > 0.5)

    print("a cell with no terrain in the middle of nine")
    gap = field([(gx, gy) for gx in range(3) for gy in range(3)
                 if (gx, gy) != (1, 1)])
    out = S.smooth(gap)
    lo = min(min(v) for v in out.values())
    check("the land around it still reads 1.0, not diluted (%.6f)" % lo,
          abs(lo - 1.0) < 1e-6)
    check("and the gap is not invented into the output", (1, 1) not in out)

    print()
    if FAILED:
        print("%d FAILED" % len(FAILED))
        return 1
    print("all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
