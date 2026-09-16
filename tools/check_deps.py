#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check that every external record the mod depends on is still where it was.

Run it after RFAB updates. It reads SSEEdit_Scripts/RFAB_deps.txt - the same
table RFAB_Validate_Deps.pas reads - opens the plugin files themselves and says
PASS or FAIL per line.

Why not just the xEdit script: xEdit has no command line, so that check only
happens when somebody remembers to sit down and run it by hand. This reads the
same bytes xEdit reads, takes a second, and can run as part of the pre-flight.
The xEdit script stays for what only xEdit can answer - winning overrides and
the archetype templates.

    python tools/check_deps.py
    python tools/check_deps.py --what RFAB.esp 0CE266 CD955

Exit code is 1 if anything failed, so it can gate a build.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "SSEEdit_Scripts" / "RFAB_deps.txt"

# ROOT is <game>/MO2/mods/<mod>, so the game folder is three levels up.
GAME = ROOT.parent.parent.parent
DATA = GAME / "Data"
MODS = ROOT.parent

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


# --- the plugin format, as much of it as this needs ------------------------
#
# A record header is 24 bytes: signature, data size, flags, formID, and the
# rest we do not use. Groups nest, and a group header is the same 24 bytes with
# its size counting the header itself. Compressed records (flag 0x00040000)
# carry a 4-byte uncompressed length before the zlib stream.

COMPRESSED = 0x00040000


def subrecords(data):
    """(signature, payload) for each subrecord, in order.

    XXXX carries the real length of the subrecord that follows when it does not
    fit in the 16-bit field - a long VMAD or a big DESC hits it.
    """
    i, big = 0, 0
    while i + 6 <= len(data):
        sig = data[i:i + 4]
        size = struct.unpack_from("<H", data, i + 4)[0]
        i += 6
        if sig == b"XXXX":
            big = struct.unpack_from("<I", data, i)[0]
            i += size
            continue
        if big:
            size, big = big, 0
        yield sig, data[i:i + size]
        i += size


def edid_of(data):
    """The EditorID subrecord, or None."""
    for sig, payload in subrecords(data):
        if sig == b"EDID":
            return payload.rstrip(b"\0").decode("cp1251", "replace")
    return None


def scan(path, sigs):
    """{local formID: (sig, edid)} for every record in the wanted groups.

    Top-level groups whose label is not wanted are skipped whole, which is what
    keeps a 250 MB master under a second: the bulk of a plugin is CELL and WRLD
    and we never need to look inside them.
    """
    buf = path.read_bytes()
    found = {}

    def walk(off, end, top):
        while off + 24 <= end:
            if buf[off:off + 4] == b"GRUP":
                # GRUP header: size at +4, label at +8, type at +12. The type was read
                # from +16 here for a while, which is the timestamp - so the
                # top-level skip never fired and every group was descended into.
                # Results were right, the shortcut simply was not taken.
                size, label, gtype = struct.unpack_from("<I4si", buf, off + 4)
                if size < 24:
                    return
                if not (top and gtype == 0 and label not in sigs):
                    walk(off + 24, off + size, False)
                off += size
                continue
            sig = buf[off:off + 4]
            size, flags = struct.unpack_from("<II", buf, off + 4)
            fid = struct.unpack_from("<I", buf, off + 12)[0]
            if sig in sigs:
                data = buf[off + 24:off + 24 + size]
                if flags & COMPRESSED:
                    try:
                        data = zlib.decompress(data[4:])
                    except zlib.error:
                        data = b""
                found[fid & 0xFFFFFF] = (sig.decode(), edid_of(data))
            off += 24 + size

    walk(0, len(buf), True)
    return found


def scan_all(path):
    """{full formID: (sig, offset, size, flags)} for every record in the file.

    Keyed by the whole formID, not the low 24 bits: the top byte is the index
    into the file's own master list, and telling an override of a master apart
    from one of our own records is exactly what it is for.
    """
    buf = path.read_bytes()
    found = {}

    def walk(off, end):
        while off + 24 <= end:
            if buf[off:off + 4] == b"GRUP":
                size = struct.unpack_from("<I", buf, off + 4)[0]
                if size < 24:
                    return
                walk(off + 24, off + size)
                off += size
                continue
            sig = buf[off:off + 4]
            size, flags = struct.unpack_from("<II", buf, off + 4)
            fid = struct.unpack_from("<I", buf, off + 12)[0]
            found[fid] = (sig.decode(), off, size, flags)
            off += 24 + size

    walk(0, len(buf))
    return found


def record_data(buf, hit):
    """The record's payload, decompressed if it needs it."""
    _, off, size, flags = hit
    data = buf[off + 24:off + 24 + size]
    if flags & COMPRESSED:
        try:
            data = zlib.decompress(data[4:])
        except zlib.error:
            data = b""
    return data


def masters_of(path):
    """The file's master list, in order. A record's top byte indexes into it."""
    buf = path.read_bytes()
    size = struct.unpack_from("<I", buf, 4)[0]
    return [p.rstrip(b"\0").decode("cp1251", "replace")
            for sig, p in subrecords(buf[24:24 + size]) if sig == b"MAST"]


def locate(name):
    """The file as the game would load it: Data first, then the MO2 mods."""
    direct = DATA / name
    if direct.is_file():
        return direct
    hits = sorted(MODS.glob("*/" + name))
    return hits[0] if hits else None


OURS = "RFAB_SurvivalLayer.esp"


def check_overrides(rows):
    """Our own plugin must override exactly the records the table marks.

    The other half of an RFAB update. Every record we overwrite is a record
    whose next RFAB fix we silently revert until the generator is run again, so
    the rule is that the list stays short and explicit - and something has to
    notice when it grows.

    What is NOT checked here is whether the CONTENT of those overrides still
    matches RFAB's: formIDs inside a subrecord are stored as indices into the
    plugin's own master list, and ours is ordered differently from RFAB's, so
    every reference-carrying subrecord differs on a byte compare whether or not
    anything really changed. Telling those apart needs the record schema, which
    is what xEdit has and this does not. The generator rebuilds all five from
    the current RFAB, which is why it is run after every RFAB update.
    """
    print()
    print("-- the master records we override --")

    ours = locate(OURS)
    if not ours:
        print("  %s not built yet - skipped" % OURS)
        return 0

    expected = [(f, k) for f, k, _, note in rows if note.startswith("[override]")]
    matched = set()

    masters = masters_of(ours)
    buf = ours.read_bytes()

    count, bad = 0, 0
    for fid, hit in sorted(scan_all(ours).items()):
        index = fid >> 24
        if hit[0] == "TES4" or index >= len(masters):
            continue                       # the header, or one of our own
        fname, low = masters[index], "%06X" % (fid & 0xFFFFFF)
        name = edid_of(record_data(buf, hit)) or ""
        count += 1

        want = next((e for e in expected if e[0] == fname and
                     (e[1].lstrip("#").upper() == low or
                      e[1].lower() == name.lower())), None)
        if want:
            matched.add(want)
            print("  ok    %-10s %s %s \"%s\"" % (fname, hit[0], low, name))
        else:
            bad += 1
            print("  FAIL  %-10s %s %s \"%s\" - not on the override list"
                  % (fname, hit[0], low, name))

    for entry in expected:
        if entry not in matched:
            bad += 1
            print("  FAIL  %s %s is on the override list but is not overridden"
                  % entry)

    print("  %d override(s). Content is not compared here - see the docstring;"
          % count)
    print("  re-run the generator after an RFAB update and it rebuilds them.")
    return bad


_full = {}


def full_scan(fname):
    """Every record in a file, by local formID, cached. Only for diagnostics."""
    if fname not in _full:
        path = locate(fname)
        _full[fname] = {f & 0xFFFFFF: v
                        for f, v in scan_all(path).items()} if path else {}
    return _full[fname]


def read_table():
    rows = []
    for n, line in enumerate(TABLE.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) != 4:
            raise SystemExit("%s:%d: expected 4 fields, got %d"
                             % (TABLE.name, n, len(parts)))
        rows.append(tuple(p.strip() for p in parts))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--what", nargs="+", metavar="FILE ID",
                    help="print what sits at these local formIDs in FILE")
    args = ap.parse_args()

    if args.what:
        name, ids = args.what[0], [int(x, 16) for x in args.what[1:]]
        path = locate(name)
        if not path:
            raise SystemExit("%s not found under %s or %s" % (name, DATA, MODS))
        every = {f & 0xFFFFFF: v for f, v in scan_all(path).items()}
        buf = path.read_bytes()
        for want in ids:
            hit = every.get(want)
            if not hit:
                print("  %06X  -- no such record in %s" % (want, name))
                continue
            print("  %06X  %s  %s" % (want, hit[0], edid_of(record_data(buf, hit))))
        return 0

    rows = read_table()
    wanted = {}
    for fname, key, sig, _ in rows:
        wanted.setdefault(fname, set()).add(sig.encode())

    print("=" * 70)
    print(" RFAB Survival Layer - external dependencies")
    print("=" * 70)

    cache, missing_file = {}, {}
    for fname, sigs in wanted.items():
        path = locate(fname)
        if not path:
            missing_file[fname] = True
            continue
        cache[fname] = scan(path, sigs)
        print("  read %-24s %s/" % (fname, path.parent.name))
    print()

    npass = nfail = 0
    for fname, key, sig, note in rows:
        if fname in missing_file:
            nfail += 1
            print("  FAIL  %s not found  (%s)" % (fname, note))
            continue

        table = cache[fname]
        if key.startswith("#"):
            want = int(key[1:], 16)
            hit = table.get(want)
            if hit and hit[0] == sig:
                npass += 1
                print("  ok    %-16s %s %06X \"%s\"  (%s)"
                      % (fname, sig, want, hit[1], note))
            elif hit:
                nfail += 1
                print("  FAIL  %-16s %06X is %s, not %s - \"%s\"  (%s)"
                      % (fname, want, hit[0], sig, hit[1], note))
            else:
                nfail += 1
                # Not in that group - say which group it IS in, if any. "RFAB
                # changed the record's type" and "RFAB deleted the record" want
                # different fixes, and the filtered scan cannot tell them apart
                # on its own.
                elsewhere = full_scan(fname).get(want)
                where = (" - it is a %s now" % elsewhere[0]) if elsewhere                     else " - no such record"
                print("  FAIL  %-16s %s %06X%s  (%s)"
                      % (fname, sig, want, where, note))
        else:
            hit = next(((fid, e) for fid, (s, e) in table.items()
                        if s == sig and e and e.lower() == key.lower()), None)
            if hit:
                npass += 1
                print("  ok    %-16s %s %06X \"%s\"  (%s)"
                      % (fname, sig, hit[0], key, note))
            else:
                nfail += 1
                print("  FAIL  %-16s %s \"%s\" - not found  (%s)"
                      % (fname, sig, key, note))

    nfail += check_overrides(rows)

    print()
    print("=" * 70)
    print(" PASS %d   FAIL %d" % (npass, nfail))
    print(" All dependencies resolve." if nfail == 0 else
          " SOME DEPENDENCIES MOVED - see the FAIL lines above.")
    print("=" * 70)
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
