#!/usr/bin/env python3
"""UTDeck stage 3.6: pause a game while the phone is locked or another app is in front, as Lomiri
pauses its own apps.

    utdeck-pause.py PID [LOG]

Until PID ends, once a second: when Lomiri's lock screen is up (com.lomiri.LomiriGreeter.IsActive)
or the screen is off, every process of this prefix (WINEPREFIX) that runs a Windows program gets
SIGSTOP, and SIGCONT when the phone is unlocked again. Lomiri does this for its own apps, but a game
started by hidden Steam is not one of them, so it ran on at full speed behind the lock screen
(stage 3.6: Ys IX at 3 cores and the whole GPU, Celceta at 30 fps, music playing). Freezing was
checked by hand on Ys IX: nothing is used while frozen, PulseAudio pauses the sound stream by
itself, and picture, sound and controller come back.

Another app in front: Wine (patch 0015) writes the window's keyboard focus to UTDECK_FOCUS_FILE.
When it says "0" for a second, the game is frozen and its sound streams muted; a frozen game cannot
read Lomiri's "back in front", so it is woken for PEEK seconds every PERIOD seconds to read it, and
woken and unmuted once the file says "1". Lomiri cannot be asked instead: its isPidFocused only
knows its own apps, and it does not tie Wine's window to the app that started it.

Left running: hidden Steam (its DRM needs it awake), Wine's server and Wine's own services (other
processes need them; they idle). Each change is logged to LOG with the time and the processes.
If this helper ends while the game is frozen, it wakes it first.
"""
import os
import signal
import subprocess
import sys
import time

GREETER = ["gdbus", "call", "--session", "--timeout", "2", "--dest", "com.lomiri.LomiriGreeter",
           "--object-path", "/com/lomiri/LomiriGreeter", "--method",
           "org.freedesktop.DBus.Properties.Get", "com.lomiri.LomiriGreeter", "IsActive"]
SERVICES = ("services.exe", "winedevice.exe", "plugplay.exe", "svchost.exe", "explorer.exe",
            "rpcss.exe", "conhost.exe", "tabtip.exe", "steam.exe")


def backlight():
    base = "/sys/class/backlight"
    for d in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        try:
            with open(f"{base}/{d}/brightness") as f:
                return int(f.read())
        except (OSError, ValueError):
            pass
    return -1


def locked():
    """True when locked or the screen is off; None when Lomiri does not answer (no change then)."""
    if backlight() == 0:
        return True
    try:
        out = subprocess.run(GREETER, capture_output=True, text=True, timeout=3).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    if "true" in out:
        return True
    if "false" in out:
        return False
    return None


def game_processes(prefix):
    """This prefix's processes that run a Windows program, Wine's services left out."""
    found = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            with open(f"/proc/{p}/cmdline", "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode(errors="replace")
            with open(f"/proc/{p}/environ", "rb") as f:
                env = f.read().split(b"\0")
        except OSError:
            continue
        # The program is everything up to the first ".exe": game folders have spaces ("Ys IX
        # Monstrum Nox"), so the first word of the command line is not it.
        i = cmd.lower().find(".exe")
        if i < 0:
            continue
        exe = cmd[:i + 4].replace("\\", "/").rsplit("/", 1)[-1].lower()
        if exe in SERVICES or cmd.startswith("/"):   # a Linux program naming a .exe (a shell, Steam's wrapper)
            continue
        if f"WINEPREFIX={prefix}".encode() in env:
            found.append(int(p))
    return found


PEEK = 0.3      # seconds a game frozen for "not in front" is woken to read Lomiri's events
PERIOD = 1.5    # seconds between two such peeks (the longest delay on return)


def in_front(path):
    """Wine's patch 0015 writes 1 or 0 to UTDECK_FOCUS_FILE; no file (an older Wine, or no focus
    event yet) counts as in front, so nothing is paused for it."""
    if not path:
        return True
    try:
        with open(path) as f:
            return f.read(1) != "0"
    except OSError:
        return True


def set_mute(ids, mute, streams=False):
    """Mute (or unmute) PulseAudio streams: of the processes ids, or the stream ids themselves.
    Returns the stream ids changed."""
    changed = []
    if streams:
        targets = list(ids)
    else:
        targets, cur, pids = [], None, {str(p) for p in ids if p}
        try:
            out = subprocess.run(["pactl", "list", "sink-inputs"], capture_output=True, text=True, timeout=3).stdout
        except (OSError, subprocess.TimeoutExpired):
            return []
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("Sink Input #"):
                cur = line.split("#", 1)[1]
            elif line.startswith("application.process.id") and cur and line.split('"')[1] in pids:
                targets.append(cur)
    for t in targets:
        try:
            if subprocess.run(["pactl", "set-sink-input-mute", t, "1" if mute else "0"],
                              capture_output=True, timeout=3).returncode == 0:
                changed.append(t)
        except (OSError, subprocess.TimeoutExpired):
            pass
    return changed


def send(pids, sig):
    for p in pids:
        if p is None:
            continue
        try:
            os.kill(p, sig)
        except OSError:
            pass


def main():
    root = int(sys.argv[1])
    log = open(sys.argv[2], "a") if len(sys.argv) > 2 else sys.stdout
    prefix = os.environ.get("WINEPREFIX", "")
    # Lomiri is asked over the session bus; Steam's environment normally has it, but not always.
    os.environ.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    os.environ.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path=/run/user/{os.getuid()}/bus")
    focus_file = os.environ.get("UTDECK_FOCUS_FILE", "")
    frozen, why, muted = [], None, []

    def say(text):
        log.write(f"pause: {time.strftime('%T')} {text}\n")
        log.flush()

    def freeze(reason):
        nonlocal frozen, why, muted
        frozen, why = game_processes(prefix), reason
        send(frozen, signal.SIGSTOP)
        if reason == "not in front":       # peeks would play bits of sound: mute meanwhile
            muted = set_mute(frozen, True)
        say(f"{reason}, frozen {frozen}" + (f", muted {len(muted)} stream(s)" if muted else ""))
        if not frozen:                      # nothing found: say so once, not every second
            frozen = [None]

    def thaw(reason):
        nonlocal frozen, why, muted
        send(frozen, signal.SIGCONT)
        if muted:
            set_mute(muted, False, streams=True)
        say(f"{reason}, woken {frozen}")
        frozen, why, muted = [], None, []

    def wake(*_):
        if frozen:
            thaw("helper ending")
        sys.exit(0)

    signal.signal(signal.SIGTERM, wake)
    away_since = None
    while os.path.exists(f"/proc/{root}"):
        state = locked()
        front = in_front(focus_file)
        if state is True:
            if not frozen:
                freeze("locked")
            elif why == "not in front":
                why = "locked"              # stays frozen; no more peeks while locked
        elif state is False:
            if why == "locked":
                thaw("unlocked")            # then the focus decides on the next round
            elif not front:
                away_since = away_since or time.time()
                if not frozen and time.time() - away_since >= 1:
                    freeze("not in front")
                elif why == "not in front":
                    # A frozen game cannot read Lomiri's "back in front": wake it for a moment so
                    # that it can (Wine writes the focus file), then look again.
                    send(frozen, signal.SIGCONT)
                    time.sleep(PEEK)
                    if in_front(focus_file):
                        thaw("back in front")
                        away_since = None
                        continue
                    send(frozen, signal.SIGSTOP)
            else:
                away_since = None
                if why == "not in front":
                    thaw("back in front")
        time.sleep(PERIOD if why == "not in front" else 1)
    wake()


if __name__ == "__main__":
    main()
