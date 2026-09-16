#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Reading Skyrim's own plugin files: records, terrain, ground textures.

The format and nothing else. No climate, no model, no opinion about where a
place is - those live in tools/bake_climate.py and in the baked map itself.

Shared by tools/control_points.py, tools/bake_climate.py and
tools/draw_baked.py, which all have to decode the same LAND records the same
way or the workbook and the map stop describing the same world.

TAMRIEL is a module-level variable rather than an argument because land() and
regions() walk the whole record tree and test it deep inside; callers set it
around a call and put it back.
"""
import array
import struct
import zlib

TAMRIEL = 0x0000003C
CELL = 4096.0
SIDE = 33


def subrecords(buf):
    i, big = 0, None
    while i + 6 <= len(buf):
        s, sz = struct.unpack_from("<4sH", buf, i)
        i += 6
        if s == b"XXXX":
            big = struct.unpack_from("<I", buf, i)[0]
            i += sz
            continue
        if big is not None:
            sz, big = big, None
        yield s, buf[i:i + sz]
        i += sz

def payload(buf, off, size, flags):
    d = buf[off + 24:off + 24 + size]
    if flags & 0x00040000:
        try:
            return zlib.decompress(d[4:])
        except zlib.error:
            return b""
    return d

def top_groups(buf):
    out = {}
    pos = 24 + struct.unpack_from("<4sI", buf, 0)[1]
    while pos + 24 <= len(buf):
        sig, gsize, lab, gt = struct.unpack_from("<4sI4sI", buf, pos)
        if sig != b"GRUP":
            break
        out[lab] = (pos, gsize)
        pos += gsize
    return out

def snow_textures(buf, groups):
    """LTEX form ids whose editor id mentions snow."""
    ids = set()
    gp, gs = groups[b"LTEX"]
    p, e = gp + 24, gp + gs
    while p + 24 <= e:
        s, size, flags, formid, _, _ = struct.unpack_from("<4sIIIHH", buf, p)
        for ss, v in subrecords(payload(buf, p, size, flags)):
            if ss == b"EDID" and b"snow" in v.lower():
                ids.add(formid)
        p += 24 + size
    return ids

# Ice that lies on the ground or on the water, as opposed to ice that hangs
# off a cliff. Matched on the editor id, within Landscape\Ice models only.
ICE_WORDS = ("icefloe", "iceberg", "icepile", "iceplane", "frozenpool",
             "icelandbroken")

# How far a floe speaks beyond its own edge, as a multiple of its OBND
# radius.
#
# The blur that follows is deliberately modest, because on land it has to
# leave a forest-to-snowline transition standing where the game really has
# one. Pack ice has no such neighbour to protect: every last floe in the
# game sits inside an already frozen place, in open water, with more ice
# beyond it. So the gaps between floes are gaps in the reading and not in
# the climate, and this closes them.
ICE_SPREAD = 4.0


def ice_statics(buf, groups):
    """{form id: footprint radius in units} for pack ice and bergs.

    The snow covariate reads ATXT alpha, which is paint on the ground. Pack
    ice is not paint - it is three and a half thousand placed objects
    floating over a seabed textured in sand - so the Sea of Ghosts came out
    reading 0.00 snow and -3.8 degrees, in cells carrying forty ice floes
    each. It is a white surface either way, and the model should see it.

    The radius is the record's own OBND, not a number of ours: all 59
    Landscape\Ice records carry one, and they run from a few hundred units
    to IcebergLarge's 5056 x 4068. Half the mean of the two horizontal
    extents is taken, which is rotation-free - a reference carries a
    rotation and the bounds do not.

    Icicles and glacier pieces are deliberately out: one hangs off an
    overhang and the other is a wall, and neither is ground you walk on.
    """
    out = {}
    gp, gs = groups[b"STAT"]
    p, e = gp + 24, gp + gs
    while p + 24 <= e:
        if buf[p:p + 4] == b"GRUP":
            g, = struct.unpack_from("<I", buf, p + 4)
            if g < 24:
                break
            p += g
            continue
        size, flags, formid = struct.unpack_from("<III", buf, p + 4)
        edid = modl = obnd = None
        for s, v in subrecords(payload(buf, p, size, flags)):
            if s == b"EDID":
                edid = v.rstrip(b"\0").decode("cp1252", "replace").lower()
            elif s == b"MODL":
                modl = v.rstrip(b"\0").decode("cp1252", "replace").lower()
            elif s == b"OBND" and len(v) >= 12:
                obnd = struct.unpack_from("<6h", v, 0)
        if (edid and modl and obnd and
                modl.replace("/", "\\").startswith("landscape\\ice") and
                any(w in edid for w in ICE_WORDS)):
            span = ((obnd[3] - obnd[0]) + (obnd[4] - obnd[1])) / 2.0
            if span > 0.0:
                out[formid] = span / 2.0
        p += 24 + size
    return out


def contains(poly, x, y):
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def regions(buf, groups):
    """[(name, priority, [rings])] for Tamriel, highest priority first."""
    out = []
    gp, gs = groups[b"REGN"]
    p, e = gp + 24, gp + gs
    while p + 24 <= e:
        s, size, flags, formid, _, _ = struct.unpack_from("<4sIIIHH", buf, p)
        body = payload(buf, p, size, flags)
        edid = wnam = None
        rings = []
        prio = 0
        cur = None
        for ss, v in subrecords(body):
            if ss == b"EDID":
                edid = v.rstrip(b"\0").decode("cp1252")
            elif ss == b"WNAM":
                wnam = struct.unpack("<I", v)[0]
            elif ss == b"RDAT":
                cur = struct.unpack_from("<I", v, 0)[0]
                if cur == 3 and len(v) > 5:
                    prio = v[5]
            elif ss == b"RPLD":
                n = len(v) // 8
                f = struct.unpack_from("<%df" % (n * 2), v, 0)
                rings.append([(f[i], f[i + 1]) for i in range(0, len(f), 2)])
        if wnam == TAMRIEL and edid in AS and rings:
            out.append((edid, prio, rings))
        p += 24 + size
    # High Hrothgar and the Throat carry no weather list, so no priority to
    # read. They sit inside the mountain shape and say something more exact
    # about it, so they are tested first - the smaller the claim, the earlier.
    out.sort(key=lambda r: (-r[1], sum(len(x) for x in r[2])))
    return out

def land(buf, groups, snowids, iceids=None):
    """{(gx, gy): (heights, snow)} - both 33x33, snow as 0..1 coverage.

    `iceids` is ice_statics()' {form id: radius}. When given, every placed
    reference to one of those records paints full cover over its own
    footprint, on top of whatever the ground was painted with - pack ice is
    snow as far as the model is concerned, and it is the only white surface
    in the game that the ground textures do not describe.
    """
    heights = {}
    snow = {}
    floes = []

    def walk(off, end, in_world, pending):
        while off + 24 <= end:
            if buf[off:off + 4] == b"GRUP":
                size, label, gtype = struct.unpack_from("<I4si", buf, off + 4)
                if size < 24:
                    return
                sub = in_world
                if gtype == 1:
                    sub = struct.unpack_from("<I", label)[0] == TAMRIEL
                walk(off + 24, off + size, sub, pending)
                off += size
                continue
            sig = buf[off:off + 4]
            size, flags = struct.unpack_from("<II", buf, off + 4)
            if in_world and sig == b"CELL":
                pending[0] = None
                for s, pl in subrecords(payload(buf, off, size, flags)):
                    if s == b"XCLC" and len(pl) >= 8:
                        pending[0] = struct.unpack_from("<ii", pl)
            elif in_world and iceids and sig == b"REFR":
                base = None
                at = None
                scale = 1.0
                for s, pl in subrecords(payload(buf, off, size, flags)):
                    if s == b"NAME" and len(pl) == 4:
                        base, = struct.unpack_from("<I", pl, 0)
                    elif s == b"DATA" and len(pl) >= 24:
                        at = struct.unpack_from("<ff", pl, 0)
                    elif s == b"XSCL" and len(pl) >= 4:
                        scale, = struct.unpack_from("<f", pl, 0)
                if at is not None and base in iceids:
                    floes.append((at[0], at[1], iceids[base] * scale))
            elif in_world and sig == b"LAND" and pending[0]:
                key = pending[0]
                pending[0] = None
                hs = None
                cover = array.array("f", [0.0] * (33 * 33))
                layer = None
                for s, pl in subrecords(payload(buf, off, size, flags)):
                    if s == b"VHGT" and len(pl) >= 4 + 33 * 33:
                        hs = array.array("f", vhgt_grid(pl))
                    elif s == b"BTXT" and len(pl) >= 6:
                        tex, quad = struct.unpack_from("<IB", pl, 0)
                        if tex in snowids:
                            paint_quad(cover, quad, None, 1.0)
                    elif s == b"ATXT" and len(pl) >= 6:
                        tex, quad = struct.unpack_from("<IB", pl, 0)
                        layer = (quad, tex in snowids)
                    elif s == b"VTXT" and layer is not None:
                        quad, issnow = layer
                        layer = None
                        paint_quad(cover, quad, pl, 1.0 if issnow else 0.0)
                if hs is not None:
                    heights[key] = hs
                    snow[key] = cover
            off += 24 + size

    walk(0, len(buf), False, [None])
    paint_ice(snow, floes)
    return heights, snow


def paint_ice(snow, floes):
    """Stamp each floe's footprint into the cells it covers.

    Works in global vertex coordinates, because a floe is bigger than the
    128 units between vertices and routinely bigger than the 4096 of a
    cell: IcebergLarge is 5056 across. A vertex on a cell seam belongs to
    two cells at once and is written in both, or the seam would show.
    """
    for x, y, r in floes:
        r *= ICE_SPREAD
        if r <= 0.0:
            continue
        rr = r * r
        vi0 = int((x - r) // 128)
        vi1 = int((x + r) // 128) + 1
        vj0 = int((y - r) // 128)
        vj1 = int((y + r) // 128) + 1
        for vj in range(vj0, vj1 + 1):
            dy = vj * 128.0 - y
            for vi in range(vi0, vi1 + 1):
                dx = vi * 128.0 - x
                if dx * dx + dy * dy > rr:
                    continue
                gx, i = divmod(vi, 32)
                gy, j = divmod(vj, 32)
                xs = ((gx, i), (gx - 1, 32)) if i == 0 else ((gx, i),)
                ys = ((gy, j), (gy - 1, 32)) if j == 0 else ((gy, j),)
                for cx, ci in xs:
                    for cy, cj in ys:
                        cover = snow.get((cx, cy))
                        if cover is not None:
                            cover[cj * 33 + ci] = 1.0

def paint_quad(cover, quad, vtxt, value):
    """Composite one texture layer over the cell's 33x33 coverage grid.

    A quadrant is 17x17 of the cell's 33x33 points; quadrant 0 is the corner at
    the cell's own origin, 1 is +x, 2 is +y, 3 is both. VTXT gives the alpha at
    the points the layer touches; a base texture (no VTXT) covers the lot.
    """
    ox = 16 if (quad & 1) else 0
    oy = 16 if (quad & 2) else 0
    if vtxt is None:
        for r in range(17):
            base = (oy + r) * 33 + ox
            for c in range(17):
                cover[base + c] = value
        return
    for i in range(0, len(vtxt) - 7, 8):
        pos, _, alpha = struct.unpack_from("<HHf", vtxt, i)
        if pos >= 17 * 17:
            continue
        r, c = divmod(pos, 17)
        k = (oy + r) * 33 + ox + c
        a = 0.0 if alpha < 0.0 else (1.0 if alpha > 1.0 else alpha)
        cover[k] = cover[k] * (1.0 - a) + value * a


# --- the three models ------------------------------------------------------

def png(path, width, height, rows):
    raw = bytearray()
    for r in rows:
        raw.append(0)
        raw += r

    def chunk(tag, pl):
        c = struct.pack(">I", len(pl)) + tag + pl
        return c + struct.pack(">I", zlib.crc32(tag + pl) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                     + chunk(b"IEND", b""))

def vhgt_grid(vhgt):
    """The 33x33 grid in world units, VHGT's own delta encoding."""
    offset = struct.unpack_from("<f", vhgt)[0]
    g = struct.unpack_from("<%db" % (33 * 33), vhgt, 4)
    out = []
    row = offset
    for r in range(33):
        row += g[r * 33]
        v = row
        out.append(v * 8.0)
        for c in range(1, 33):
            v += g[r * 33 + c]
            out.append(v * 8.0)
    return out
