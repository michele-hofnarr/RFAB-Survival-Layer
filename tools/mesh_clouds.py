#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Every placed object of a worldspace as a point cloud per base record.

    python tools/mesh_clouds.py            # Tamriel, from Skyrim.esm
    python tools/mesh_clouds.py SOLSTH     # Solstheim, from Dragonborn.esm

This is how the texture families were found. Bethesda named its
region-specific dressing after the region - TreeAspen, TreeReachBush,
RockPileS02VolcanicDirt01, TreePineForest - so a mesh that is placed many
times and yet kept to one part of the map is a region saying its name, and a
family is the substring those names share.

Per base record: how often it is placed, in how many cells, its centroid, its
radius of gyration (RMS distance from the centroid) and the area of its convex
hull. Written to docs/refr_cloud.csv for Tamriel and docs/refr_cloud_<tag>.csv
otherwise; the family pooling is printed.

Deleted and initially-disabled references are skipped - cut content and
enable-parent props would otherwise count.
"""
import csv
import io
import math
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import esm                     # noqa: E402
import bake_climate as B       # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DATA = B.DATA
CELL = 4096.0
FLAG_DELETED = 0x00000020
FLAG_INITIALLY_DISABLED = 0x00000800

# The words pooled and printed at the end. Not a model input - the model reads
# fit_climate.FAMILIES - just the list that was looked at.
FAMILY_WORDS = [
    "aspen", "fallforest", "reach", "pineforest", "pine", "fieldgrass",
    "tundra", "volcanic", "marsh", "swamp", "snow", "ice", "coast", "beach",
    "jazbay", "dragonstongue", "deathbell", "creep", "lavender", "juniper",
    "thistle", "mushroom", "dead", "burnt", "moss", "ash", "scathecraw",
    "trama", "glacier",
]


def world(tag):
    """(plugin, worldspace id) for a tag of bake_climate.WORLDS."""
    for t, plugin, ws, _ in B.WORLDS:
        if t == tag:
            return plugin, ws
    raise SystemExit("no such worldspace tag: %s (have %s)"
                     % (tag, ", ".join(w[0] for w in B.WORLDS)))


def base_records(buf, groups):
    """{formid: (signature, editor id)} for every top-level record."""
    out = {}
    for sig, (gp, gs) in groups.items():
        if sig in (b"CELL", b"WRLD", b"DIAL", b"NAVI", b"QUST"):
            continue
        p, e = gp + 24, gp + gs
        while p + 24 <= e:
            if buf[p:p + 4] == b"GRUP":
                g, = struct.unpack_from("<I", buf, p + 4)
                if g < 24:
                    break
                p += g
                continue
            size, flags, fid = struct.unpack_from("<III", buf, p + 4)
            edid = None
            for s, v in esm.subrecords(esm.payload(buf, p, size, flags)):
                if s == b"EDID":
                    edid = v.rstrip(b"\0").decode("cp1252", "replace")
                    break
            out[fid] = (sig.decode(), edid or "")
            p += 24 + size
    return out


def world_refs(buf, groups, worldspace):
    """[(base, x, y)] for every live REFR under the worldspace, and how many
    were dropped as (deleted, initially disabled)."""
    out = []
    dropped = [0, 0]

    def walk(off, end, inside):
        while off + 24 <= end:
            if buf[off:off + 4] == b"GRUP":
                size, label, gtype = struct.unpack_from("<I4si", buf, off + 4)
                if size < 24:
                    return
                sub = inside
                if gtype == 1:
                    sub = struct.unpack_from("<I", label)[0] == worldspace
                if sub or gtype in (0, 1):
                    walk(off + 24, off + size, sub)
                off += size
                continue
            sig = buf[off:off + 4]
            size, flags = struct.unpack_from("<II", buf, off + 4)
            if inside and sig == b"REFR":
                if flags & FLAG_DELETED:
                    dropped[0] += 1
                elif flags & FLAG_INITIALLY_DISABLED:
                    dropped[1] += 1
                else:
                    base = pos = None
                    for s, v in esm.subrecords(esm.payload(buf, off, size, flags)):
                        if s == b"NAME" and len(v) == 4:
                            base, = struct.unpack_from("<I", v, 0)
                        elif s == b"DATA" and len(v) >= 8:
                            pos = struct.unpack_from("<ff", v, 0)
                    if base is not None and pos is not None:
                        out.append((base, pos[0], pos[1]))
            off += 24 + size

    gp, gs = groups[b"WRLD"]
    walk(gp + 24, gp + gs, False)
    return out, dropped


def window(refs, margin=2, least=5):
    """Cell bounds of where things are placed, with a margin.

    Cells with fewer than `least` references do not count: Solstheim's
    worldspace has a scatter of props out on the low-detail copy of
    Skyrim's coast, and a bounding box that honoured them was twice the
    island."""
    from collections import Counter
    per = Counter((int(x // CELL), int(y // CELL)) for _, x, y in refs)
    cells = [c for c, n in per.items() if n >= least] or list(per)
    return (min(c[0] for c in cells) - margin, max(c[0] for c in cells) + margin,
            min(c[1] for c in cells) - margin, max(c[1] for c in cells) + margin)


def hull_area(pts):
    """Convex hull area by monotone chain, in square units."""
    pts = sorted(set(pts))
    if len(pts) < 3:
        return 0.0

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    hull = lower[:-1] + upper[:-1]
    area = 0.0
    for i in range(len(hull)):
        x0, y0 = hull[i]
        x1, y1 = hull[(i + 1) % len(hull)]
        area += x0 * y1 - x1 * y0
    return abs(area) / 2.0


def stats(pts):
    """(n, cells, cx, cy, rg, hull area in cells^2) of one cloud."""
    m = len(pts)
    cx = sum(p[0] for p in pts) / m
    cy = sum(p[1] for p in pts) / m
    rg = math.sqrt(sum((p[0] - cx) ** 2 + (p[1] - cy) ** 2 for p in pts) / m)
    cells = len({(int(p[0] // CELL), int(p[1] // CELL)) for p in pts})
    area = hull_area(pts) / (CELL * CELL) if m >= 3 else 0.0
    return m, cells, cx, cy, rg, area


def load(tag):
    """(bases, refs) of the worldspace, read once."""
    plugin, worldspace = world(tag)
    buf = (DATA / plugin).read_bytes()
    groups = esm.top_groups(buf)
    bases = base_records(buf, groups)
    refs, dropped = world_refs(buf, groups, worldspace)
    del buf
    return bases, refs, dropped


def main():
    tag = sys.argv[1].upper() if len(sys.argv) > 1 else "TAMRIEL"
    plugin, _ = world(tag)
    out = ROOT / "docs" / ("refr_cloud.csv" if tag == "TAMRIEL"
                           else "refr_cloud_%s.csv" % tag.lower())
    print("%s, from %s ..." % (tag, plugin))
    bases, refs, (deleted, disabled) = load(tag)
    print("  %d base records, %d live references (%d deleted, %d initially "
          "disabled skipped)" % (len(bases), len(refs), deleted, disabled))

    n = len(refs)
    gx = sum(r[1] for r in refs) / n
    gy = sum(r[2] for r in refs) / n
    grg = math.sqrt(sum((r[1] - gx) ** 2 + (r[2] - gy) ** 2 for r in refs) / n)
    print("  everything together: centroid (%.1f, %.1f) cells, rg %.1f cells"
          % (gx / CELL, gy / CELL, grg / CELL))

    cloud = defaultdict(list)
    for base, x, y in refs:
        cloud[base].append((x, y))

    rows = []
    for base, pts in cloud.items():
        sig, edid = bases.get(base, ("?", "<%08X>" % base))
        rows.append((sig, base, edid) + stats(pts))
    rows.sort(key=lambda r: -r[3])
    with io.open(out, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["sig", "formid", "editor_id", "n", "cells", "cx", "cy",
                    "rg_units", "hull_cells2"])
        for sig, base, edid, m, cells, cx, cy, rg, area in rows:
            w.writerow([sig, "%08X" % base, edid, m, cells, "%.0f" % cx,
                        "%.0f" % cy, "%.0f" % rg, "%.1f" % area])
    print("  wrote %s: %d base records placed" % (out.relative_to(ROOT), len(rows)))

    print("\nby name family:")
    print("   %-14s %7s %6s %8s %8s   %-18s %s"
          % ("family", "n", "cells", "rg/cell", "hull", "centroid (cells)", "meshes"))
    for word in FAMILY_WORDS:
        pts, meshes = [], 0
        for base, c in cloud.items():
            if word in bases.get(base, ("", ""))[1].lower():
                pts += c
                meshes += 1
        if len(pts) < 3:
            continue
        m, cells, cx, cy, rg, area = stats(pts)
        print("   %-14s %7d %6d %8.1f %8.0f   (%5.1f, %5.1f)        %d"
              % (word, m, cells, rg / CELL, area, cx / CELL, cy / CELL, meshes))


if __name__ == "__main__":
    main()
