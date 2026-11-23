#!/usr/bin/env python3
"""UTDeck stage 3.2b: check a game folder before its first start. Reads only.

    python3 -I gamecheck.py GAME_DIR              the full check (our own tests)
    python3 -I gamecheck.py --programs GAME_DIR   the Windows files and imports only (the app)

Prints:
- every Windows executable and DLL with its machine (x86, x86-64, ARM64) and bitness;
- the DLLs that the .exe files import (the graphics API: d3d9, d3d11, d3d12, opengl32, ...);
- steam_api.dll / steam_api64.dll: whether it carries an Authenticode signature by Valve (a Steam
  emulator put in its place is not signed);
- any known DRM-bypass marker found in any file of the folder (Steam emulators, crack groups),
  with the file and the marker. We do not run games with one in our own tests (stage 3 rules).

--programs (stage 4.5): only the first two parts, for UTDeck's "Add a game" (its list of programs
to choose from). The app does not check for DRM bypasses (the maintainer, 2026-11-23: UTDeck, like
Wine itself, does not police what its users run); this mode also does not read every file of the
game, which for a large game took long.
"""
import os
import struct
import sys

MACHINES = {0x14c: "x86", 0x8664: "x86-64", 0xaa64: "ARM64", 0xa641: "ARM64EC"}
# Distinctive strings only: short tags such as "CPY" or "3DM" match ordinary data, and so does a
# bare "RELOADED" (PvZ Replanted: the identifier PVZ_RELOADED_STEAM_CLIENT).
MARKERS = [b"SmartSteamEmu", b"Goldberg", b"steam_emu.ini", b"steam_settings", b"CreamAPI",
           b"cream_api", b"ColdClientLoader", b"ALI213", b"CODEX", b"*!ReLOADeD!*", b"-RELOADED", b"RELOADED.nfo",
           b"SKIDROW", b"PLAZA", b"EMPRESS", b"TENOKE", b"DARKSiDERS", b"Universal Cracker",
           b"Steamless", b"SteamStub unpacked", b"OnlineFix", b"nemirtingas"]
CHUNK = 8 << 20


def pe_info(d):
    """(machine, bits, [imported DLL names]) of a PE image, or None."""
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
            names.append(d[n:d.index(b"\0", n)].decode("latin-1"))
        o += 20
    return MACHINES.get(mach, hex(mach)), "64-bit" if magic == 0x20b else "32-bit", names, d, dirs


def signed_by_valve(d, dirs):
    """True if the PE's certificate table names Valve (a plain byte search of the signer blob)."""
    cert_off, cert_size = struct.unpack_from("<II", d, dirs + 4 * 8)
    return bool(cert_size) and b"Valve" in d[cert_off:cert_off + cert_size]


def scan(path):
    """Markers found in a file, read in chunks (game data files can be several GB)."""
    found, tail = set(), b""
    with open(path, "rb") as f:
        while True:
            block = f.read(CHUNK)
            if not block:
                break
            data = tail + block
            for m in MARKERS:
                if m in data:
                    found.add(m.decode())
            tail = data[-64:]
    return found


args = sys.argv[1:]
programs_only = "--programs" in args
args = [a for a in args if a != "--programs"]
root = args[0]
pes, hits = [], []
for dirpath, _, files in os.walk(root):
    for name in sorted(files):
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        if name.lower().endswith((".exe", ".dll", ".asi", ".sys")):
            d = open(path, "rb").read()
            info = pe_info(d)
            if info:
                pes.append((rel, info))
        if programs_only:
            continue
        try:
            m = scan(path)
        except OSError:
            continue
        if m:
            hits.append((rel, sorted(m)))

print("Windows files:")
for rel, (mach, bits, _, d, _) in pes:
    print(f"  {mach:8s} {bits}  {len(d) // 1024:7d} KB  {rel}")
print("Imports of the .exe files:")
for rel, (_, _, names, _, _) in pes:
    if rel.lower().endswith(".exe"):
        print(f"  {rel}: {' '.join(sorted(set(n.lower() for n in names)))}")
if programs_only:
    sys.exit(0)
for rel, (_, _, _, d, dirs) in pes:
    if os.path.basename(rel).lower() in ("steam_api.dll", "steam_api64.dll"):
        print(f"Steam API: {rel}: {'signed by Valve' if signed_by_valve(d, dirs) else 'NOT signed by Valve'}")
print("DRM-bypass markers:", "none" if not hits else "")
for rel, m in hits:
    print(f"  {rel}: {', '.join(m)}")
