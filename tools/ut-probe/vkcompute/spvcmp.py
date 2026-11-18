#!/usr/bin/env python3
"""Compare two spvrun output dumps (the same shader and inputs on two drivers). Runs on the PC.

  python3 spvcmp.py DIR_A DIR_B [--float | --rgba8 | --int] [--tol 1e-4]

--float: 32-bit floats, equal if |a-b| <= tol * max(1, |a|, |b|) (or both NaN).
--rgba8: packed 8-bit channels, equal if every channel differs by at most 1.
--int:   exact 32-bit words.
Prints, per output buffer, how many elements differ and the first few, and exits 1 if any differ.
"""
import argparse
import math
import os
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--float", action="store_true")
    g.add_argument("--rgba8", action="store_true")
    g.add_argument("--int", action="store_true")
    ap.add_argument("--tol", type=float, default=1e-4)
    args = ap.parse_args()
    mode = "rgba8" if args.rgba8 else "int" if args.int else "float"

    worst = 0
    for name in sorted(os.listdir(args.a)):
        pa, pb = os.path.join(args.a, name), os.path.join(args.b, name)
        if not os.path.exists(pb):
            print(f"{name}: missing in {args.b}")
            worst = 1
            continue
        da, db = open(pa, "rb").read(), open(pb, "rb").read()
        n = min(len(da), len(db)) // 4
        bad, shown, maxerr = 0, 0, 0.0
        if mode == "float":
            va, vb = struct.unpack(f"<{n}f", da[:4 * n]), struct.unpack(f"<{n}f", db[:4 * n])
            for i, (x, y) in enumerate(zip(va, vb)):
                if math.isnan(x) and math.isnan(y):
                    continue
                err = abs(x - y) / max(1.0, abs(x), abs(y)) if not (math.isnan(x) or math.isnan(y)) else math.inf
                maxerr = max(maxerr, err)
                if err > args.tol:
                    bad += 1
                    if shown < 5:
                        print(f"  {name}[{i}]: {x!r} vs {y!r}")
                        shown += 1
        else:
            va, vb = struct.unpack(f"<{n}I", da[:4 * n]), struct.unpack(f"<{n}I", db[:4 * n])
            for i, (x, y) in enumerate(zip(va, vb)):
                if mode == "int":
                    differ = x != y
                else:
                    differ = any(abs(((x >> s) & 255) - ((y >> s) & 255)) > 1 for s in (0, 8, 16, 24))
                if differ:
                    bad += 1
                    if shown < 5:
                        print(f"  {name}[{i}]: 0x{x:08x} vs 0x{y:08x}")
                        shown += 1
        extra = f", largest relative error {maxerr:.3g}" if mode == "float" else ""
        print(f"{name}: {n} elements, {bad} differ ({mode}){extra}")
        worst |= bad > 0
    return worst


if __name__ == "__main__":
    sys.exit(main())
