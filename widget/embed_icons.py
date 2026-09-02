#!/usr/bin/env python3
"""Embed widget icons into an uncompressed (FWS) SWF.

For each PNG in the icons folder, adds a DefineBitsLossless2 tag (32-bit,
premultiplied ARGB) and exports it for ActionScript as "ico_<name>" via one
ExportAssets tag, spliced in just before the End tag. The FileLength header
field is rewritten. Run after FFDec -importScript, before the CWS re-pack.

Usage: embed_icons.py <in.swf> <out.swf> <icons_dir>
"""
import sys, os, zlib, struct, io

CHAR_BASE = 2000   # icon character IDs: 2000, 2001, ...


def define_bits_lossless2(char_id, img_rgba, w, h):
    # PIX32 per SWF spec: alpha, red, green, blue - RGB premultiplied.
    out = bytearray()
    px = img_rgba
    for i in range(0, len(px), 4):
        r, g, b, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        if a != 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        out += bytes((a, r, g, b))
    body = struct.pack('<HBHH', char_id, 5, w, h) + zlib.compress(bytes(out), 9)
    return tag(36, body)


def export_assets(entries):
    body = struct.pack('<H', len(entries))
    for cid, name in entries:
        body += struct.pack('<H', cid) + name.encode('ascii') + b'\x00'
    return tag(56, body)


def tag(code, body):
    n = len(body)
    if n < 0x3f:
        return struct.pack('<H', (code << 6) | n) + body
    return struct.pack('<H', (code << 6) | 0x3f) + struct.pack('<I', n) + body


def png_rgba(path):
    # minimal: use PIL if available, else fail loudly
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    return im.tobytes(), im.width, im.height


def main():
    inp, outp, icodir = sys.argv[1], sys.argv[2], sys.argv[3]
    d = bytearray(open(inp, 'rb').read())
    assert d[:3] == b'FWS', 'expected uncompressed FWS SWF (run FFDec -decompress first)'

    # skip 8-byte header, then RECT, then FrameRate(2) + FrameCount(2)
    b = io.BytesIO(d[8:])
    first = b.read(1)[0]
    nbits = first >> 3
    rect_bytes = ((5 + 4 * nbits) + 7) // 8
    b.read(rect_bytes - 1)
    b.read(4)  # framerate + framecount
    tags_start = 8 + b.tell()

    # walk the tag stream; remember the last ShowFrame (code 1) - splice there
    # so the bitmaps are defined inside frame 1, before frame-1 init actions run
    pos = tags_start
    end_pos = None
    while pos < len(d):
        v = struct.unpack_from('<H', d, pos)[0]
        code, ln = v >> 6, v & 0x3f
        hlen = 2
        if ln == 0x3f:
            ln = struct.unpack_from('<I', d, pos + 2)[0]
            hlen = 6
        if code == 1:
            end_pos = pos          # last ShowFrame wins
        if code == 0:
            if end_pos is None:
                end_pos = pos
            break
        pos += hlen + ln
    assert end_pos is not None, 'no ShowFrame/End tag'

    files = sorted(f for f in os.listdir(icodir) if f.lower().endswith('.png'))
    blob = bytearray()
    exports = []
    for i, f in enumerate(files):
        cid = CHAR_BASE + i
        rgba, w, h = png_rgba(os.path.join(icodir, f))
        blob += define_bits_lossless2(cid, rgba, w, h)
        exports.append((cid, 'ico_' + os.path.splitext(f)[0]))
    blob += export_assets(exports)

    d[end_pos:end_pos] = blob
    struct.pack_into('<I', d, 4, len(d))   # FileLength
    open(outp, 'wb').write(d)
    print('embedded', len(files), 'icons:', ', '.join(n for _, n in exports))


if __name__ == '__main__':
    main()
