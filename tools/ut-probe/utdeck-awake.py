#!/usr/bin/env python3
"""UTDeck stage 4.7: keep the phone awake while a game is played or Steam downloads a game.

    utdeck-awake.py game PID [LOG]   the screen stays on while PID (game-run.sh) runs and the game's
                                     window is in front; another app in front, the user's own
                                     screen timeout applies again
    utdeck-awake.py downloads        the phone does not go to sleep while Steam downloads a game;
                                     the screen may go off. Ends when Steam ends.
    utdeck-awake.py hold             the screen stays on until the program that started it closes
                                     this helper's input (or ends): UTDeck's starting page, from the
                                     tap until the game runs (a game's start, Steam's included, can
                                     take minutes before game-run.sh's own "game" mode begins)

Ubuntu Touch turns the screen off after the user's timeout (System Settings, Battery) when nobody
touches it, and then puts the phone to sleep (suspend). A game played with a controller gets no
touch, so the screen went off in the middle of play; and a download stopped while the phone was
locked (the maintainer, stage 4.7).

Both ask the phone's power service, repowerd, on the system bus: keepDisplayOn
(com.canonical.Unity.Screen) keeps the screen on, requestSysState(name, 1) (com.lomiri.Repowerd)
keeps the system awake with the screen off. repowerd drops a request by itself when the program that
asked ends (checked on stone, 2026-11-23: listSysRequests is empty right after), so a helper that
crashes or is killed cannot leave the phone awake.

The game's window in front: Wine (patch 0015) writes its keyboard focus to UTDECK_FOCUS_FILE, "1" or
"0" (as utdeck-pause.py reads it); no file, or no value yet, counts as in front.

A download: a folder steamapps/downloading/<appid>, while Steam is working on it: its size changed in
the last STALL seconds, it appeared less than STALL seconds ago, or Steam wrote its content log
(logs/content_log.txt, a "Current download rate" line every minute while it downloads) in the last
STALL seconds. The folder's size alone is not enough: Barro 2020's 151 MB download (stage 4.7) left
the folder empty until its last second. A download Steam left there without going on does not keep
the phone awake for ever. Each change is logged to steam/awake.log (no account data: app ids only).
"""
import os
import sys
import time

STEAM = os.path.expanduser("~/.local/share/Steam")
STALL = 180
AWAKE_LOG = os.path.expanduser("~/utdeck-probe/steam/awake.log")


def log(path, text):
    if path:
        try:
            with open(path, "a") as f:
                f.write(f"keep-awake: {time.strftime('%H:%M:%S')} {text}\n")
        except OSError:
            pass


def note(text):
    """A line in awake.log; the file is kept small (the older half goes to awake.log.old)."""
    try:
        if os.path.getsize(AWAKE_LOG) > 65536:
            os.replace(AWAKE_LOG, AWAKE_LOG + ".old")
    except OSError:
        pass
    log(AWAKE_LOG, text)


def steam_logging():
    try:
        return time.time() - os.path.getmtime(os.path.join(STEAM, "logs/content_log.txt")) < STALL
    except OSError:
        return False


def running(pid):
    return os.path.exists(f"/proc/{pid}")


def in_front():
    path = os.environ.get("UTDECK_FOCUS_FILE", "")
    if not path:
        return True
    try:
        with open(path) as f:
            return f.read().strip() != "0"
    except OSError:
        return True


def steam_running():
    uid = os.getuid()
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            if os.stat(f"/proc/{p}").st_uid != uid:
                continue
            with open(f"/proc/{p}/comm") as f:
                if f.read().strip() == "steam":
                    return True
        except OSError:
            pass
    return False


def state_flags(appid):
    """The game's StateFlags from its .acf (4: fully installed, nothing running), or ""."""
    try:
        with open(os.path.join(STEAM, "steamapps", f"appmanifest_{appid}.acf"), encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.split('"')
                if len(parts) >= 4 and parts[1] == "StateFlags":
                    return parts[3]
    except OSError:
        pass
    return ""


def download_sizes():
    """{appid: bytes} of Steam's downloads in progress."""
    base = os.path.join(STEAM, "steamapps/downloading")
    sizes = {}
    try:
        names = os.listdir(base)
    except OSError:
        return sizes
    for name in names:
        if not name.isdigit() or state_flags(name) == "4":
            continue   # a folder Steam left behind (empty after a file check): the game is just installed
        total = 0
        for root, _dirs, files in os.walk(os.path.join(base, name)):
            for f in files:
                try:
                    total += os.lstat(os.path.join(root, f)).st_size
                except OSError:
                    pass
        sizes[name] = total
    return sizes


def game(pid, logfile):
    screen = dbus.SystemBus().get_object("com.canonical.Unity.Screen", "/com/canonical/Unity/Screen")
    cookie = None
    while running(pid):
        front = in_front()
        if front and cookie is None:
            cookie = screen.keepDisplayOn(dbus_interface="com.canonical.Unity.Screen")
            log(logfile, "the screen stays on while the game is in front")
        elif not front and cookie is not None:
            screen.removeDisplayOnRequest(cookie, dbus_interface="com.canonical.Unity.Screen")
            cookie = None
            log(logfile, "another app in front: the screen's own timeout applies")
        time.sleep(2)
    # Released when this program ends, too.


def hold():
    screen = dbus.SystemBus().get_object("com.canonical.Unity.Screen", "/com/canonical/Unity/Screen")
    screen.keepDisplayOn(dbus_interface="com.canonical.Unity.Screen")
    sys.stdin.read()   # returns when the app closes its end, or ends


def downloads():
    power = dbus.SystemBus().get_object("com.lomiri.Repowerd", "/com/lomiri/Repowerd")
    cookie = None
    seen = {}       # appid: (bytes, time the size last changed)
    gone = 0
    while True:
        now = time.monotonic()
        sizes = download_sizes()
        seen = {a: seen[a] if a in seen and seen[a][0] == b else (b, now) for a, b in sizes.items()}
        active = bool(seen) and (any(now - t < STALL for _b, t in seen.values()) or steam_logging())
        if active and cookie is None:
            cookie = power.requestSysState("utdeck-downloads", dbus.Int32(1), dbus_interface="com.lomiri.Repowerd")
            note(f"the phone stays awake: Steam downloads {', '.join(sorted(seen))}")
        elif not active and cookie is not None:
            power.clearSysState(cookie, dbus_interface="com.lomiri.Repowerd")
            cookie = None
            note("released: no download in progress" if not seen else
                 f"released: {', '.join(sorted(seen))} not going on for {STALL} s")
        # Steam gone for 30 s (stopped, or restarting for a game's mapping takes less): done.
        gone = 0 if steam_running() else gone + 1
        if gone >= 3:
            note("Steam ended")
            return
        time.sleep(10)


if __name__ == "__main__":
    try:
        import dbus
    except ImportError:
        sys.exit("utdeck-awake: python3-dbus is missing")
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "game" and len(sys.argv) > 2 and sys.argv[2].isdigit():
        game(int(sys.argv[2]), sys.argv[3] if len(sys.argv) > 3 else "")
    elif mode == "hold":
        hold()
    elif mode == "downloads":
        note("started")
        downloads()
    else:
        sys.exit("usage: utdeck-awake.py game PID [LOG] | downloads | hold")
