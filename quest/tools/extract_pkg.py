#!/usr/bin/env python3
"""Extract Outrage installer archives (.pkg, magic "GKPO") from a Descent 3 CD.

Format (little-endian), reverse-engineered from the Descent 3 Mercenary disc:
    char[4]  magic "GKPO"
    u32      entry count
    entry * count:
        u32  dir length,  char[dir length]  NUL-terminated dir ("" or "movies\\")
        u32  name length, char[name length] NUL-terminated file name
        u32  data size
        u64  FILETIME (last modified)
        u8[data size]  file contents, stored uncompressed

    extract_pkg.py OUTDIR ARCHIVE.pkg [ARCHIVE.pkg ...]   # later archives win
    extract_pkg.py --list ARCHIVE.pkg
"""
import argparse
import datetime
import os
import struct
import sys


def entries(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        magic, count = struct.unpack("<4sI", f.read(8))
        if magic != b"GKPO":
            sys.exit(f"{path}: not a GKPO package")
        for _ in range(count):
            (dlen,) = struct.unpack("<I", f.read(4))
            d = f.read(dlen).rstrip(b"\0").decode("latin-1")
            (nlen,) = struct.unpack("<I", f.read(4))
            n = f.read(nlen).rstrip(b"\0").decode("latin-1")
            length, filetime = struct.unpack("<IQ", f.read(12))
            rel = (d + n).replace("\\", "/")
            data_start = f.tell()
            yield rel, data_start, length, filetime, f
            f.seek(data_start + length)  # absolute: the consumer moves the file position
        if f.tell() != size:
            sys.exit(f"{path}: parsed {f.tell()} of {size} bytes; unknown layout")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("paths", nargs="+", help="OUTDIR then archives, or archives with --list")
    args = ap.parse_args()
    outdir, archives = (None, args.paths) if args.list else (args.paths[0], args.paths[1:])
    if not archives:
        ap.error("no archives given")

    for archive in archives:
        for rel, offset, length, filetime, f in entries(archive):
            if args.list:
                print(f"{length:>12}  {rel}")
                continue
            dest = os.path.join(outdir, rel)
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            f.seek(offset)
            with open(dest, "wb") as out:
                remaining = length
                while remaining:
                    chunk = f.read(min(remaining, 1 << 20))
                    out.write(chunk)
                    remaining -= len(chunk)
            mtime = (filetime - 116444736000000000) / 1e7  # FILETIME -> Unix time
            os.utime(dest, (mtime, mtime))
        print(f"extracted {archive}", file=sys.stderr)


if __name__ == "__main__":
    main()
