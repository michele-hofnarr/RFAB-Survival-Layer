#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The blank the hand-set temperatures get written into.

Offline. One sheet per worldspace, listing every map marker with everything the
baking will know about that spot - region, height, snow on the ground, and how
far the place stands above the land around it.

    python tools/control_points.py --xlsx docs/control_points.xlsx
    python tools/control_points.py                 # the same, as text

WHY MARKERS. A control point has to be somewhere both of us can stand. A map
marker is a place fast travel goes to, and its coordinates come out of the
plugin rather than out of somebody reading them off the console.

WHAT THE COLUMNS ARE FOR. The temperature is not measured here - it is decided,
by looking at the bar in game, and the workbook turns a wanted bar reading into
degrees. See the Справка sheet, and docs/CLIMATE_MAP.md.

NOTHING ALREADY ANSWERED IS EVER LOST. Regenerating reads the existing workbook
first and carries every answer back in BY POSITION, from whichever sheet it was
on - including the single "Все маркеры" sheet this used to write. A place is
where it is; what it is called is a label, and labelling it better must not
throw the answer away.

NAMES come from the game's own string tables, so the list reads the way the
game does. The pack ships Skyrim's loose; every DLC's lives inside
Skyrim - Interface.bsa, which is read here directly. A marker whose name will
not resolve falls back to what it is and where it stands ("пещера 43520,
75284") - still findable on the map, and safe to rename by hand, because the
answers are keyed to the coordinates rather than to the label.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT.parent.parent.parent
DATA = GAME / "Data"
MODS = GAME / "MO2" / "mods"

sys.path.insert(0, str(ROOT / "tools"))
import esm                     # noqa: E402
import snow_field                 # noqa: E402
import fit_climate as F           # noqa: E402

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

CELL = 4096.0

# How far out "the land around it" reaches when a place is measured against its
# surroundings. Four cells: far enough that a valley floor reads as a floor,
# near enough that a peak does not average itself away.
AROUND = 4

# What a level of clothing is worth, out of Settings.h: every slot is
# fWarmthPerSlot and every point of frost resistance fFrostResistWeight.
WARMTH_PER_SLOT = 14.67
WARMTH_PER_RESIST = 0.818

PROTECTION = [
    ("голым", 0, 0),
    ("4 слота, без резиста", 4, 0),
    ("4 слота, резист 25%", 4, 25),
    ("4 слота, резист 50%", 4, 50),
    ("4 слота, резист 75%", 4, 75),
    ("4 слота, резист 100%", 4, 100),
]

# tag, worldspace, the strings to read names from, and every plugin that puts
# markers in it. The DLCs add their own to Tamriel - Castle Volkihar is
# Dawnguard's - and a sheet that left those out would lose them.
WORLDS = [
    ("Skyrim", 0x0000003C,
     ["Skyrim.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]),
    ("Solstheim", 0x02000800, ["Dragonborn.esm"]),
]

# What a map marker is, for the worldspace whose names we cannot read.
MARKER_TYPE = {
    0: "маркер", 1: "город", 2: "городок", 3: "поселение", 4: "пещера",
    5: "лагерь", 6: "форт", 7: "нордские руины", 8: "двемерские руины",
    9: "кораблекрушение", 10: "роща", 11: "место", 12: "логово дракона",
    13: "ферма", 14: "лесопилка", 15: "шахта", 16: "имперский лагерь",
    17: "лагерь Братьев Бури", 18: "камень", 19: "стена слова",
    20: "лагерь великанов", 21: "хижина", 22: "конюшни", 23: "поляна",
    24: "маяк", 25: "орочья крепость", 26: "святилище даэдра",
    27: "драконий курган", 28: "кораблекрушение", 29: "причал", 30: "алтарь",
    31: "нордское жилище",
}


# --- the plugin format, only as much as this needs -------------------------

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


def parse_strings(buf):
    """{id: text} out of a .STRINGS table."""
    count = struct.unpack_from("<I", buf, 0)[0]
    head = 8 + count * 8
    out = {}
    for i in range(count):
        sid, off = struct.unpack_from("<II", buf, 8 + i * 8)
        start = head + off
        out[sid] = buf[start:buf.find(b"\0", start)].decode("utf-8", "replace")
    return out


def from_bsa(archive, wanted):
    """One file out of a BSA, by its full path inside the archive.

    Only as much of the format as this needs. The string tables are stored
    uncompressed, which is the whole reason this is sixty lines and not a
    dependency: no LZ4 decoder is required to read them.
    """
    if not archive.exists():
        return None
    buf = archive.read_bytes()
    if buf[:4] != b"BSA\0":
        return None
    (version, offset, flags, folders, files,
     folder_names, file_names, _ff, _pad) = struct.unpack_from("<IIIIIIIHH", buf, 4)
    if version not in (104, 105):
        return None

    recsize = 24 if version == 105 else 16
    at = offset
    folder_recs = []
    for _ in range(folders):
        if version == 105:
            _hash, count, _p, off = struct.unpack_from("<QIIQ", buf, at)
        else:
            _hash, count, off = struct.unpack_from("<QII", buf, at)
        folder_recs.append((count, off))
        at += recsize

    entries = []
    for count, off in folder_recs:
        p = off - file_names          # the offset counts the name block in
        folder = ""
        if flags & 0x1:
            n = buf[p]
            folder = buf[p + 1:p + n].decode("cp1252", "replace")
            p += 1 + n
        for _ in range(count):
            _fh, size, foff = struct.unpack_from("<QII", buf, p)
            entries.append([folder, None, size, foff])
            p += 16

    if flags & 0x2:
        # The names of every file, in order, after the last file record.
        p = offset + folders * recsize
        for count, off in folder_recs:
            q = off - file_names
            if flags & 0x1:
                q += 1 + buf[q]
            p = max(p, q + count * 16)
        for e in entries:
            end = buf.find(b"\0", p)
            e[1] = buf[p:end].decode("cp1252", "replace")
            p = end + 1

    for folder, name, size, off in entries:
        if not name or ("%s/%s" % (folder, name)).lower() != wanted:
            continue
        compressed = bool(flags & 0x4)
        if size & 0x40000000:
            compressed = not compressed
            size &= ~0x40000000
        p = off
        if flags & 0x100:             # the name is repeated before the data
            n = buf[p]
            p += 1 + n
            size -= 1 + n
        if compressed:
            import zlib
            try:
                return zlib.decompress(buf[p + 4:p + size])
            except zlib.error:
                return None           # LZ4, which nothing here needs
        return buf[p:p + size]
    return None


def strings_table(stem):
    """{id: text} for one plugin, from wherever its table is.

    The pack ships Skyrim's loose, which is what makes the list read in the
    pack's own Russian rather than the game's. The DLCs' are not loose anywhere
    in this install - they sit inside Skyrim - Interface.bsa, so that is read.
    """
    if not stem:
        return {}
    for cand in (MODS / "RFAB" / "strings" / (stem + "_russian.STRINGS"),
                 DATA / "Strings" / (stem + "_russian.STRINGS"),
                 DATA / "Strings" / (stem + "_english.STRINGS")):
        if cand.exists():
            return parse_strings(cand.read_bytes())
    for archive in ("Skyrim - Interface.bsa", "Skyrim - Patch.bsa"):
        for lang in ("russian", "english"):
            raw = from_bsa(DATA / archive,
                           "strings/%s_%s.strings" % (stem, lang))
            if raw:
                return parse_strings(raw)
    return {}


def markers(buf, worldspace):
    """[(name id, marker type, x, y, z)] - the persistent references."""
    out = []

    def payload(off, size, flags):
        d = buf[off + 24:off + 24 + size]
        if flags & 0x00040000:
            try:
                return zlib.decompress(d[4:])
            except zlib.error:
                return b""
        return d

    def scan(off, end):
        while off + 24 <= end:
            sig, size = struct.unpack_from("<4sI", buf, off)
            if sig == b"GRUP":
                label, gtype = struct.unpack_from("<4sI", buf, off + 8)
                if gtype in (4, 5):     # exterior blocks - markers are not there
                    off += size
                    continue
                scan(off + 24, off + size)
                off += size
                continue
            flags = struct.unpack_from("<I", buf, off + 8)[0]
            if sig == b"REFR":
                name = kind = pos = None
                marker = False
                for s, v in subrecords(payload(off, size, flags)):
                    if s == b"XMRK":
                        marker = True
                    elif s == b"FULL" and len(v) == 4:
                        name = struct.unpack("<I", v)[0]
                    elif s == b"TNAM" and len(v) >= 1:
                        kind = v[0]
                    elif s == b"DATA" and len(v) >= 12:
                        pos = struct.unpack_from("<fff", v, 0)
                if marker and pos:
                    out.append((name, kind, pos[0], pos[1], pos[2]))
            off += 24 + size

    groups = esm.top_groups(buf)
    gp, gs = groups[b"WRLD"]
    p, end = gp + 24, gp + gs
    # The worldspace's own children group holds the persistent cell, and every
    # map marker is a persistent reference. No need to walk the whole province.
    while p + 24 <= end:
        sig, size = struct.unpack_from("<4sI", buf, p)
        if sig == b"GRUP":
            label, gtype = struct.unpack_from("<4sI", buf, p + 8)
            if gtype == 1 and struct.unpack_from("<I", label)[0] == worldspace:
                scan(p + 24, p + size)
                return out
            p += size
            continue
        p += 24 + size
    return out


def regions(buf, worldspace):
    """[(name, [rings])] for one worldspace, highest weather priority first."""
    out = []

    def payload(off, size, flags):
        d = buf[off + 24:off + 24 + size]
        if flags & 0x00040000:
            try:
                return zlib.decompress(d[4:])
            except zlib.error:
                return b""
        return d

    groups = esm.top_groups(buf)
    if b"REGN" not in groups:
        return out
    gp, gs = groups[b"REGN"]
    p, e = gp + 24, gp + gs
    while p + 24 <= e:
        s, size, flags, formid, _, _ = struct.unpack_from("<4sIIIHH", buf, p)
        edid = wnam = None
        rings = []
        prio = 0
        cur = None
        for ss, v in subrecords(payload(p, size, flags)):
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
        if wnam == worldspace and rings and edid and edid.startswith("Weather"):
            out.append((edid.replace("Weather", ""), prio, rings))
        elif wnam == worldspace and rings and edid and "Solstheim" in edid:
            out.append((edid.replace("DLC2Solstheim", ""), prio, rings))
        p += 24 + size
    out.sort(key=lambda r: (-r[1], sum(len(x) for x in r[2])))
    return [(n, r) for n, _, r in out]


# --- what the baking will know about a spot --------------------------------

def sample(heights, x, y):
    """Terrain height at a point, from the 33x33 grid of its cell."""
    gx, gy = int(x // CELL), int(y // CELL)
    hs = heights.get((gx, gy))
    if hs is None:
        return None
    fx = (x - gx * CELL) / 128.0
    fy = (y - gy * CELL) / 128.0
    i = min(31, max(0, int(fx)))
    j = min(31, max(0, int(fy)))
    u, v = fx - i, fy - j
    h00 = hs[j * 33 + i]
    h10 = hs[j * 33 + i + 1]
    h01 = hs[(j + 1) * 33 + i]
    h11 = hs[(j + 1) * 33 + i + 1]
    return (h00 * (1 - u) * (1 - v) + h10 * u * (1 - v)
            + h01 * (1 - u) * v + h11 * u * v)


def snow_at(snow, x, y):
    gx, gy = int(x // CELL), int(y // CELL)
    sc = snow.get((gx, gy))
    if sc is None:
        return None
    i = min(32, max(0, int((x - gx * CELL) / 128.0)))
    j = min(32, max(0, int((y - gy * CELL) / 128.0)))
    return sc[j * 33 + i]


def around(heights, x, y):
    """Mean terrain height of the cells around this one."""
    gx, gy = int(x // CELL), int(y // CELL)
    total = n = 0
    for dy in range(-AROUND, AROUND + 1):
        for dx in range(-AROUND, AROUND + 1):
            hs = heights.get((gx + dx, gy + dy))
            if hs is None:
                continue
            for k in range(0, 33 * 33, 37):
                total += hs[k]
                n += 1
    return total / n if n else None


def collect(tag, worldspace, plugins, snowids, iceids, famids):
    """Every marker of one worldspace, with its covariates.

    Each plugin's markers are named from that plugin's own string table, so
    Dawnguard's Castle Volkihar reads as itself rather than as a fort at a
    coordinate.
    """
    print("# %s ..." % tag, file=sys.stderr)
    marks = []
    regs = []
    heights = snow = None
    fams = {}
    for plugin in plugins:
        path = DATA / plugin
        if not path.exists():
            continue
        buf = path.read_bytes()
        found = markers(buf, worldspace)
        if found:
            # RESOLVED HERE, against this plugin's OWN table.
            #
            # The id spaces are per file and they overlap heavily: 4226 of
            # Skyrim's 30301 ids also exist in a DLC table meaning something
            # else. Merging the tables and looking up afterwards renamed every
            # one of them - Septimus Signus's Outpost came out as a line of
            # Dawnguard dialogue, because 0x2328 means both.
            table = strings_table(plugin.split(".")[0].lower())
            marks += [(table.get(nid), kind, x, y, z)
                      for nid, kind, x, y, z in found]
        if heights is None:
            # Terrain and regions come from whichever plugin owns the
            # worldspace, which is the first one listed.
            regs = regions(buf, worldspace)
            was = esm.TAMRIEL
            esm.TAMRIEL = worldspace
            try:
                heights, snow, fams = esm.land(
                    buf, esm.top_groups(buf), snowids, iceids, famids)
            finally:
                esm.TAMRIEL = was
            # The same ruler the baking uses, so a point is measured the way
            # the field around it will be built - the families included.
            snow = snow_field.smooth(snow)
            fams = {n: snow_field.smooth(g) for n, g in fams.items()}
        del buf
        if found:
            print("#   %-16s %d markers" % (plugin, len(found)), file=sys.stderr)
    print("#   %d markers, %d regions, %d cells with terrain"
          % (len(marks), len(regs), len(heights or {})), file=sys.stderr)

    rows = []
    for name, kind, x, y, z in marks:
        h = sample(heights, x, y)
        if h is None:
            continue
        mean = around(heights, x, y)
        rel = (h - mean) if mean is not None else 0.0
        hit = "-"
        for rname, rings in regs:
            if any(esm.contains(r, x, y) for r in rings):
                hit = rname
                break
        if not name:
            # No strings for this plugin: say what it is and where.
            name = "%s %.0f, %.0f" % (
                MARKER_TYPE.get(kind, "маркер"), x, y)
        rows.append([name, x, y, h, rel, snow_at(snow, x, y) or 0.0, hit]
                    + [(snow_at(fams[n], x, y) or 0.0) if n in fams else 0.0
                       for n in F.FAMILIES])
    rows.sort(key=lambda r: -r[3])
    return rows


# --- the workbook ----------------------------------------------------------

def existing_answers(path):
    """What is already filled in, so regenerating never loses it.

    KEYED BY POSITION, not by name. A place is where it is; what it is called
    is a label, and labelling it better should not throw the answer away. That
    also lets Solstheim's rows - named after their own coordinates for want of
    anything else - be renamed by hand with no consequence.

    Read from EVERY sheet, including the single "Все маркеры" one this used to
    write, so an older workbook carries forward without being touched by hand.
    """
    if not Path(path).exists():
        return {}
    from openpyxl import load_workbook
    wb = load_workbook(path)
    out = {}
    for name in wb.sheetnames:
        if name == "Справка":
            continue
        ws = wb[name]
        # BY HEADER, NOT BY POSITION. The columns have been renumbered once
        # already - dropping a column and reading the next one along turns a
        # protection level into a bar reading and loses every answer.
        head = {str(ws.cell(1, c).value).strip(): c
                for c in range(1, ws.max_column + 1)
                if ws.cell(1, c).value}
        cx, cy = head.get("X"), head.get("Y")
        cp, cb = head.get("защита"), head.get("равновесие")
        if not all((cx, cy, cp, cb)):
            print("#   %s: не нашёл колонок ответов, лист пропущен" % name,
                  file=sys.stderr)
            continue
        for i in range(2, ws.max_row + 1):
            x, y = ws.cell(i, cx).value, ws.cell(i, cy).value
            prot, bar = ws.cell(i, cp).value, ws.cell(i, cb).value
            if x is None or y is None or not prot or bar is None:
                continue
            out[(round(float(x)), round(float(y)))] = (prot, float(bar))
    return out


HEAD = ["место", "X", "Y", "земля Z", "превышение", "снег", "регион (игра)",
        "защита", "равновесие", "T, градусы"] + F.FAMILIES


def write_xlsx(sheets, path):
    """One sheet per worldspace, plus the note that explains the two columns."""
    from openpyxl import Workbook
    from openpyxl.styles import Alignment, Font, PatternFill
    from openpyxl.worksheet.datavalidation import DataValidation

    answers = existing_answers(path)
    wb = Workbook()
    wb.remove(wb.active)

    ref = wb.create_sheet("Справка")
    ref["A1"] = "защита"
    ref["B1"] = "warmth"
    for i, (name, slots, resist) in enumerate(PROTECTION, start=2):
        ref.cell(i, 1, name)
        ref.cell(i, 2, slots * WARMTH_PER_SLOT + resist * WARMTH_PER_RESIST)
    last = len(PROTECTION) + 1
    ref["D1"] = "Как это считается"
    for i, line in enumerate((
            "warmth = слоты x 14.67 + резист% x 0.818",
            "chill  = max(0, 12 - температура)",
            "load   = chill x 0.055 - warmth x 0.010   (сухо, без огня)",
            "шкала стремится к 1 - load",
            "",
            "обратно:  температура = 12 - (1 - равновесие + warmth/100) / 0.055",
            "",
            "Равновесие можно ставить БОЛЬШЕ 1: это значит 'полная с запасом'.",
            "Ровно 1.0 значит 'полная, но без запаса'.",
            "",
            "Условия всюду одни: день, ясно, сухо, рядом нет огня.",
            "",
            "Лист на мир. Заполняй любую строку - карта строится по всем, где",
            "есть и защита, и равновесие. Перегенерация ответы не затирает:",
            "они переносятся ПО КООРДИНАТАМ, с любого листа.",
            "",
            "Имена берутся из строковых таблиц игры: у Скайрима из тех, что",
            "кладёт сборка, у DLC - прямо из Skyrim - Interface.bsa. Если имя",
            "не нашлось, строка названа по типу маркера и координатам.",
            "Переименовывай как угодно: ответ привязан к месту, а не к",
            "подписи, и от переименования не теряется.",
    ), start=2):
        ref.cell(i, 4, line)
    ref.column_dimensions["A"].width = 22
    ref.column_dimensions["B"].width = 10
    ref.column_dimensions["D"].width = 66

    filled = PatternFill("solid", fgColor="C6E0B4")
    blank = PatternFill("solid", fgColor="FFF2CC")
    carried = 0

    for tag, rows in sheets:
        ws = wb.create_sheet(tag)
        ws.append(HEAD)
        for c in range(1, len(HEAD) + 1):
            ws.cell(1, c).font = Font(bold=True)
            ws.cell(1, c).alignment = Alignment(horizontal="center")
        for r in rows:
            a = answers.get((round(r[1]), round(r[2])))
            carried += 1 if a else 0
            ws.append([r[0], round(r[1]), round(r[2]), round(r[3]), round(r[4]),
                       round(r[5], 2), r[6],
                       a[0] if a else None, a[1] if a else None, None]
                      + [round(v, 2) for v in r[7:]])
        for i in range(2, ws.max_row + 1):
            f = filled if ws.cell(i, 8).value else blank
            ws.cell(i, 8).fill = f
            ws.cell(i, 9).fill = f
            ws.cell(i, 9).number_format = "0.00"
            ws.cell(i, 6).number_format = "0.00"
            for c in range(11, 11 + len(F.FAMILIES)):
                ws.cell(i, c).number_format = "0.00"
            ws.cell(i, 10).value = (
                "=IFERROR(12-(1-I%d+VLOOKUP(H%d,Справка!$A$2:$B$%d,2,FALSE)"
                "/100)/0.055,\"\")" % (i, i, last))
            ws.cell(i, 10).number_format = "+0.0;-0.0"
            ws.cell(i, 10).font = Font(bold=True)
        dv = DataValidation(type="list", formula1="=Справка!$A$2:$A$%d" % last,
                            allow_blank=True)
        ws.add_data_validation(dv)
        dv.add("H2:H%d" % ws.max_row)
        dvv = DataValidation(type="decimal", operator="between", formula1="0",
                             formula2="3", allow_blank=True)
        ws.add_data_validation(dvv)
        dvv.add("I2:I%d" % ws.max_row)
        ws.auto_filter.ref = "A1:J%d" % ws.max_row
        for col, w in zip("ABCDEFGHIJ",
                          (34, 9, 9, 9, 11, 7, 15, 21, 12, 11)):
            ws.column_dimensions[col].width = w
        ws.freeze_panes = "A2"

    wb.move_sheet("Справка", offset=len(sheets))
    wb.save(path)
    print("# %s: %s, %d answers carried over"
          % (path, ", ".join("%s %d" % (t, len(r)) for t, r in sheets), carried),
          file=sys.stderr)


def main():
    snowids = set()
    iceids = {}
    famids = {n: set() for n in F.FAMILIES}
    for plugin in ("Skyrim.esm", "Dragonborn.esm"):
        buf = (DATA / plugin).read_bytes()
        g = esm.top_groups(buf)
        if b"LTEX" in g:
            snowids |= esm.snow_textures(buf, g)
            for n in F.FAMILIES:
                famids[n] |= esm.textures_named(buf, g, n)
        if b"STAT" in g:
            iceids.update(esm.ice_statics(buf, g))
        del buf

    sheets = [(tag, collect(tag, ws, plugins, snowids, iceids, famids))
              for tag, ws, plugins in WORLDS]

    if "--xlsx" in sys.argv:
        write_xlsx(sheets, sys.argv[sys.argv.index("--xlsx") + 1])
        return

    for tag, rows in sheets:
        print("# --- %s ---" % tag)
        print("# %-32s %8s %8s %8s %8s %6s  %-16s %s"
              % ("name", "x", "y", "ground", "rel", "snow", "region", "T"))
        for r in rows:
            print("  %-32s %8.0f %8.0f %8.0f %+8.0f %6.2f  %-16s ?"
                  % (r[0][:32], r[1], r[2], r[3], r[4], r[5], r[6]))


if __name__ == "__main__":
    main()
