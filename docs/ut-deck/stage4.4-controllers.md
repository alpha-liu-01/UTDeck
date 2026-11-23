# Stage 4.4: controllers, the plan

Written 2026-11-22, when 4.3 closed. **Not started**: it is tested on stone (its Bluetooth is the
more reliable), and starts when stone is at hand. Pulled forward from stage 5 by the maintainer
([stage4-roadmap.md](stage4-roadmap.md)): stage 4's exit needs a gamepad, and today only the BETOP
T6 over Bluetooth reaches games.

Goal: every controller the maintainer has reaches games, or there is a written reason why not; and
a page in UTDeck that shows what games see, live.

## Terms used here

- *evdev* = Linux's input devices, `/dev/input/event*`: what a kernel driver makes of a controller
  (buttons and axes with standard codes).
- *hidraw* = `/dev/hidraw*`: the controller's raw HID reports, without a kernel driver's
  interpretation. Programs with their own controller drivers (SDL's HIDAPI drivers, Wine's hidraw
  path) need it.
- *SDL* = the library that turns many controllers into one standard gamepad layout (an Xbox-like
  one), with a database of known controllers.
- *`winebus`* = Wine's driver that brings Linux controllers into Windows as HID devices, through
  three paths: SDL, evdev ("input"), and hidraw.
- *XInput* = Windows' Xbox controller interface (four slots, Xbox layout); most games use it.
  *DirectInput* = the older interface for any controller. Some games also read PlayStation pads
  directly (their own HID code, for their button icons, touchpad, light bar).
- *`xpad`* = the Linux driver for wired Xbox-style pads (`045e:028e` and many clones).
- *`uaccess`* = a udev tag that gives the person at the seat access to a device.

## What exists

- **The BETOP T6 over Bluetooth works** (stage 3.1, M9; Ys IX and Celceta): in Xbox mode it reports
  `045e:02e0`, binds the kernel's `hid-microsoft`, and reaches Wine through `winebus`'s SDL path as
  an Xbox controller: XInput slot 0, the winmm joystick, rumble.
- **stone's kernel** (stone-ut, Linux 5.4) has the drivers: `xpad`, `hid-playstation`, `hid-sony`,
  `hid-nintendo`, `hid-steam`, `hid-microsoft`, `uhid`, `hidraw` (the Dolphin port's stage 0, from
  `/proc/config.gz`). `/dev/input/event*` is `root:android_input 0660` and phablet is in
  `android_input`. The permissions of `/dev/hidraw*` are **not known yet** (no pad was connected
  when stage 0 looked).
- **The DualShock 4 clone over USB** (`054c:05c4`) is rejected by the kernel: `hid-sony` asks it for
  its MAC address (feature report `0x81`), the clone does not answer, and the driver gives up; no
  evdev and no hidraw node is left (the Dolphin port's stage 1). Over Bluetooth: not tried.
- **Steam saw a DualShock** on the Nothing Phone (its notice "Controller using Steam Input": "Your
  DualShock controller isn't supported by this game", stage 4.3), so the Steam client's own SDL found
  one there.
- **Test tools**: `tools/ut-probe/gptest.sh` with `tools/ut-probe/win/gptest.c`: lists the Linux input
  devices and hidraw nodes, then reads XInput and the winmm joystick inside Wine, with `winebus`'s
  messages logged. The Dolphin port's Controllers page (`dolphin-ut`, stage 3.5) for the look.
- **Not available**: `/dev/uinput` (stage 0), so no virtual controllers, and so no Steam Input (the
  real controller is passed to the game, 3.2b).

## A first lead, from Wine's source

`winebus` takes a DualShock 4 or DualSense **only through hidraw**: for those IDs (Sony, `054c`) it
prefers hidraw and drops the same controller arriving through SDL or evdev
(`dlls/winebus.sys/main.c`, `is_hidraw_enabled` and the "ignoring non-hidraw device" path). So if
phablet cannot open `/dev/hidraw*`, a PlayStation pad disappears from both paths, and no game sees
it. Two ways out, to be tried in this order:

1. **The registry, in user space**: `winebus` reads per-device options from
   `HKLM\System\CurrentControlSet\Services\WineBus\Devices\<VID>[/<PID>]`, value `Hidraw`. With
   `Hidraw` = 0 for `054c`, the pad goes through SDL instead and arrives as an Xbox controller
   (XInput): it then works in nearly every game, without PlayStation button icons, touchpad or light
   bar. This fits UTDeck's prefix setup (`steam-prefix.reg`, `prefix-dlls.sh`).
2. **hidraw access**, for the full PlayStation pad (games that support it natively show its icons
   and use its touchpad). A udev rule on the phone would be a system change: only with the
   maintainer's OK, and only if 1 is not enough.

This is a lead, not a finding: the first step checks it on stone.

## Controllers

| Controller | Expected path | Test |
| --- | --- | --- |
| BETOP T6, Bluetooth | works today: `hid-microsoft`, SDL, XInput | the reference: checked again, unchanged |
| BETOP T6, wired | its Xbox (X-input) mode should be `045e:028e` and bind `xpad` | stone over USB OTG |
| Xbox controller | Bluetooth: `hid-microsoft` or `hid-generic` and SDL's mapping; wired: `xpad` | stone (the model to be named by the maintainer) |
| DualShock 4 clone | USB: rejected by `hid-sony` (see above); Bluetooth: unknown | both, then the kernel patch question (below) |
| DualSense | `hid-playstation` makes evdev and hidraw nodes; Wine wants hidraw (the lead) | stone, Bluetooth and USB |
| Switch Pro | `hid-nintendo`, then SDL; the same `winebus` paths as above | **not testable** (no controller): built for, marked untested |

## Steps

1. **Observe, controller by controller, on stone.** For each one, follow it layer by layer and write
   down where it gets lost:
   - the kernel: which driver bound it, with which IDs, any errors (`dmesg`);
   - the device nodes: which `event*` and `hidraw*` nodes, their owner and mode, whether phablet can
     open them;
   - Wine: which `winebus` path took or dropped it (`WINEDEBUG=+hid` shows "ignoring ... device");
   - Windows: XInput slot, DirectInput device, the button and axis layout (`gptest.exe`);
   - one game: does it move, are the buttons right, does rumble work.
2. **Fix where it is lost**, user space first: `winebus` registry options in UTDeck's prefix setup,
   SDL hints and mappings in the runtime's environment, `gptest.sh`'s checks made permanent in
   `utdeck-setup.sh check`. A system change (a udev rule) or a kernel change (the DualShock clone's
   `0x81`, a patch in stone-ut's kernel) only with the maintainer's OK, each written up first with
   what it changes.
3. **The probe**: `gptest.exe` grown into a small Windows program that writes, many times a second,
   what Windows offers games: the XInput slots with buttons, sticks, triggers; the DirectInput
   devices; the PlayStation pads seen directly; and that plays a short rumble on request.
4. **The Controllers page** (Settings, Controllers), native Lomiri look after the Dolphin port's
   page: each connected controller with its name, how games see it (Xbox, PlayStation, generic),
   its buttons and sticks moving live, a rumble test, and, for one that is not seen, the reason in
   words (for example "the kernel did not accept this controller"). It shows Wine's view, from the
   probe, not Linux's: a pad can look fine to Linux while games do not see it, which is where the
   PlayStation pads fail today.
5. **One game per controller**, from the app.

## Also here, if it fits

- **K3: a controller connected while a game runs** is not seen by the game (stage 3S, M13; and
  once Ys: Memories of Celceta drew its world black after that). `winebus` follows udev for new
  devices; why that does not reach the game is part of step 1. If the fix is outside `winebus`, it
  stays with stage 5.

## Not in 4.4

- Touch controls, the virtual controller (no `uinput`), the on-screen keyboard: stage 5.
- Remapping and per-game controller profiles: games map their own controls; a remapping layer would
  need the virtual controller (stage 5) or per-game settings (4.6).
- The Nothing Phone (1): not for controllers (its Bluetooth is not reliable with the BETOP T6).

## Exit

On stone: the BETOP T6 (Bluetooth and wired), an Xbox controller, the DualShock 4 clone and the
DualSense each work in a game started from UTDeck, or have the reason they do not written down (with
the fix proposed, if it needs the maintainer's OK); the Switch Pro path ready and marked untested;
the Controllers page shows each working controller live, with rumble.

## Open questions for the maintainer

- Which Xbox controller it is (Xbox One, One S or Series over Bluetooth, or a wired 360-style pad).
- Whether the BETOP T6 has a wired X-input mode (and how it is switched).
