#!/usr/bin/env python3
"""UTDeck stage 3.S: make a compatibility tool Steam's default for Windows games, without its window.

    set-default.py [TOOL]               default TOOL: utdeck; "--show" prints the current mappings
    set-default.py --app APPID --remove the game back to Steam's own choice (its Linux build, if any)
    set-default.py --app APPID [TOOL]   force TOOL for one game, as its properties page does
                                        ("Force the use of a specific Steam Play compatibility
                                        tool"); for games that also have a Linux build, which the
                                        default (app "0") does not cover: Steam then installs the
                                        game's Windows build and starts it through TOOL

Runs on the phone, with Steam stopped (Steam rewrites config.vdf while it runs). Writes the entry
the settings page "Enable Steam Play for all other titles" writes: CompatToolMapping, app "0", into
~/.local/share/Steam/config/config.vdf, under InstallConfigStore/Software/Valve/Steam. Only that
block is touched; the old file is kept next to it as config.vdf.utdeck-bak, on the phone (the file
holds account data and never leaves the phone). Prints only the mapping block, nothing else of the
file.
"""
import os
import re
import shutil
import subprocess
import sys

CFG = os.path.expanduser("~/.local/share/Steam/config/config.vdf")
args = sys.argv[1:]
app, priority = "0", "75"            # what Steam's settings page writes for the default
if args[:1] == ["--app"]:
    if len(args) < 2 or not args[1].isdigit():
        sys.exit("usage: set-default.py --app APPID [TOOL]")
    app, priority, args = args[1], "250", args[2:]   # what a game's properties page writes
arg = args[0] if args else "utdeck"

if subprocess.run(["pgrep", "-u", str(os.getuid()), "-f", "[s]teamrtarm64/"],
                  stdout=subprocess.DEVNULL).returncode == 0 and arg != "--show":
    sys.exit("Steam is running: stop it first (steam-service.sh stop)")

lines = open(CFG, encoding="utf-8").read().split("\n")


def find_block(start, end, depth, key):
    """Index of the line `<depth tabs>"key"` followed by `<depth tabs>{`, and of its closing brace."""
    head = "\t" * depth + f'"{key}"'
    for i in range(start, end):
        if lines[i] == head and lines[i + 1] == "\t" * depth + "{":
            for j in range(i + 2, end):
                if lines[j] == "\t" * depth + "}":
                    return i, j
    return None


path = [(0, "InstallConfigStore"), (1, "Software"), (2, "Valve"), (3, "Steam")]
lo, hi = 0, len(lines)
for depth, key in path:
    b = find_block(lo, hi, depth, key)
    if not b:
        sys.exit(f"config.vdf: no {key} block where expected; not touching it")
    lo, hi = b
m = find_block(lo, hi, 4, "CompatToolMapping")

if arg == "--show":
    print("\n".join(lines[m[0]:m[1] + 1]) if m else "no CompatToolMapping block")
    sys.exit(0)
if arg == "--remove":
    # --app APPID --remove: the game goes back to Steam's own choice (its Linux build, if it has
    # one; stage 3.7).
    if app == "0":
        sys.exit("--remove is for one game: set-default.py --app APPID --remove")
    old = find_block(m[0] + 2, m[1], 5, app) if m else None
    if not old:
        sys.exit(f"no entry for {app}; nothing to remove")
    del lines[old[0]:old[1] + 1]
    shutil.copy2(CFG, CFG + ".utdeck-bak")
    tmp = CFG + ".utdeck-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    os.replace(tmp, CFG)
    m = find_block(lo, len(lines), 4, "CompatToolMapping")
    print("\n".join(lines[m[0]:m[1] + 1]))
    sys.exit(0)
if not re.fullmatch(r"[A-Za-z0-9_.-]+", arg):
    sys.exit("tool name: letters, digits, _ . - only")

t = "\t"
entry = [t * 5 + f'"{app}"', t * 5 + "{", t * 6 + f'"name"\t\t"{arg}"', t * 6 + '"config"\t\t""',
         t * 6 + f'"priority"\t\t"{priority}"', t * 5 + "}"]
if m:
    old = find_block(m[0] + 2, m[1], 5, app)
    if old:
        del lines[old[0]:old[1] + 1]
        m = (m[0], m[1] - (old[1] + 1 - old[0]))
    lines[m[0] + 2:m[0] + 2] = entry
else:
    lines[lo + 2:lo + 2] = [t * 4 + '"CompatToolMapping"', t * 4 + "{"] + entry + [t * 4 + "}"]

shutil.copy2(CFG, CFG + ".utdeck-bak")
tmp = CFG + ".utdeck-tmp"
with open(tmp, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
os.replace(tmp, CFG)
m = find_block(lo, len(lines), 4, "CompatToolMapping")
print("\n".join(lines[m[0]:m[1] + 1]))
