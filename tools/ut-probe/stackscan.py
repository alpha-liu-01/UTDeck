#!/usr/bin/env python3
"""UTDeck: a rough backtrace of a stuck process, without a debugger.

    stackscan.py PID [LIB-SUBSTRING...]

For every thread, reads the stack from the stack pointer in /proc/PID/task/TID/syscall through
/proc/PID/mem (same user, no Yama on this kernel) and prints the 8-byte values that point into
executable code of the mapped files, as "library+offset": the return addresses on the stack, newest
first, plus some stale ones (it is a scan, not an unwinder). With LIB-SUBSTRINGs, only those
libraries are listed. Offsets go to addr2line on an unstripped build.
"""
import os
import struct
import sys

pid = int(sys.argv[1])
only = sys.argv[2:]
maps = []
for line in open(f"/proc/{pid}/maps"):
    f = line.split()
    if len(f) < 6 or "x" not in f[1]:
        continue
    a, b = (int(x, 16) for x in f[0].split("-"))
    maps.append((a, b, int(f[2], 16), f[5]))


def where(v):
    for a, b, off, name in maps:
        if a <= v < b:
            return name, v - a + off
    return None


mem = open(f"/proc/{pid}/mem", "rb", buffering=0)
for tid in sorted(os.listdir(f"/proc/{pid}/task"), key=int):
    t = f"/proc/{pid}/task/{tid}"
    comm = open(f"{t}/comm").read().strip()
    sc = open(f"{t}/syscall").read().split()
    if len(sc) < 3 or sc[0] == "running":
        continue
    sp, pc = int(sc[-2], 16), int(sc[-1], 16)
    try:
        mem.seek(sp)
        data = mem.read(48 * 1024)
    except OSError:
        continue
    seen, out = set(), []
    w = where(pc)
    if w:
        out.append(f"pc {os.path.basename(w[0])}+{w[1]:#x}")
    for i in range(0, len(data) - 7, 8):
        v = struct.unpack_from("<Q", data, i)[0]
        w = where(v)
        if not w:
            continue
        name = os.path.basename(w[0])
        if only and not any(s in name for s in only):
            continue
        key = (name, w[1])
        if key in seen:
            continue
        seen.add(key)
        out.append(f"{name}+{w[1]:#x}")
        if len(out) >= 14:
            break
    print(f"{tid} {comm} syscall {sc[0]}: " + " | ".join(out))
