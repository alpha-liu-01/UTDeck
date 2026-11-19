#!/usr/bin/env python3
"""memgrep.py PID PATTERN [CONTEXT] [OUTDIR]: search a running process's readable memory for
PATTERN, as UTF-8 and as UTF-16LE, and print (or save under OUTDIR) CONTEXT bytes from each hit.
Reads only (/proc/PID/mem, same user, no Yama restriction). Used in stage 2.4 to collect 3DMark
11's test results from its GUI while the GUI was stuck before showing them."""
import os, re, sys

pid, pat = int(sys.argv[1]), sys.argv[2]
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 4000
out = sys.argv[4] if len(sys.argv) > 4 else None
needles = [(pat.encode(), "utf-8"), (pat.encode("utf-16-le"), "utf-16-le")]
hits = 0
seen = set()
mem = open(f"/proc/{pid}/mem", "rb", 0)
for line in open(f"/proc/{pid}/maps"):
    f = line.split()
    if "r" not in f[1]:
        continue
    a, b = (int(x, 16) for x in f[0].split("-"))
    if b - a > 512 << 20:
        continue
    try:
        mem.seek(a)
        data = mem.read(b - a)
    except (OSError, ValueError, OverflowError):
        continue
    for needle, enc in needles:
        for m in re.finditer(re.escape(needle), data):
            start = max(0, m.start() - 200)
            chunk = data[start:m.start() + ctx]
            text = chunk.decode(enc, errors="replace")
            key = text[:400]
            if key in seen:
                continue
            seen.add(key)
            hits += 1
            if out:
                os.makedirs(out, exist_ok=True)
                with open(os.path.join(out, f"hit{hits:03d}-{enc}-{a + m.start():x}.txt"), "w") as o:
                    o.write(text)
            else:
                print(f"=== hit {hits} at {a + m.start():#x} ({enc}, {f[-1] if len(f) > 5 else 'anon'})")
                print(text)
print(f"{hits} distinct hits", file=sys.stderr)
