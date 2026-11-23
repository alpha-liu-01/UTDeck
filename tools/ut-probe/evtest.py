#!/usr/bin/env python3
"""UTDeck stage 4.4: what the kernel itself reports for a controller, before SDL and Wine.

    evtest.py [/dev/input/eventN] [SECONDS]     (default: the first game controller, 30 s)

Prints the device's name and IDs, every axis with its range (min, max, fuzz, flat: the kernel's
calibration as the driver set it), the buttons it says it has, then every button press and release
and every large axis move, with the kernel's own names (BTN_SOUTH, ABS_HAT0X, ...). The evdev nodes
are readable for phablet (android_input), so this needs no rights the app does not have.
"""
import fcntl
import glob
import os
import select
import struct
import sys
import time

EV_KEY, EV_ABS = 0x01, 0x03
KEYS = {
    0x130: "BTN_SOUTH", 0x131: "BTN_EAST", 0x132: "BTN_C", 0x133: "BTN_NORTH", 0x134: "BTN_WEST",
    0x135: "BTN_Z", 0x136: "BTN_TL", 0x137: "BTN_TR", 0x138: "BTN_TL2", 0x139: "BTN_TR2",
    0x13a: "BTN_SELECT", 0x13b: "BTN_START", 0x13c: "BTN_MODE", 0x13d: "BTN_THUMBL",
    0x13e: "BTN_THUMBR", 0x220: "BTN_DPAD_UP", 0x221: "BTN_DPAD_DOWN", 0x222: "BTN_DPAD_LEFT",
    0x223: "BTN_DPAD_RIGHT", 0x2c0: "BTN_TRIGGER_HAPPY1", 0x2c1: "BTN_TRIGGER_HAPPY2",
    0x120: "BTN_TRIGGER", 0x121: "BTN_THUMB", 0x122: "BTN_THUMB2", 0x123: "BTN_TOP",
    0x124: "BTN_TOP2", 0x125: "BTN_PINKIE", 0x126: "BTN_BASE", 0x127: "BTN_BASE2",
    0x128: "BTN_BASE3", 0x129: "BTN_BASE4", 0x12a: "BTN_BASE5", 0x12b: "BTN_BASE6",
    0x12f: "BTN_DEAD", 0xa7: "KEY_RECORD", 0x9e: "KEY_BACK", 0xac: "KEY_HOMEPAGE",
}
ABS = {0x00: "ABS_X", 0x01: "ABS_Y", 0x02: "ABS_Z", 0x03: "ABS_RX", 0x04: "ABS_RY", 0x05: "ABS_RZ",
       0x06: "ABS_THROTTLE", 0x07: "ABS_RUDDER", 0x09: "ABS_GAS", 0x0a: "ABS_BRAKE",
       0x10: "ABS_HAT0X", 0x11: "ABS_HAT0Y"}


def ioc(direction, nr, size):
    return (direction << 30) | (size << 16) | (ord("E") << 8) | nr


def bits(fd, ev, count):
    buf = bytearray((count + 7) // 8)
    fcntl.ioctl(fd, ioc(2, 0x20 + ev, len(buf)), buf)
    return [i for i in range(count) if buf[i // 8] >> (i % 8) & 1]


def find_controller():
    for path in sorted(glob.glob("/dev/input/event*"), key=lambda p: int(p[16:])):
        try:
            fd = os.open(path, os.O_RDONLY)
        except OSError:
            continue
        try:
            if 0x130 in bits(fd, EV_KEY, 0x300) or 0x120 in bits(fd, EV_KEY, 0x300):
                return path
        finally:
            os.close(fd)
    return None


def main():
    path = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].startswith("/") else find_controller()
    seconds = float(sys.argv[-1]) if len(sys.argv) > 1 and not sys.argv[-1].startswith("/") else 30
    if not path:
        print("no game controller among /dev/input/event*")
        return
    fd = os.open(path, os.O_RDONLY)
    name = bytearray(256)
    fcntl.ioctl(fd, ioc(2, 0x06, len(name)), name)
    ident = bytearray(8)
    fcntl.ioctl(fd, ioc(2, 0x02, 8), ident)
    bus, vid, pid, ver = struct.unpack("4H", ident)
    label = bytes(name).split(b"\x00")[0].decode(errors="replace").strip()
    print(f"{path}: \"{label}\" bus {bus:#x} {vid:04x}:{pid:04x} version {ver:04x}")
    axes = bits(fd, EV_ABS, 0x40)
    rng = {}
    for a in axes:
        info = bytearray(24)
        fcntl.ioctl(fd, ioc(2, 0x40 + a, 24), info)
        value, lo, hi, fuzz, flat, res = struct.unpack("6i", info)
        rng[a] = (lo, hi)
        print(f"  axis {ABS.get(a, hex(a))}: now {value}, range {lo}..{hi}, fuzz {fuzz}, flat {flat}")
    keys = bits(fd, EV_KEY, 0x300)
    print("  buttons: " + " ".join(KEYS.get(k, hex(k)) for k in keys))
    print(f"reading for {seconds:g} s")
    last = {}
    end = time.time() + seconds
    while time.time() < end:
        if not select.select([fd], [], [], 0.2)[0]:
            continue
        data = os.read(fd, 24 * 64)
        for off in range(0, len(data) - 23, 24):
            _, _, typ, code, value = struct.unpack("qqHHi", data[off:off + 24])
            if typ == EV_KEY:
                print(f"{KEYS.get(code, hex(code))} {'down' if value else 'up'}", flush=True)
            elif typ == EV_ABS:
                lo, hi = rng.get(code, (-32768, 32767))
                step = max(1, (hi - lo) // 4)
                if code in (0x10, 0x11) or abs(value - last.get(code, value + step)) >= step:
                    print(f"{ABS.get(code, hex(code))} {value}", flush=True)
                    last[code] = value
    os.close(fd)


main()
