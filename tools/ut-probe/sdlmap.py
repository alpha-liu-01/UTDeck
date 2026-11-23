#!/usr/bin/env python3
"""UTDeck stage 4.4: the controllers SDL sees and the layout it gives each one.

    sdlmap.py            every joystick: name, SDL's GUID, whether SDL calls it a game controller,
                         and its layout (the mapping string)
    sdlmap.py --guid     only the GUIDs, one per line

Uses the phone's own SDL 2 (libSDL2-2.0.so.0), the one Wine's winebus loads, so what it prints is
what Wine gets; SDL_GAMECONTROLLERCONFIG in the environment is applied as it would be for Wine. A GUID
is SDL's ID for a controller: the bus (03 USB, 05 Bluetooth), a checksum of the name, the USB IDs and
a version, so the same pad on another bus or kernel driver has another GUID (and may get another
layout from SDL's database).
"""
import ctypes
import sys


class GUID(ctypes.Structure):
    _fields_ = [("data", ctypes.c_uint8 * 16)]


sdl = ctypes.CDLL("libSDL2-2.0.so.0")
sdl.SDL_JoystickNameForIndex.restype = ctypes.c_char_p
sdl.SDL_GameControllerMappingForDeviceIndex.restype = ctypes.c_void_p
sdl.SDL_JoystickGetDeviceGUID.restype = GUID
sdl.SDL_Init(0x2000 | 0x200)   # SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK
guids_only = "--guid" in sys.argv
if not guids_only:
    version = (ctypes.c_uint8 * 3)()
    sdl.SDL_GetVersion(version)
    print("SDL %d.%d.%d, %d joystick(s)" % (version[0], version[1], version[2], sdl.SDL_NumJoysticks()))
for i in range(sdl.SDL_NumJoysticks()):
    guid = bytes(sdl.SDL_JoystickGetDeviceGUID(i).data).hex()
    if guids_only:
        print(guid)
        continue
    name = sdl.SDL_JoystickNameForIndex(i).decode(errors="replace")
    print(f"{i}: \"{name}\" GUID {guid}, game controller: {'yes' if sdl.SDL_IsGameController(i) else 'no'}")
    mapping = sdl.SDL_GameControllerMappingForDeviceIndex(i)
    print("  layout: " + (ctypes.string_at(mapping).decode() if mapping else "none"))
    if mapping:
        sdl.SDL_free(ctypes.c_void_p(mapping))
sdl.SDL_Quit()
