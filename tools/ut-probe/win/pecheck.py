#!/usr/bin/env python3
"""pecheck.py FILE...: for each Windows executable or DLL, print its machine, 32/64-bit, whether it
is a .NET assembly (has a CLR header), and its size. Reads only."""
import struct, sys

MACHINES = {0x14c: "x86", 0x8664: "x86-64", 0xaa64: "ARM64", 0xa641: "ARM64EC"}
for path in sys.argv[1:]:
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    mach = struct.unpack_from("<H", d, pe + 4)[0]
    magic = struct.unpack_from("<H", d, pe + 24)[0]
    dirs = pe + 24 + (112 if magic == 0x20b else 96)
    clr_size = struct.unpack_from("<II", d, dirs + 14 * 8)[1]
    kind = ".NET" if clr_size else "native"
    bits = "64-bit" if magic == 0x20b else "32-bit"
    print(f"{path:30s} {MACHINES.get(mach, hex(mach)):8s} {bits} {kind:6s} {len(d) // 1024} KB")
