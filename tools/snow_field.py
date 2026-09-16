#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Turn the painted snow texture into a snow climate covariate.

Used by tools/control_points.py when it measures a place and by
tools/bake_climate.py when it bakes the field, so both answer the same question
about the same spot.

WHY THIS EXISTS. The snow a LAND record carries is an ATXT/VTXT alpha at every
vertex - a decision an artist made about one 128-unit square of ground. Read
straight, it says "is this square metre painted with snow", and into that
answer go swept courtyards, footpaths, bare rock and the dither along a texture
seam. The model wants the other question, "how snowy is this place", and it
prices snow at about ten degrees. Ingol's Barrow is what happens when the two
get confused: its clearing is nine hundred units of bare ground inside a snow
region, and the baked field fell 10.6 degrees across a single 128-unit step at
its edge - one stride.

Nothing is hidden by smoothing it. The air over a swept courtyard is not warmer
than the air five paces away, and a field that says otherwise is wrong about
the courtyard rather than right about the snow.

HOW. A box mean of RADIUS vertices, PASSES times; two passes of a box make a
triangular kernel, which leaves no plateaus of its own behind. It runs over the
whole worldspace at once rather than cell by cell, because a blur that stopped
at a cell border would trade the cliff at the courtyard for a seam every 4096
units. Cells with no terrain cast no vote: the values and the votes are blurred
together and divided at the end, so a coastline averages the land it has rather
than the sea it has not.

RADIUS is the one number worth tuning, and it is a length: 16 vertices is 2048
units, and two passes build a triangle reaching twice that - a whole cell.

It started at 8, picked against Ingol's clearing, which is a wide one at seven
vertices: 4 leaves a visible dip in it, 6 nearly closes it, 8 closes it. But a
clearing is not the widest thing the painting gets wrong. A watercourse is
painted bare for its whole length, bed and banks together, and a corridor is not
a patch - at 8 the blur reached across the water and not across the bare margin
beside it, and the baked field ran a warm ribbon down every river in the north.
Measured over the 1298 cells with a real river bed in them, a riverbed vertex
came out +0.63 warmer than the rest of its own cell, and up to +9.9.

One cell is still far under the scale a real snow line works at, which is tens
of cells, so what goes is the painting and not the climate.
"""
from array import array
from collections import deque
from itertools import accumulate
from operator import add, sub

SIDE = 33
STRIDE = SIDE - 1          # vertices a cell advances; 33 overlaps by one

RADIUS = 16                # vertices each way in one pass; 2048 units
PASSES = 2                 # two boxes make a triangle


def rows_at(y, gy0, gy1):
    """Which (cell row, j within it) the global vertex row y is made of.

    A cell's j=32 is its neighbour's j=0 - one vertex written by both. Later
    entries overwrite earlier ones, so along a coast the cell that exists wins
    over the cell that does not, whichever side of the seam it is on.
    """
    gy = gy0 + y // STRIDE
    j = y % STRIDE
    out = []
    if j == 0 and gy > gy0:
        out.append((gy - 1, STRIDE))     # the row above the seam, written down
    if gy <= gy1:
        out.append((gy, j))
    return out


def box_pass(rows, width, height, radius):
    """A sliding row-window sum over the stream of rows that rows(y) yields.

    Sums, not means: values and votes travel together and the division happens
    once at the very end, which is what makes a missing cell an absence rather
    than a zero.

    Rows are asked for in order and only 2 * radius + 1 of them are held at
    once. The grids run to six thousand vertices a side, so materialising one
    to transpose it is not worth it.
    """
    span = 2 * radius + 1
    pad = array("d", bytes(8 * radius))
    blank = array("d", bytes(8 * width))
    value = array("d", blank)
    vote = array("d", blank)
    window = deque()
    edge = [-1, 0, -1]        # last y answered, oldest row in the sum, rows fed

    def across(row):
        wide = pad + row + pad
        run = array("d", accumulate(wide, initial=0.0))
        return array("d", map(sub, run[span:], run[:width]))

    def step(y):
        if y == edge[0]:
            return value, vote
        if y != edge[0] + 1:
            raise AssertionError("rows are read in order; asked %d after %d"
                                 % (y, edge[0]))
        while edge[2] <= y + radius:
            k = edge[2]
            edge[2] += 1
            if k >= height:
                window.append(None)
                continue
            near, cast = rows(k)
            near, cast = across(near), across(cast)
            window.append((near, cast))
            value[:] = array("d", map(add, value, near))
            vote[:] = array("d", map(add, vote, cast))
        while edge[1] < y - radius:
            gone = window.popleft()
            edge[1] += 1
            if gone is None:
                continue
            value[:] = array("d", map(sub, value, gone[0]))
            vote[:] = array("d", map(sub, vote, gone[1]))
        edge[0] = y
        return value, vote

    return step


def smooth(snow, radius=RADIUS, passes=PASSES):
    """{(gx, gy): 33x33 coverage} -> the same, as a climate covariate.

    The input is left as it was.
    """
    keys = list(snow)
    if not keys or radius <= 0 or passes <= 0:
        return {k: array("f", snow[k]) for k in keys}

    gx0 = min(k[0] for k in keys)
    gx1 = max(k[0] for k in keys)
    gy0 = min(k[1] for k in keys)
    gy1 = max(k[1] for k in keys)
    width = (gx1 - gx0) * STRIDE + SIDE
    height = (gy1 - gy0) * STRIDE + SIDE

    out = {k: array("f", bytes(4 * SIDE * SIDE)) for k in keys}
    ones = array("d", [1.0] * SIDE)
    blank = array("d", bytes(8 * width))

    def read(y):
        """One global row of painted coverage, and where it had terrain."""
        near = array("d", blank)
        cast = array("d", blank)
        for gy, j in rows_at(y, gy0, gy1):
            for gx in range(gx0, gx1 + 1):
                cover = snow.get((gx, gy))
                if cover is None:
                    continue
                at = (gx - gx0) * STRIDE
                near[at:at + SIDE] = array("d", cover[j * SIDE:(j + 1) * SIDE])
                cast[at:at + SIDE] = ones
        return near, cast

    rows = read
    for _ in range(passes):
        rows = box_pass(rows, width, height, radius)

    for y in range(height):
        near, cast = rows(y)
        for gy, j in rows_at(y, gy0, gy1):
            for gx in range(gx0, gx1 + 1):
                cover = out.get((gx, gy))
                if cover is None:
                    continue
                at = (gx - gx0) * STRIDE
                cover[j * SIDE:(j + 1) * SIDE] = array("f", [
                    near[at + c] / cast[at + c] if cast[at + c] > 0.0 else 0.0
                    for c in range(SIDE)])
    return out
