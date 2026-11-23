#!/usr/bin/env python3
"""UTDeck stage 4.4: a controller layout for SDL where its own does not fit the phone's kernel driver.

    padlayout.py            prints SDL_GAMECONTROLLERCONFIG lines (one per controller that needs one)
                            on stdout, and what it did on stderr

Run by game-run.sh before a game starts; Wine's winebus reads controllers through the phone's SDL 2
and takes the layout from this variable over SDL's own database.

Why: SDL's database knows a controller by its ID (bus, USB IDs, version, a checksum of the name), but
a layout there is written for one version of the kernel driver. stone's hid-nintendo (an early
version) reports the D-pad as four buttons and has a Capture button; SDL's layouts for the Switch Pro
Controller, both over Bluetooth and over USB, expect a D-pad hat and no Capture button, so in games
every button from Capture on was one off and the D-pad did nothing (the BETOP T6 in its Switch mode).

What: for each controller SDL sees, its layout is checked against what the kernel says the device
has. Only if SDL has no layout for a gamepad, or SDL's layout uses a D-pad hat the device does not
have, a layout is made from the kernel's own button names, by position, as on an Xbox pad: the bottom
face button is A, the right one B, the left one X, the top one Y. Controllers SDL already gets right
are left alone (the BETOP T6 in Xbox mode, the DualSense, a DualShock 4).
"""
import ctypes
import fcntl
import os
import sys

EV_KEY, EV_ABS = 0x01, 0x03
KEY_MAX, ABS_MAX = 0x2ff, 0x3f
ABS_HAT0X, ABS_HAT3Y = 0x10, 0x17
ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05
BTN_JOYSTICK = 0x120
BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_Z = 0x130, 0x131, 0x133, 0x134, 0x135
BTN_TL, BTN_TR, BTN_TL2, BTN_TR2 = 0x136, 0x137, 0x138, 0x139
BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR = 0x13a, 0x13b, 0x13c, 0x13d, 0x13e
BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT = 0x220, 0x221, 0x222, 0x223

# Kernel button -> SDL's name for it, by position.
BUTTONS = [(BTN_SOUTH, "a"), (BTN_EAST, "b"), (BTN_WEST, "x"), (BTN_NORTH, "y"),
           (BTN_TL, "leftshoulder"), (BTN_TR, "rightshoulder"), (BTN_SELECT, "back"),
           (BTN_START, "start"), (BTN_MODE, "guide"), (BTN_THUMBL, "leftstick"),
           (BTN_THUMBR, "rightstick"), (BTN_Z, "misc1"), (BTN_DPAD_UP, "dpup"),
           (BTN_DPAD_DOWN, "dpdown"), (BTN_DPAD_LEFT, "dpleft"), (BTN_DPAD_RIGHT, "dpright")]
AXES = [(ABS_X, "leftx"), (ABS_Y, "lefty"), (ABS_RX, "rightx"), (ABS_RY, "righty")]


def say(msg):
    print("padlayout: " + msg, file=sys.stderr)


def bits(fd, ev, count):
    buf = bytearray((count + 8) // 8)
    fcntl.ioctl(fd, (2 << 30) | (len(buf) << 16) | (ord("E") << 8) | (0x20 + ev), buf)
    return {i for i in range(count + 1) if buf[i // 8] >> (i % 8) & 1}


def make_layout(keys, axes):
    """A layout from the kernel's buttons and axes, numbered as SDL 2 numbers them on Linux: buttons
    from BTN_JOYSTICK up to KEY_MAX first, then those below; axes in code order without the hats."""
    order = [k for k in range(BTN_JOYSTICK, KEY_MAX + 1) if k in keys] + \
            [k for k in range(0, BTN_JOYSTICK) if k in keys]
    button = {k: i for i, k in enumerate(order)}
    plain = [a for a in range(ABS_MAX + 1) if a in axes and not ABS_HAT0X <= a <= ABS_HAT3Y]
    axis = {a: i for i, a in enumerate(plain)}
    parts = [f"{name}:b{button[k]}" for k, name in BUTTONS if k in button]
    parts += [f"{name}:a{axis[a]}" for a, name in AXES if a in axis]
    # Triggers: the analog axes if the device has both, else the buttons.
    if ABS_Z in axis and ABS_RZ in axis:
        parts += [f"lefttrigger:a{axis[ABS_Z]}", f"righttrigger:a{axis[ABS_RZ]}"]
    else:
        parts += [f"lefttrigger:b{button[k]}" for k in (BTN_TL2,) if k in button]
        parts += [f"righttrigger:b{button[k]}" for k in (BTN_TR2,) if k in button]
    if ABS_HAT0X in axes and not any(k in button for k in (BTN_DPAD_UP, BTN_DPAD_DOWN)):
        parts += ["dpup:h0.1", "dpright:h0.2", "dpdown:h0.4", "dpleft:h0.8"]
    return ",".join(parts) + ","


class GUID(ctypes.Structure):
    _fields_ = [("data", ctypes.c_uint8 * 16)]


def main():
    try:
        sdl = ctypes.CDLL("libSDL2-2.0.so.0")
    except OSError:
        say("no SDL 2 on this phone: nothing to check")
        return
    sdl.SDL_JoystickNameForIndex.restype = ctypes.c_char_p
    sdl.SDL_JoystickPathForIndex.restype = ctypes.c_char_p
    sdl.SDL_GameControllerMappingForDeviceIndex.restype = ctypes.c_void_p
    sdl.SDL_JoystickGetDeviceGUID.restype = GUID
    if sdl.SDL_Init(0x2000 | 0x200) != 0:
        say("SDL did not start")
        return
    for i in range(sdl.SDL_NumJoysticks()):
        name = (sdl.SDL_JoystickNameForIndex(i) or b"?").decode(errors="replace")
        path = (sdl.SDL_JoystickPathForIndex(i) or b"").decode()
        guid = bytes(sdl.SDL_JoystickGetDeviceGUID(i).data).hex()
        mapping = sdl.SDL_GameControllerMappingForDeviceIndex(i)
        sdl_layout = ctypes.string_at(mapping).decode() if mapping else ""
        if mapping:
            sdl.SDL_free(ctypes.c_void_p(mapping))
        if not path.startswith("/dev/input/event"):
            say(f"\"{name}\" ({guid}): read by SDL's own driver, SDL's layout is used")
            continue
        try:
            fd = os.open(path, os.O_RDONLY)
            keys, axes = bits(fd, EV_KEY, KEY_MAX), bits(fd, EV_ABS, ABS_MAX)
            os.close(fd)
        except OSError as e:
            say(f"\"{name}\": {path} not readable ({e.strerror})")
            continue
        if BTN_SOUTH not in keys:
            # Generic joystick buttons (the BETOP T6 in Xbox mode, hid-microsoft), a wheel: SDL's
            # database alone knows the layout.
            say(f"\"{name}\" ({guid}): no gamepad buttons from the kernel, SDL's layout is used")
            continue
        has_hat = ABS_HAT0X in axes
        if sdl_layout and (has_hat or ":h0." not in sdl_layout):
            say(f"\"{name}\" ({guid}): SDL's layout fits")
            continue
        reason = "SDL has no layout" if not sdl_layout else "SDL's layout expects a D-pad hat the device does not have"
        print(f"{guid},{name},{make_layout(keys, axes)}")
        say(f"\"{name}\" ({guid}): {reason}; a layout by position from the kernel's buttons is used")
    sdl.SDL_Quit()


main()
