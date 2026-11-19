#!/usr/bin/env python3
"""Unpack the file stream InstallShield appends to some setup.exe files (seen in 3DMark 11's
installer, InstallShield 2010-era): a 32-bit file count, then for each file four UTF-16 strings
(name, path, version, size in decimal) and the file's bytes. Runs on the PC.

    isstream.py STREAM OUTDIR      (STREAM: the overlay, e.g. extracted with `7z x setup.exe "[0]"`)
Lists every file; writes them under OUTDIR at their path. Reads only; runs nothing.
"""
import os, struct, sys

def u16(data, pos):
    end = pos
    while data[end:end + 2] != b"\0\0":
        end += 2
    return data[pos:end].decode("utf-16-le"), end + 2

def main():
    data = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    count = struct.unpack_from("<I", data, 0)[0]
    pos = 4
    for _ in range(count):
        name, pos = u16(data, pos)
        path, pos = u16(data, pos)
        version, pos = u16(data, pos)
        size_s, pos = u16(data, pos)
        size = int(size_s)
        body = data[pos:pos + size]
        pos += size
        rel = path.replace("\\", "/")
        dest = os.path.normpath(os.path.join(out, rel))
        if not dest.startswith(os.path.normpath(out) + os.sep):
            raise SystemExit(f"refusing path outside OUTDIR: {path}")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(body)
        print(f"{size:>12}  {path}  ({name}, {version}) {body[:4]!r}")
    print(f"{count} files, {pos} of {len(data)} bytes used")

if __name__ == "__main__":
    main()
