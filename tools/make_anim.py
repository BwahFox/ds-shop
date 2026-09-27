#!/usr/bin/env python3
"""Pack the DSi Shop's own sprites into /ds-shop/anim.bin for the DS.

The shop draws its download animation (Mario, Luigi, Peach and Toad throwing
data into a box that fills up) and its "please wait" icon with these, when
the file is on the SD card. Without it, it falls back to its own drawn box
and spinner. The sprites are Nintendo's, so they aren't in this repo: you
take them from your own DSi's copy of the DSi Shop.

    tools/make_anim.py <layout dir> sdcard/ds-shop/anim.bin

<layout dir> holds the DSi Shop's layout NARCs unpacked, one folder each
(from layout/cmn/*.szs in the app's NitroFS, Yaz0-compressed NARCs):
    shop_progressbar/ued_progress_bar.{NCLR,ncgr,ncer}   (NCLR is *_obj.NCLR)
    wait_icon/ued_wait_icon.{NCLR,ncgr,ncer}

These are Nintendo's NNS G2D formats, and their tiles and sprite attributes
are already what the DS sprite hardware wants (4bpp, 1D mapping with a
128-byte boundary), so they're stored as they are and the DS copies them
straight to sprite VRAM.

File layout (little-endian):
    0   'DSAN'
    4   u32 version (1)
    8   u32 number of sets
    then per set:
        char[8] name ("dl", "wait"), zero-padded
        u32 tile data size in bytes
        u32 number of cells
        u32 number of sprites (OAM entries) in all the cells
        512 bytes palette (256 BGR555 colours)
        tile data
        per cell: u16 first sprite, u16 sprite count
        per sprite: u16 attr0, attr1, attr2 (y in attr0 and x in attr1 are
            relative to the cell's centre); padded to 4 bytes
"""
import os
import struct
import sys

SETS = [  # name, folder, file stem
    ("dl", "shop_progressbar", "ued_progress_bar"),
    ("wait", "wait_icon", "ued_wait_icon"),
]


def sections(data):
    hdr_size, n = struct.unpack_from("<HH", data, 0x0C)
    out, p = {}, hdr_size
    for _ in range(n):
        magic = data[p:p + 4][::-1].decode()          # stored reversed
        size = struct.unpack_from("<I", data, p + 4)[0]
        out[magic] = data[p:p + size]
        p += size
    return out


def find(folder, stem, ext):
    for n in sorted(os.listdir(folder)):
        low = n.lower()
        if low.endswith(ext) and low.startswith(stem.lower()):
            return os.path.join(folder, n)
    sys.exit(f"no {stem}*{ext} in {folder}")


def palette(path):
    s = sections(open(path, "rb").read())["PLTT"]
    size, off = struct.unpack_from("<II", s, 0x10)
    raw = s[0x18:0x18 + size]
    return (raw + bytes(512))[:512]


def tiles(path):
    s = sections(open(path, "rb").read())["CHAR"]
    depth, = struct.unpack_from("<I", s, 0x0C)
    if depth != 3:
        sys.exit(f"{path}: expected 4bpp tiles")
    size, off = struct.unpack_from("<II", s, 0x18)
    return s[8 + off:8 + off + size]


def cells(path):
    s = sections(open(path, "rb").read())["CEBK"]
    n, attr_type, cell_off, mapping = struct.unpack_from("<HHII", s, 8)
    if mapping != 2:
        sys.exit(f"{path}: expected 1D/128-byte tile mapping, got {mapping}")
    base = 8 + cell_off
    esz = 16 if attr_type == 1 else 8
    oam_base = base + n * esz
    out = []
    for i in range(n):
        n_oam, _, oam_off = struct.unpack_from("<HHI", s, base + i * esz)
        out.append([struct.unpack_from("<HHH", s, oam_base + oam_off + j * 6) for j in range(n_oam)])
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1:]
    out = [b"DSAN", struct.pack("<II", 1, len(SETS))]
    for name, folder, stem in SETS:
        d = os.path.join(src, folder)
        pal = palette(find(d, stem, ".nclr"))
        tl = tiles(find(d, stem, ".ncgr"))
        cl = cells(find(d, stem, ".ncer"))
        n_spr = sum(len(c) for c in cl)
        out.append(name.encode().ljust(8, b"\0"))
        out.append(struct.pack("<III", len(tl), len(cl), n_spr))
        out += [pal, tl]
        first = 0
        for c in cl:
            out.append(struct.pack("<HH", first, len(c)))
            first += len(c)
        spr = b"".join(struct.pack("<HHH", *a) for c in cl for a in c)
        out.append(spr + bytes(-len(spr) % 4))
        print(f"{name}: {len(cl)} cells, {n_spr} sprites, {len(tl)} bytes of tiles")
    data = b"".join(out)
    with open(dst, "wb") as f:
        f.write(data)
    print(f"wrote {dst} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
