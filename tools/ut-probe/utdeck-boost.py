#!/usr/bin/env python3
"""UTDeck stage 3.5: CPU boost for a running game, as the Dolphin port does for itself.

    utdeck-boost.py PID UTIL_MIN [LOG]

Every 2 s until PID ends, sets uclamp_min = UTIL_MIN (0-1024) on every thread of PID and its
descendants (the game's Wine processes) and of the wineserver of the same prefix (WINEPREFIX of
this process), so threads started later get it too. uclamp_min tells the scheduler a thread needs
at least that share of a CPU's performance: the frequency governor picks a higher clock from the
CPU's own table and the scheduler prefers the big cores. No clock or limit is set; the vendor's
thermal service keeps lowering the allowed maximum when it wants to. One sched_setattr call per
thread, keeping its scheduling class and priority (no root needed for one's own threads).
At the end it writes one line to LOG (or stdout): threads boosted, failures.
"""
import ctypes
import os
import struct
import sys
import time

libc = ctypes.CDLL(None, use_errno=True)
SYS_sched_setattr = 274                      # aarch64
FLAGS = 0x08 | 0x10 | 0x20                   # KEEP_POLICY | KEEP_PARAMS | UTIL_CLAMP_MIN


def attr(util_min):
    # struct sched_attr: size, policy, flags, nice, priority, runtime, deadline, period, util_min, util_max
    return ctypes.create_string_buffer(struct.pack("=IIQiIQQQII", 56, 0, FLAGS, 0, 0, 0, 0, 0, util_min, 1024), 56)


def children():
    """Map parent pid -> [child pids], from /proc/*/stat."""
    kids = {}
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            with open(f"/proc/{p}/stat") as f:
                ppid = int(f.read().rsplit(")", 1)[1].split()[1])
        except (OSError, IndexError, ValueError):
            continue
        kids.setdefault(ppid, []).append(int(p))
    return kids


def wineservers(prefix):
    found = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            with open(f"/proc/{p}/comm") as f:
                if f.read().strip() != "wineserver":
                    continue
            with open(f"/proc/{p}/environ", "rb") as f:
                env = f.read().split(b"\0")
        except OSError:
            continue
        if prefix and f"WINEPREFIX={prefix}".encode() in env:
            found.append(int(p))
    return found


def main():
    root, util = int(sys.argv[1]), int(sys.argv[2])
    log = sys.argv[3] if len(sys.argv) > 3 else None
    buf, done, failed = attr(max(0, min(1024, util))), set(), {}
    prefix = os.environ.get("WINEPREFIX", "")
    start = time.time()
    while os.path.exists(f"/proc/{root}"):
        kids, todo, procs = children(), [root], []
        while todo:
            p = todo.pop()
            procs.append(p)
            todo.extend(kids.get(p, []))
        procs.extend(wineservers(prefix))
        for p in procs:
            try:
                tids = os.listdir(f"/proc/{p}/task")
            except OSError:
                continue
            for t in tids:
                tid = int(t)
                if tid in done:
                    continue
                if libc.syscall(SYS_sched_setattr, tid, buf, 0) == 0:
                    done.add(tid)
                else:
                    e = os.strerror(ctypes.get_errno())
                    failed[e] = failed.get(e, 0) + 1
                    done.add(tid)          # do not retry a thread that refused
        time.sleep(2)
    # Threads and processes started by a boosted thread inherit its uclamp_min (the kernel copies
    # it on clone), so this counts only the threads set directly: ATRI's 51 threads were all
    # boosted while this said 6 (stage 3.5; Steam's reaper adopts the game's processes, so they are
    # no longer descendants of PID, but they inherited the boost from steam.exe).
    line = (f"boost: uclamp_min {util} set on {len(done) - sum(failed.values())} threads over "
            f"{int(time.time() - start)} s (threads they start inherit it)"
            + (f", failed: {failed}" if failed else "") + "\n")
    if log:
        with open(log, "a") as f:
            f.write(line)
    else:
        sys.stdout.write(line)


if __name__ == "__main__":
    main()
