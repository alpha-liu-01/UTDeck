#!/usr/bin/env python3
"""Run a command and report wall time, its CPU time (user + system, children included) and
the CPU load that means, plus frames per second when a frame count is given.

  python3 run-timed.py [--frames N] -- command args...

Used in stage 0 to measure vkcube's cost per presentation path. Plain Python: the phone has
no bc and no GNU time.
"""
import os
import resource
import subprocess
import sys
import time


def main():
    args = sys.argv[1:]
    frames = None
    if args[:1] == ["--frames"]:
        frames = int(args[1])
        args = args[2:]
    if args[:1] == ["--"]:
        args = args[1:]
    start = time.monotonic()
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wall = time.monotonic() - start
    ru = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = ru.ru_utime + ru.ru_stime
    out = proc.stdout.strip().splitlines()
    for line in out[-4:]:
        print("  |", line)
    line = (f"exit {proc.returncode}, wall {wall:.2f} s, cpu {cpu:.2f} s "
            f"(user {ru.ru_utime:.2f}, sys {ru.ru_stime:.2f}) = {100 * cpu / wall:.0f} % of one core, "
            f"max RSS {ru.ru_maxrss // 1024} MB")
    if frames:
        line += f", {frames / wall:.1f} fps, {1000 * cpu / frames:.2f} ms CPU per frame"
    print(line)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
