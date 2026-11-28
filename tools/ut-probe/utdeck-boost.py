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
Some kernels refuse clamp changes without CAP_SYS_NICE (Android's 6.1 kernels, on the OnePlus Ace 5:
EPERM on every thread). Threads refused that way are handed, once per pass, to the phone's
io.github.alphaliu01.utdeck.Boost1 service when it has one (giuliac-ut: a root service that sets
uclamp_min on threads of the caller's own user, and nothing else); without it they stay counted as
failures, as before.
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


BOOST_SERVICE = "io.github.alphaliu01.utdeck.Boost1"
BOOST_PATH = "/io/github/alphaliu01/utdeck/Boost1"


class Service:
    """The phone's Boost1 service, if it has one (looked up once, on the first refused thread)."""

    def __init__(self):
        self.proxy, self.looked = None, False

    def clamp(self, util, tids):
        """(done, refused) through the service, or None without one."""
        if not self.looked:
            self.looked = True
            try:
                import dbus
                bus = dbus.SystemBus()
                if bus.name_has_owner(BOOST_SERVICE) or BOOST_SERVICE in bus.list_activatable_names():
                    self.proxy = dbus.Interface(bus.get_object(BOOST_SERVICE, BOOST_PATH), BOOST_SERVICE)
            except Exception:
                self.proxy = None
        if self.proxy is None:
            return None
        try:
            done, refused = self.proxy.ClampThreads(util, tids, signature="uau")
            return int(done), int(refused)
        except Exception:
            return None


def main():
    root, util = int(sys.argv[1]), int(sys.argv[2])
    log = sys.argv[3] if len(sys.argv) > 3 else None
    util = max(0, min(1024, util))
    buf, done, failed = attr(util), set(), {}
    service, by_service = Service(), 0
    prefix = os.environ.get("WINEPREFIX", "")
    start = time.time()
    while os.path.exists(f"/proc/{root}"):
        refused = []
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
                    err = ctypes.get_errno()
                    if err == 1:           # EPERM: the kernel wants CAP_SYS_NICE; the service may help
                        refused.append(tid)
                    else:
                        e = os.strerror(err)
                        failed[e] = failed.get(e, 0) + 1
                    done.add(tid)          # do not retry a thread that refused
        if refused:
            r = service.clamp(util, refused)
            if r is None:
                e = os.strerror(1)
                failed[e] = failed.get(e, 0) + len(refused)
            else:
                by_service += r[0]
                if r[1]:
                    e = "refused by the boost service"
                    failed[e] = failed.get(e, 0) + r[1]
        time.sleep(2)
    # Threads and processes started by a boosted thread inherit its uclamp_min (the kernel copies
    # it on clone), so this counts only the threads set directly: ATRI's 51 threads were all
    # boosted while this said 6 (stage 3.5; Steam's reaper adopts the game's processes, so they are
    # no longer descendants of PID, but they inherited the boost from steam.exe).
    line = (f"boost: uclamp_min {util} set on {len(done) - sum(failed.values())} threads over "
            f"{int(time.time() - start)} s (threads they start inherit it)"
            + (f", {by_service} of them through {BOOST_SERVICE}" if by_service else "")
            + (f", failed: {failed}" if failed else "") + "\n")
    if log:
        with open(log, "a") as f:
            f.write(line)
    else:
        sys.stdout.write(line)


if __name__ == "__main__":
    main()
