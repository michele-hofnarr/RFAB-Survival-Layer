# -*- coding: utf-8 -*-
"""Resolve an Address Library id to an RVA and disassemble the function."""
import struct
import sys
import io

DB = (r"R:\Games\The Elder Scrolls V Skyrim - Special Edition\MO2\mods"
      r"\Address Library for SKSE Plugins\SKSE\Plugins\version-1-5-97-0.bin")
EXE = r"R:\Games\The Elder Scrolls V Skyrim - Special Edition\SkyrimSE.exe"


class R:
    def __init__(self, d):
        self.d = d
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def u64(self):
        v = struct.unpack_from("<Q", self.d, self.p)[0]
        self.p += 8
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v


def load_db(path):
    r = R(open(path, "rb").read())
    fmt = r.i32()
    ver = [r.i32() for _ in range(4)]
    nlen = r.i32()
    name = r.d[r.p:r.p + nlen].decode("ascii", "replace")
    r.p += nlen
    ptr_size = r.i32()
    count = r.i32()
    out = {}
    pid = 0
    poff = 0
    for _ in range(count):
        t = r.u8()
        low = t & 0xF
        high = t >> 4
        if low == 0:
            ident = r.u64()
        elif low == 1:
            ident = pid + 1
        elif low == 2:
            ident = pid + r.u8()
        elif low == 3:
            ident = pid - r.u8()
        elif low == 4:
            ident = pid + r.u16()
        elif low == 5:
            ident = pid - r.u16()
        elif low == 6:
            ident = r.u16()
        elif low == 7:
            ident = r.u32()
        else:
            raise ValueError("bad low %d" % low)
        tmp = (poff // ptr_size) if (high & 8) else poff
        h = high & 7
        if h == 0:
            off = r.u64()
        elif h == 1:
            off = tmp + 1
        elif h == 2:
            off = tmp + r.u8()
        elif h == 3:
            off = tmp - r.u8()
        elif h == 4:
            off = tmp + r.u16()
        elif h == 5:
            off = tmp - r.u16()
        elif h == 6:
            off = r.u16()
        elif h == 7:
            off = r.u32()
        if high & 8:
            off *= ptr_size
        out[ident] = off
        pid = ident
        poff = off
    return fmt, ver, name, ptr_size, out


def rva_to_file(exe, rva):
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[pe:pe + 4] == b"PE\0\0"
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    sect = pe + 24 + optsz
    for i in range(nsec):
        s = sect + i * 40
        name = d[s:s + 8].rstrip(b"\0").decode()
        vsize = struct.unpack_from("<I", d, s + 8)[0]
        vaddr = struct.unpack_from("<I", d, s + 12)[0]
        rsize = struct.unpack_from("<I", d, s + 16)[0]
        raddr = struct.unpack_from("<I", d, s + 20)[0]
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return d, raddr + (rva - vaddr), name
    raise ValueError("rva not in any section")


def main():
    ident = int(sys.argv[1]) if len(sys.argv) > 1 else 19446
    nbytes = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    fmt, ver, name, psz, db = load_db(DB)
    out = io.open(r"C:\Users\Fredd\AppData\Local\Temp\claude\disasm_out.txt",
                  "w", encoding="utf-8")
    out.write("db format=%d version=%s name=%s ptr=%d entries=%d\n"
              % (fmt, ver, name, psz, len(db)))
    if ident not in db:
        out.write("id %d NOT in database\n" % ident)
        out.close()
        print("missing")
        return
    rva = db[ident]
    out.write("id %d -> RVA 0x%X\n" % (ident, rva))
    d, foff, sect = rva_to_file(EXE, rva)
    out.write("section %s, file offset 0x%X\n\n" % (sect, foff))

    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    code = d[foff:foff + nbytes]
    for ins in md.disasm(code, 0x140000000 + rva):
        out.write("%08X  %-22s %s %s\n"
                  % (ins.address - 0x140000000, ins.bytes.hex(), ins.mnemonic, ins.op_str))
        if ins.mnemonic == "ret":
            break
    out.close()
    print("written")


main()
