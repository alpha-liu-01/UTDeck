#!/usr/bin/env python3
"""UTDeck stage 4.10: is a Windows program a 32-bit x86 .NET program? Reads only.

    python3 -I dotnet32.py PROGRAM.exe     prints "yes" or "no"

game-run.sh asks it before a game starts: such a program (its .exe imports mscoree.dll, .NET's
loader; it runs on Wine Mono) gets FEX for its 32-bit code instead of Box64, our default. Celeste
(32-bit .NET) stopped within seconds on Box64: Mono's check of its threads' states failed ("Cannot
transition thread ... from RUNNING with DONE_BLOCKING"); on FEX, Proton's Wine's default on ARM64, it
plays. The PE reading is gamecheck.py's, cut down to the header and the import table.
"""
import struct
import sys


def imports_of(d):
    """(machine, [imported DLL names, lower case]) of a PE image, or None."""
    if d[:2] != b"MZ" or len(d) < 0x40:
        return None
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        return None
    mach, nsec = struct.unpack_from("<HH", d, pe + 4)
    opt_size = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dirs = opt + (112 if magic == 0x20b else 96)
    imp_rva = struct.unpack_from("<I", d, dirs + 8)[0]
    sec = opt + opt_size
    sections = [struct.unpack_from("<IIII", d, sec + 40 * i + 8) for i in range(nsec)]  # vsize, va, rawsize, rawptr

    def off(rva):
        for vsize, va, rsize, rptr in sections:
            if va <= rva < va + max(vsize, rsize):
                return rptr + rva - va
        return None

    names = []
    o = off(imp_rva) if imp_rva else None
    while o is not None and o + 20 <= len(d):
        name_rva = struct.unpack_from("<I", d, o + 12)[0]
        if not name_rva:
            break
        n = off(name_rva)
        if n is not None:
            names.append(d[n:d.index(b"\0", n)].decode("latin-1").lower())
        o += 20
    return mach, names


try:
    with open(sys.argv[1], "rb") as f:
        info = imports_of(f.read())
except (OSError, IndexError, struct.error, ValueError):
    info = None
print("yes" if info and info[0] == 0x14c and "mscoree.dll" in info[1] else "no")
