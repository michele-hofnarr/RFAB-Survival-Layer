#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check that the mod's own magic effects can actually apply.

Run it after every xEdit generator pass, before anything is played.

Two faults it exists to catch, both of which shipped once and neither of which
is visible in game as a fault - the illness simply does nothing:

  ARCHETYPE.  Peak and Dual value modifiers do not stack. The engine keeps one
              per actor value and never instantiates a second, so an effect
              whose actor value is already held applies nothing and shows
              nothing. Twenty library entries came out Peak or Dual because
              that is what the vanilla or RFAB effect each was copied from
              happened to be, and they collided with RFAB on six actor values
              and with each other on six more.

  CONDITION.  Four of the library entries are copied from RFAB's Peryite
              disease effects, which carry a condition that gates RFAB's own
              debuffs against RFAB's own boon. A penalty this mod applies has
              no business answering to it. The generator meant to strip them
              and silently did not, for twenty records.

It also lists the actor values two of our own records still share, which is not
a fault by itself - plain value modifiers stack - but is worth seeing.

    python tools/check_effects.py

Exit code is 1 if anything failed, so it can gate a release.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = ROOT / "RFAB_SurvivalLayer.esp"

# MGEF DATA offsets, TES5.
OFF_FLAGS = 0x00
OFF_ARCH = 0x40
OFF_AV = 0x44

ARCHETYPES = {
    0: "Value Modifier",
    1: "Script",
    5: "Dual Value Modifier",
    32: "Accumulate Magnitude",
    34: "Peak Value Modifier",
}

# The only archetypes our records are allowed to use. Script is the campfire
# effect and the monitor: they modify no actor value, so nothing can displace
# them.
ALLOWED = {0, 1}


def records(path):
    """Every top-level record in the file, as (tag, formid, body)."""
    data = path.read_bytes()
    out = []

    def walk(off, end):
        while off < end:
            tag = data[off:off + 4]
            size = struct.unpack_from("<I", data, off + 4)[0]
            if tag == b"GRUP":
                walk(off + 24, off + size)
                off += size
                continue
            flags = struct.unpack_from("<I", data, off + 8)[0]
            fid = struct.unpack_from("<I", data, off + 12)[0]
            body = data[off + 24:off + 24 + size]
            if flags & 0x00040000:
                body = zlib.decompress(body[4:])
            out.append((tag, fid, body))
            off += 24 + size

    walk(24 + struct.unpack_from("<I", data, 4)[0], len(data))
    return out


def subrecords(body):
    """(signature, payload) pairs, honouring the XXXX oversize marker."""
    off, out, oversize = 0, [], None
    while off < len(body):
        sig = body[off:off + 4]
        size = struct.unpack_from("<H", body, off + 4)[0]
        if sig == b"XXXX":
            oversize = struct.unpack_from("<I", body, off + 6)[0]
            off += 6 + size
            continue
        if oversize is not None:
            size, oversize = oversize, None
        out.append((sig.decode("latin1"), body[off + 6:off + 6 + size]))
        off += 6 + size
    return out


def main():
    if not PLUGIN.exists():
        print("plugin not found: %s" % PLUGIN)
        return 1

    ours = []
    for tag, fid, body in records(PLUGIN):
        if tag != b"MGEF":
            continue
        subs = subrecords(body)
        fields = dict(subs)
        edid = fields.get("EDID", b"").split(b"\0")[0].decode("latin1")
        if not edid.startswith("_RSL_"):
            continue
        data = fields.get("DATA", b"")
        if len(data) < OFF_AV + 4:
            print("  !! %s has no readable DATA" % edid)
            return 1
        ours.append({
            "edid": edid,
            "arch": struct.unpack_from("<I", data, OFF_ARCH)[0],
            "av": struct.unpack_from("<i", data, OFF_AV)[0],
            "conditions": sum(1 for s, _ in subs if s == "CTDA"),
        })

    print("%d magic effects of our own in %s" % (len(ours), PLUGIN.name))
    print()

    bad_arch = [r for r in ours if r["arch"] not in ALLOWED]
    bad_cond = [r for r in ours if r["conditions"]]

    print("-- archetype ------------------------------------------------------")
    if bad_arch:
        print("  FAIL: %d effect(s) use an archetype that does not stack." % len(bad_arch))
        for r in sorted(bad_arch, key=lambda r: r["edid"]):
            print("      %-34s %-22s av %d" % (
                r["edid"], ARCHETYPES.get(r["arch"], r["arch"]), r["av"]))
    else:
        print("  PASS: every effect is a plain value modifier or a script.")

    print()
    print("-- conditions -----------------------------------------------------")
    if bad_cond:
        print("  FAIL: %d effect(s) still carry a condition." % len(bad_cond))
        for r in sorted(bad_cond, key=lambda r: r["edid"]):
            print("      %-34s %d condition(s)" % (r["edid"], r["conditions"]))
    else:
        print("  PASS: no effect of ours is gated by a condition.")

    # Not a fault - plain modifiers stack - but two of our own on one actor
    # value is worth knowing about when a number looks doubled.
    print()
    print("-- actor values shared between our own effects ---------------------")
    byav = {}
    for r in ours:
        if r["arch"] == 0 and r["av"] >= 0:
            byav.setdefault(r["av"], []).append(r["edid"])
    shared = {av: names for av, names in byav.items() if len(names) > 1}
    if shared:
        for av in sorted(shared):
            print("  av %-4d %s" % (av, ", ".join(sorted(shared[av]))))
    else:
        print("  none")

    failed = len(bad_arch) + len(bad_cond)
    print()
    print("=" * 70)
    print(" %s" % ("ALL CLEAR" if not failed else "%d PROBLEM(S)" % failed))
    print("=" * 70)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
