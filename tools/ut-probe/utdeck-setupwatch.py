#!/usr/bin/env python3
"""UTDeck stage 4.3: end Visual C++ installers that finished but never exit, during a Steam install script.

    utdeck-setupwatch.py PID [LOG]

Until PID (game-run.sh's Wine process for the install script) ends, every 5 s: for each process of
this prefix (WINEPREFIX of this process, read from each process's environment) running Microsoft's
Visual C++ redistributable (VC_redist.x64.exe, vcredist_x86.exe, ...), find that installer's own log
(the WiX "burn" engine's dd_vcredist_<arch>_<time>.log in the prefix's Temp folder). When the log
says it finished ("i007: Exit code: ...", its last line) more than 20 s ago and the process used no
CPU over the last 10 s, the process is ended (SIGTERM, then SIGKILL): the install script then goes on.

Why: under Wine on FEX such an installer finishes its work, logs its exit code, and then stays asleep
for good (one thread in a futex, no CPU): seen with Aperture Desk Job's uninstaller (stage 3.9) and
Ys IX's x64 installer on a brand-new prefix (stage 4.3, the Nothing Phone (1): 8 minutes until ended by
hand). The install script waits on it, and a game's first launch with it.

Each action is logged to LOG with the time. Only processes of this prefix are looked at.
"""
import glob
import os
import re
import signal
import sys
import time

PID = int(sys.argv[1])
LOG = sys.argv[2] if len(sys.argv) > 2 else None
PREFIX = os.environ.get("WINEPREFIX", "")
INSTALLER = re.compile(r"vc_?redist[^\\/]*\.exe", re.I)
QUIET_S = 20     # the log has said "finished" for this long
IDLE_S = 10      # and the process used no CPU for this long


def log(msg):
    line = f"setupwatch: {time.strftime('%H:%M:%S')} {msg}"
    if LOG:
        with open(LOG, "a") as f:
            f.write(line + "\n")
    else:
        print(line, flush=True)


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def cpu_ticks(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            fields = f.read().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])   # utime + stime
    except (OSError, IndexError, ValueError):
        return None


def installers():
    """(pid, cmdline) of this prefix's Visual C++ installers."""
    found = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open(f"/proc/{d}/cmdline", "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode(errors="replace")
            if not INSTALLER.search(cmd):
                continue
            with open(f"/proc/{d}/environ", "rb") as f:
                env = f.read().split(b"\0")
        except OSError:
            continue
        if f"WINEPREFIX={PREFIX}".encode() in env:
            found.append((int(d), cmd))
    return found


def finished_log(cmd):
    """The installer's newest main log, if its last line says it finished: (path, age in s)."""
    arch = "amd64" if re.search(r"x64|amd64", cmd, re.I) else "x86"
    logs = [p for p in glob.glob(os.path.join(PREFIX, "drive_c/users/*/AppData/Local/Temp",
                                              f"dd_vcredist_{arch}_*.log"))
            if not re.search(r"_\d{3}_", os.path.basename(p))]   # not the per-package logs
    if not logs:
        return None
    newest = max(logs, key=os.path.getmtime)
    try:
        with open(newest, "rb") as f:
            f.seek(max(0, os.path.getsize(newest) - 4096))
            tail = f.read().decode("utf-8", errors="replace")
    except OSError:
        return None
    lines = [l for l in tail.splitlines() if l.strip()]
    if lines and "i007: Exit code:" in lines[-1]:
        return newest, time.time() - os.path.getmtime(newest)
    return None


def main():
    if not PREFIX:
        return
    seen_idle = {}   # pid -> (cpu ticks, since)
    while alive(PID):
        time.sleep(5)
        for pid, cmd in installers():
            done = finished_log(cmd)
            ticks = cpu_ticks(pid)
            if not done or ticks is None or done[1] < QUIET_S:
                seen_idle.pop(pid, None)
                continue
            first = seen_idle.get(pid)
            if not first or first[0] != ticks:
                seen_idle[pid] = (ticks, time.time())
                continue
            if time.time() - first[1] < IDLE_S:
                continue
            name = INSTALLER.search(cmd).group(0)
            log(f"{name} (pid {pid}) logged its exit {int(done[1])} s ago ({os.path.basename(done[0])}) "
                f"and is idle: ending it")
            try:
                os.kill(pid, signal.SIGTERM)
                for _ in range(10):
                    time.sleep(0.5)
                    if not alive(pid):
                        break
                else:
                    os.kill(pid, signal.SIGKILL)
                    log(f"{name} (pid {pid}) did not end on SIGTERM: SIGKILL")
            except OSError:
                pass
            seen_idle.pop(pid, None)


if __name__ == "__main__":
    main()
