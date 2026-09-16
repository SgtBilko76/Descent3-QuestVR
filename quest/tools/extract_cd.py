#!/usr/bin/env python3
"""Extract files from a Descent 3 CD image, using only the standard library.

Handles cooked ISO 9660 images (2048-byte sectors) and raw images
(2352-byte sectors, Mode 1 or Mode 2 Form 1), which are often distributed
with an .iso extension. Prefers Joliet names when present.

    extract_cd.py IMAGE OUTDIR           # extract everything
    extract_cd.py IMAGE --list           # list files only
"""
import argparse
import os
import struct
import sys

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        size = os.fstat(self.f.fileno()).st_size
        self.f.seek(16 * 2352)
        raw = self.f.read(2352)
        if size % 2352 == 0 and raw[:12] == SYNC:
            mode = raw[15]
            # Mode 1: 16-byte header. Mode 2 Form 1: 16-byte header + 8-byte subheader.
            self.sector_size, self.data_offset = 2352, (16 if mode == 1 else 24)
        else:
            self.sector_size, self.data_offset = 2048, 0
        if self.sector(16)[1:6] != b"CD001":
            sys.exit("not an ISO 9660 image (no CD001 at sector 16)")

    def sector(self, lba):
        self.f.seek(lba * self.sector_size + self.data_offset)
        return self.f.read(2048)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])


def volume_descriptors(img):
    """Yield (type, sector bytes) until the terminator."""
    lba = 16
    while True:
        s = img.sector(lba)
        if s[1:6] != b"CD001" or s[0] == 255:
            return
        yield s[0], s
        lba += 1


def walk(img, extent, size, joliet, prefix=""):
    data = img.read(extent, size)
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:  # rest of this sector is padding
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + rec_len]
        pos += rec_len
        lba, length = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        flags, name_len = rec[25], rec[32]
        raw_name = rec[33:33 + name_len]
        if raw_name in (b"\x00", b"\x01"):  # "." and ".."
            continue
        name = raw_name.decode("utf-16-be") if joliet else raw_name.decode("ascii", "replace")
        name = name.split(";")[0]
        if not joliet and name.endswith("."):  # "FILE." -> "FILE"
            name = name[:-1]
        path = f"{prefix}{name}"
        if flags & 0x02:
            yield from walk(img, lba, length, joliet, path + "/")
        else:
            yield path, lba, length


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("outdir", nargs="?")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    if not args.list and not args.outdir:
        ap.error("OUTDIR is required unless --list is given")

    img = Image(args.image)
    root, joliet = None, False
    for vtype, s in volume_descriptors(img):
        if vtype == 1 and root is None:
            root = s[156:190]
        elif vtype == 2 and s[88:91] in (b"%/@", b"%/C", b"%/E"):  # Joliet SVD
            root, joliet = s[156:190], True
    if root is None:
        sys.exit("no primary volume descriptor")
    extent, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]

    total = 0
    for path, lba, length in walk(img, extent, size, joliet):
        total += length
        if args.list:
            print(f"{length:>12}  {path}")
            continue
        dest = os.path.join(args.outdir, path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as out:
            remaining = length
            while remaining:
                chunk = img.read(lba, min(remaining, 2048 * 512))
                out.write(chunk)
                lba += 512
                remaining -= len(chunk)
    print(f"{'listed' if args.list else 'extracted'} {total / 1e6:.1f} MB "
          f"({'Joliet' if joliet else 'ISO 9660'} names, {img.sector_size}-byte sectors)", file=sys.stderr)


if __name__ == "__main__":
    main()
