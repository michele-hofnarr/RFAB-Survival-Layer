#!/usr/bin/env python3
"""Static audit of RFAB.esp disease system."""
import struct, zlib, sys, collections, io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

PATH = r"R:/Games/The Elder Scrolls V Skyrim - Special Edition/MO2/mods/RFAB/RFAB.esp"
data = open(PATH, "rb").read()

Record = collections.namedtuple("Record", "sig formid flags fields")
records = {}
low24 = {}                       # (fid & 0xFFFFFF) -> Record   (best-effort)
by_sig = collections.defaultdict(list)

def parse_fields(buf):
    out, i, override = [], 0, None
    while i + 6 <= len(buf):
        fsig = buf[i:i+4]; fsize = struct.unpack_from("<H", buf, i+4)[0]; i += 6
        if fsig == b"XXXX":
            override = struct.unpack_from("<I", buf, i)[0]; i += fsize; continue
        if override is not None:
            fsize = override; override = None
        out.append((fsig, buf[i:i+fsize])); i += fsize
    return out

def walk(buf, start, end):
    i = start
    while i < end:
        sig = buf[i:i+4]
        if sig == b"GRUP":
            gsize = struct.unpack_from("<I", buf, i+4)[0]
            walk(buf, i+24, i+gsize); i += gsize
        else:
            dsize, flags, formid = struct.unpack_from("<IiI", buf, i+4)
            body = buf[i+24:i+24+dsize]
            if flags & 0x00040000:
                try: body = zlib.decompress(body[4:])
                except Exception: body = b""
            rec = Record(sig, formid, flags, parse_fields(body))
            records[formid] = rec
            low24[formid & 0xFFFFFF] = rec
            by_sig[sig].append(rec)
            i += 24 + dsize

tes4_size = struct.unpack_from("<I", data, 4)[0]
# master list from TES4 for index mapping
masters = []
for s, d in parse_fields(data[24:24+tes4_size]):
    if s == b"MAST":
        masters.append(d.split(b"\0")[0].decode())
print(f"# masters ({len(masters)}): {masters}", file=sys.stderr)
walk(data, 24 + tes4_size, len(data))
print(f"# parsed {len(records)} records", file=sys.stderr)

def rec_of(fid):
    return records.get(fid) or low24.get(fid & 0xFFFFFF)

def edid(rec):
    for s, d in rec.fields:
        if s == b"EDID":
            return d.split(b"\0")[0].decode("cp1251", "replace")
    return None

EDID = {fid: edid(r) for fid, r in records.items()}

def name(fid):
    if fid in (0, 0xFFFFFFFF): return "None"
    r = rec_of(fid)
    if r:
        return f"{EDID.get(r.formid) or hex(fid)} [{r.sig.decode()} {fid & 0xFFFFFF:06X}]"
    return f"?? {fid & 0xFFFFFF:06X} (idx {fid >> 24:02X})"

def full(rec):
    for s, d in rec.fields:
        if s == b"FULL":
            return d.split(b"\0")[0].decode("cp1251", "replace")
    return ""

def vmad_props(rec):
    for s, d in rec.fields:
        if s != b"VMAD": continue
        try:
            off = 0
            ver, objfmt, nscr = struct.unpack_from("<hhH", d, off); off += 6
            res = []
            for _ in range(nscr):
                sl = struct.unpack_from("<H", d, off)[0]; off += 2
                sn = d[off:off+sl].decode("cp1251","replace"); off += sl
                npr = struct.unpack_from("<H", d, off)[0]; off += 2
                props = []
                for _ in range(npr):
                    pl = struct.unpack_from("<H", d, off)[0]; off += 2
                    pn = d[off:off+pl].decode("cp1251","replace"); off += pl
                    pt, ps = struct.unpack_from("<BB", d, off); off += 2
                    if pt == 1:
                        # objfmt 1: (formid u32, alias i16, unused u16); objfmt 2: (unused u16, alias i16, formid u32)
                        if objfmt == 1:
                            fid = struct.unpack_from("<I", d, off)[0]
                        else:
                            fid = struct.unpack_from("<I", d, off+4)[0]
                        off += 8; props.append((pn, ("obj", fid)))
                    elif pt == 2:
                        l = struct.unpack_from("<H", d, off)[0]; off += 2
                        props.append((pn, ("str", d[off:off+l].decode("cp1251","replace")))); off += l
                    elif pt == 3:
                        props.append((pn, ("int", struct.unpack_from("<i", d, off)[0]))); off += 4
                    elif pt == 4:
                        props.append((pn, ("float", struct.unpack_from("<f", d, off)[0]))); off += 4
                    elif pt == 5:
                        props.append((pn, ("bool", d[off]))); off += 1
                    else:
                        props.append((pn, ("?%d" % pt,))); raise StopIteration
                res.append((sn, props))
            return res
        except Exception as e:
            return [("<vmad parse err: %s>" % e, [])]
    return []

def effects(rec):
    effs, cur = [], None
    for s, d in rec.fields:
        if s == b"EFID":
            if cur: effs.append(cur)
            cur = {"mgef": struct.unpack("<I", d)[0], "mag": 0, "area": 0, "dur": 0, "conds": []}
        elif s == b"EFIT" and cur:
            cur["mag"], cur["area"], cur["dur"] = struct.unpack_from("<fii", d, 0)
        elif s == b"CTDA" and cur:
            cur["conds"].append(ctda(d))
    if cur: effs.append(cur)
    return effs

FUNC = {0x1B:"GetIsID",0x53:"GetStageDone",0x84:"HasSpell",0x48:"GetActorValue",
        0x8A:"GetIsRace",0x22C:"HasKeyword",0x1C0:"HasPerk",0x448>>0:"?"}
FUNC = {27:"GetIsID",83:"GetStageDone",132:"HasSpell",72:"GetActorValue",
        138:"GetIsRace",560:"HasKeyword",448:"HasPerk",264:"HasSpell?/264",
        59:"GetStageDone/59",294:"GetVMQuestVariable"}
def ctda(d):
    op = d[0]; comp = struct.unpack_from("<f", d, 4)[0]
    fn = struct.unpack_from("<H", d, 8)[0]
    p1, p2 = struct.unpack_from("<II", d, 12)
    fname = FUNC.get(fn, f"func{fn}")
    p1s = name(p1) if rec_of(p1) else (hex(p1) if p1 > 0x400 else p1)
    return f"{fname}({p1s}, {p2}) op{op:02X} val={comp}"

def spit_type(rec):
    for s, d in rec.fields:
        if s == b"SPIT" and len(d) >= 12:
            t = struct.unpack_from("<I", d, 8)[0]
            return {0:"Spell",1:"Disease",2:"Power",3:"LesserPower",4:"Ability",5:"Poison",6:"Enchanting",10:"Addiction",11:"Voice"}.get(t, t)
    return "?"

apply_names = ["Ataxia","BrainRot","RockJoint","WitBane","Rattles","BoneBreakFever"]

print("="*78); print("1. RFAB_Effect_ApplyDisease_* MGEFs + their Disease target"); print("="*78)
apply_mgefs = {}
for r in by_sig[b"MGEF"]:
    vp = vmad_props(r)
    is_apply = any(sn.lower() == "rfab_diseaseapply" for sn, _ in vp)
    if not is_apply and "ApplyDisease" not in (EDID.get(r.formid) or ""):
        continue
    apply_mgefs[r.formid] = r
    dis = None
    for sn, props in vp:
        for pn, v in props:
            if pn.lower() == "disease" and v[0] == "obj":
                dis = v[1]
    print(f"\n  {EDID.get(r.formid)} [{r.formid&0xFFFFFF:06X}]  \"{full(r)}\"")
    print(f"    script Disease -> {name(dis) if dis else '(unresolved)'}")

print("\n" + "="*78); print("2. RAW scan: every record whose bytes contain an ApplyDisease MGEF formid"); print("="*78)
pats = {struct.pack("<I", f): f for f in apply_mgefs}
for fid, r in records.items():
    if fid in apply_mgefs: continue
    hits = set()
    for s, d in r.fields:
        for pat, t in pats.items():
            if pat in d: hits.add((t, s.decode()))
    if hits:
        print(f"\n  {name(fid)}  \"{full(r)}\"  type={spit_type(r) if r.sig==b'SPEL' else ''}")
        for t, fs in sorted(hits):
            print(f"      contains {name(t)}  in .{fs}")
        if r.sig in (b"SPEL", b"ENCH", b"SCRL", b"ALCH"):
            for e in effects(r):
                print(f"        eff {name(e['mgef'])} mag={e['mag']} dur={e['dur']} area={e['area']}")
                for c in e["conds"]: print(f"           if {c}")

print("\n" + "="*78); print("3. RFAB_Spell_DiseaseAttacks / Queen + what references them"); print("="*78)
for r in list(by_sig[b"SPEL"]):
    e = EDID.get(r.formid) or ""
    if "DiseaseAttack" not in e and e not in ("RFAB_Spell_DiseaseAttacks","RFAB_Spell_DiseaseAttacksQueen"):
        continue
    print(f"\n  {e} [{r.formid&0xFFFFFF:06X}]  \"{full(r)}\"  type={spit_type(r)}")
    for x in effects(r):
        print(f"    eff {name(x['mgef'])} mag={x['mag']} dur={x['dur']} area={x['area']}")
        for c in x["conds"]: print(f"       if {c}")
    pat = struct.pack("<I", r.formid)
    refs = set()
    for fid2, r2 in records.items():
        if fid2 == r.formid: continue
        for s, d in r2.fields:
            if pat in d: refs.add((fid2, s.decode()))
    for fid2, fs in sorted(refs):
        print(f"      <- {name(fid2)}  .{fs}")

print("\n" + "="*78); print("4. RFAB_Disease_* records (effects / conditions / magnitudes)"); print("="*78)
for r in by_sig[b"SPEL"]:
    e = EDID.get(r.formid) or ""
    if not (e.startswith("RFAB_Disease_") or e == "DLC2DiseaseDroops"): continue
    print(f"\n  {e} [{r.formid&0xFFFFFF:06X}]  \"{full(r)}\"  type={spit_type(r)}")
    for x in effects(r):
        mn = name(x["mgef"]); mr = rec_of(x["mgef"])
        av = arch = None
        if mr:
            for s, d in mr.fields:
                if s == b"DATA" and len(d) >= 72:
                    arch = struct.unpack_from("<i", d, 64)[0]
                    av = struct.unpack_from("<i", d, 68)[0]
        print(f"    eff {mn} mag={x['mag']} dur={x['dur']} area={x['area']}  (arch={arch} av={av})")
        for c in x["conds"]: print(f"       if {c}")

print("\n" + "="*78); print("5. what references each RFAB_Disease_* (contract paths)"); print("="*78)
dz_ids = {r.formid for r in by_sig[b"SPEL"] if (EDID.get(r.formid) or "").startswith("RFAB_Disease_")}
pats2 = {struct.pack("<I", f): f for f in dz_ids}
hitmap = collections.defaultdict(set)
for fid, r in records.items():
    if fid in dz_ids: continue
    for s, d in r.fields:
        for pat, t in pats2.items():
            if pat in d: hitmap[t].add((fid, r.sig.decode(), s.decode()))
for t in sorted(dz_ids):
    print(f"\n  {name(t)}")
    for fid, rs, fs in sorted(hitmap.get(t, [])):
        print(f"      <- {name(fid)}  ({rs}.{fs})  \"{full(records[fid])}\"")

print("\n" + "="*78); print("6. DiseaseResist fortify + toggles"); print("="*78)
for r in by_sig[b"MGEF"]:
    for s, d in r.fields:
        if s == b"DATA" and len(d) >= 72:
            av = struct.unpack_from("<i", d, 68)[0]
            if av in (0x99, 153):
                arch = struct.unpack_from("<i", d, 64)[0]
                print(f"  MGEF {EDID.get(r.formid)} [{r.formid&0xFFFFFF:06X}] arch={arch} \"{full(r)}\" -> DiseaseResist")
for r in by_sig[b"GLOB"]:
    e = EDID.get(r.formid) or ""
    if "disease" in e.lower():
        val = None
        for s, d in r.fields:
            if s == b"FLTV": val = struct.unpack_from("<f", d, 0)[0]
        print(f"  GLOB {e} [{r.formid&0xFFFFFF:06X}] = {val}")
