# Stage 4.4: controllers, the plan

Written 2026-11-22, when 4.3 closed; started and done the same day (see "Done" below). Tested on
stone (its Bluetooth is the more reliable). Pulled forward from stage 5 by the maintainer
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
| BETOP T6, Switch mode | presents itself as a Switch Pro controller (`057e:2009` expected), with gyro (not in its Xbox mode): `hid-nintendo`, then SDL | stone |
| DualShock 4 clone | USB: rejected by `hid-sony` (see above); Bluetooth: unknown | both, then the kernel patch question (below) |
| DualSense | `hid-playstation` makes evdev and hidraw nodes; Wine wants hidraw (the lead) | stone, Bluetooth and USB |
| Switch Pro (genuine) | as the T6's Switch mode | no genuine one: the T6's Switch mode stands in for it |

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

## Step 1: groundwork on stone, without a controller (2026-11-22)

- **UTDeck 0.3.15** installed on stone.
- **The kernel** (`5.4.283~positron`, `/proc/config.gz`): `xpad`, `hid-playstation` (with its force
  feedback), `hid-sony`, `hid-nintendo`, `hid-microsoft`, `hid-steam`, `hid-generic`, `hidraw`,
  `uhid`, `hiddev`, all built in. `joydev` (`/dev/input/js*`) is off, which nothing here needs.
- **Who may open a controller's nodes.** phablet is in `android_input` (the evdev nodes,
  `/dev/input/event*`, `root:android_input 0660`), but in neither group the raw nodes get:
  - stone's own udev rule, `/etc/udev/rules.d/70-sunstone.rules:34`:
    `/dev/hidraw*` `OWNER="0", GROUP="1018", MODE="0660"` (1018 is Android's `usb` group);
  - Android's `ueventd.rc:74` (its `ueventd` runs next to `systemd-udevd`): `/dev/hidraw*`
    `0660 system system`.
  Both keep phablet out. systemd's `70-uaccess.rules` gives the seat's user (`logind`: one session,
  phablet on `seat0`) only some hidraw devices (production controllers for audio work), not game
  controllers. So the lead holds so far: Wine's only path for PlayStation pads is closed. To be
  confirmed with a pad connected (which rule wins, the node's real owner and mode).
- **The SDL Wine uses** is the system's, Ubuntu's `libSDL2-2.0.so.0.3000.0` (SDL 2.30) in
  `/usr/lib/aarch64-linux-gnu`: the runtime carries none. SDL 2.30 has its own drivers for
  PlayStation and Switch pads (with gyro), but they too need the raw nodes; without them it falls back
  to evdev. (Whether the Nothing Phone has the same library: to check there; the runtime may need to
  carry SDL for phones that lack it.)
- **Wine's `winebus`** starts its SDL and udev paths (`trace:hid:bus_main_thread L"SDL"` and
  `L"UDEV"` "main loop started"; the macOS path, IOHID, fails as expected).
- **New tools**:
  - `tools/ut-probe/padcheck.sh` (on the phone, `~/utdeck-probe/padcheck.sh`): every HID device with
    its IDs, the kernel driver that took it, its evdev and hidraw nodes with owner, group, mode and
    whether phablet can open them; the kernel's HID messages.
  - `gptest.exe` now also lists DirectInput 8's game controllers and the HID game controllers Windows
    offers (USB IDs, usage, report size: where a game with its own PlayStation or Switch support finds
    its pad); `gptest.exe 0` lists only.
  - `gptest.sh` runs `padcheck.sh`, then `gptest.exe`, then sums up `winebus`'s decisions from its log
    (which path added each controller, which it ignored).
  - Checked with no controller: only Wine's own virtual mouse and keyboard (`845e:0001`, `0002`).

Order for the controllers: the T6 in Xbox mode over Bluetooth first (the reference, which works),
then the T6's Switch mode, the DualSense, the DualShock 4 clone, and the T6 wired.

## Step 1: the BETOP T6, Xbox mode, Bluetooth (2026-11-22): the reference

- **Linux** (`padcheck.sh`): `BETOP T6 [045e:02e0, Bluetooth]`, kernel driver `microsoft`;
  `/dev/input/event7` (`root:android_input 660`, readable); `/dev/hidraw0` **`root:1018 660`, not
  readable**. stone's udev rule wins over Android's `ueventd` (the node's group is 1018, not
  `system`). The kernel: `unknown main item tag 0x0` (harmless), then
  `input,hidraw0: BLUETOOTH HID v9.03 Gamepad [BETOP T6]`.
- **Wine** (`gptest.sh 40`): taken by the SDL path (`sdl_add_device controller id 0`); the udev path
  tried the raw node and got `udev_add_device Unable to open udev device "/dev/hidraw0": Permission
  denied`: the first direct sight of the closed raw nodes.
- **Windows**: XInput controller 0 (type 1, subtype 1: a gamepad); winmm "Wine joystick driver" (10
  buttons, 5 axes); DirectInput 8 "Xbox One Wireless Controller" (`045e:02e0`); HID `045e:02e0`
  (usage 1:5, a gamepad, 15-byte reports). Every button (A B X Y, LB RB, Back Start, both stick
  presses), the D-pad with its diagonals, both sticks over their full range (-32768 to 32767), both
  triggers (the right one to 255; the left one's top was 241 in this run). 176 events in 40 s.
  Rumble: sent (1 s on controller 0).

## Step 1: the BETOP T6, Switch mode, Bluetooth (2026-11-22)

- **Linux**: `Pro Controller [057e:2009, Bluetooth]`, kernel driver `nintendo`; one input device,
  "Nintendo Switch Pro Controller" (`event7`, readable); `/dev/hidraw0` `root:1018`, not readable.
  stone's `hid-nintendo` is an older backport: **no motion device** (newer kernels add a second input
  device, "... (IMU)", for the gyro and accelerometer). So the T6's gyro can only come through the raw
  node, which phablet cannot open.
- **The kernel's own events** (`tools/ut-probe/evtest.py`, a new reader for one evdev node: name, IDs,
  axis ranges, buttons, then every press with the kernel's names): all 18 buttons, in the order
  pressed: A `BTN_EAST`, B `BTN_SOUTH`, X `BTN_NORTH`, Y `BTN_WEST` (by position, as on a Switch
  pad), L `BTN_TL`, R `BTN_TR`, ZL `BTN_TL2`, ZR `BTN_TR2` (digital), minus `BTN_SELECT`, plus
  `BTN_START`, Capture `BTN_Z`, Home `BTN_MODE`, both stick presses, the D-pad as four buttons
  (`BTN_DPAD_*`, no hat axes). Sticks -32767..32767 (fuzz 250, flat 500). (The T6 has no Home and
  Capture buttons of its own: Fn+Back and Fn+Start, per the maintainer.)
- **Wine, as it is: nothing.** SDL found the pad, but `winebus` dropped it:
  `ignoring non-hidraw device 057e:2009 with usages 0001:0005`, and its raw node gave `Permission
  denied`. Wine treats Nintendo's Pro Controller and Joy-Cons as it treats PlayStation pads: raw node
  only (`dlls/winebus.sys/main.c`, `case 0x057e`; the runtime's source, commit `3ea3a14`). No XInput,
  no DirectInput, no HID device.
- **Fix 1, the raw-node preference**: the registry value `Hidraw` = 0 (DWORD) under
  `HKLM\System\CurrentControlSet\Services\WineBus\Devices\057e` (all of Nintendo's pads), set in
  the test prefix only. Wine then takes the pad through SDL: XInput controller 0, DirectInput
  "Nintendo Switch Pro Controller" (`057e:2009`), a HID game controller.
- **Fix 2, SDL's layout.** Several buttons were wrong or missing (L came as RB, ZR as Back, no D-pad,
  no stick presses). SDL 2.30's built-in layout for this ID (GUID
  `050056fb7e0500000920000001000000`, version `0001`) was written for another version of the kernel
  driver: it expects the D-pad as a hat (`h0.1` ...) and no Capture button, so from Capture on every
  button is one off. A layout for stone's driver, given to SDL in `SDL_GAMECONTROLLERCONFIG`
  (SDL takes it over its own, checked with a ctypes script):

  ```
  050056fb7e0500000920000001000000,Nintendo Switch Pro Controller,a:b0,b:b1,x:b3,y:b2,back:b9,start:b10,guide:b11,misc1:b4,leftshoulder:b5,rightshoulder:b6,lefttrigger:b7,righttrigger:b8,leftstick:b12,rightstick:b13,dpup:b14,dpdown:b15,dpleft:b16,dpright:b17,leftx:a0,lefty:a1,rightx:a2,righty:a3,
  ```

  SDL numbers this driver's buttons in code order: `BTN_SOUTH` b0, `EAST` b1, `NORTH` b2, `WEST` b3,
  `Z` b4, `TL` b5, `TR` b6, `TL2` b7, `TR2` b8, `SELECT` b9, `START` b10, `MODE` b11, `THUMBL` b12,
  `THUMBR` b13, `DPAD_UP..RIGHT` b14-b17. The layout is **by position**: the bottom face button (B on
  a Switch pad) is Xbox A, as on an Xbox pad (a choice; by label would swap A/B and X/Y).
- **With both fixes, through Wine**: every button where it belongs (B bottom as A, A right as B, Y
  left as X, X top as Y; L, R as LB, RB; ZL, ZR as the triggers, 0 or 255; minus Back, plus Start;
  both stick presses; the D-pad), and both sticks over their full range with up positive. Home and
  Capture have no XInput button (Guide and `misc1` for games that read them otherwise). The T6
  reports a square, not a circle (at the corners both axes are at their ends), which first looked
  like a stuck axis; and its sticks rest a little off centre (up to 2656 of 32767, about 8 %), inside
  XInput games' usual dead zone.
- **Gyro**: not reachable now. Through SDL, Wine passes no motion to Windows; Windows games that use a
  Switch pad's gyro read its raw reports (through Wine's raw-node path), which needs the raw node.
  So gyro waits for raw-node access (the system question below), and then only helps games that
  support a Switch pad natively.

## Step 1: the DualSense, Bluetooth (2026-11-22)

- **Linux**: `DualSense Wireless Controller [054c:0ce6, Bluetooth]`, kernel driver `playstation`
  (firmware `0x010e0006`, hardware `0x711`), with three input devices, all readable for phablet: the
  gamepad (`event7`: sticks and triggers 0..255, the D-pad as a hat, 13 buttons), "Motion Sensors"
  (`event8`, the gyro and accelerometer) and "Touchpad" (`event9`). `/dev/hidraw0` `root:1018`, not
  readable.
- **Wine, as it is: nothing**, the lead confirmed: SDL found it (`sdl_add_device ... 054c:0ce6`),
  `winebus` dropped it (`ignoring non-hidraw device 054c:0ce6 with usages 0001:0005`), the raw node
  `Permission denied`. **This is why the DualSense reached no game on either phone.**
- **SDL's layout fits** this driver (GUID `050057564c050000e60c000000810000`, "PS5 Controller"): Cross
  A, Circle B, Square X, Triangle Y, Create Back, Options Start, PS Guide, L1/R1, the stick presses,
  the D-pad as the hat, L2/R2 as the analog axes. No layout fix needed.
- **With fix 1 for Sony** (`Hidraw` = 0 under `...\WineBus\Devices\054c`, test prefix): XInput
  controller 0; DirectInput and HID "Wireless Controller" (`054c:0ce6`). Every button arrived (A, B,
  X, Y, LB, RB, Start, Back, LS, RS, the D-pad), the triggers analog (R2 to 255, L2 to 239 in this
  run), both sticks over their full range in all four directions, resting within 2 % of centre.
- **Not through this path**: the touchpad, the gyro, the light bar, the adaptive triggers. Windows
  games with their own DualSense support read the pad's raw reports, so they need the raw node too.

## Step 1: the DualShock 4 clone, Bluetooth (2026-11-22)

- **Linux**: `Wireless Controller [054c:05c4, Bluetooth]`, kernel driver `sony`, with the gamepad,
  "Motion Sensors" and "Touchpad" devices (all readable); `/dev/hidraw0` `root:1018`, not readable.
  Over Bluetooth `hid-sony` accepts the clone: the address it asks a USB pad for (feature report
  `0x81`, which this clone does not answer, the Dolphin port's stage 1) comes from the Bluetooth
  connection here.
- **SDL's layout fits** ("PS4 Controller", GUID `05009b514c050000c405000000810000`; the same layout
  as the DualSense: sticks and triggers 0..255, D-pad as a hat).
- **With fix 1 for Sony** (`...\Devices\054c` covers every Sony ID): XInput controller 0, DirectInput
  and HID "Wireless Controller" (`054c:05c4`). Every button (A, B, X, Y, LB, RB, Share as Back,
  Options as Start, the D-pad, LS, RS), both sticks to their ends in all four directions, resting
  exactly at centre. The triggers: this clone's L2/R2 are digital (the maintainer): it reports them on
  the analog trigger axes as a real DualShock 4 does, but only ever 0 or 255 (seen: nothing between),
  so games get a full press or none. Without the fix it would be dropped as the DualSense was
  (the same `winebus` rule).
- `evtest.py` fixed on the way: it printed a device's name with its trailing NUL bytes (the source
  had a backslash and a zero where a NUL byte was meant).

## Step 1: the DualShock 4 clone, USB (2026-11-22)

stone has one USB port, so adb cannot stay connected while a controller is plugged in. A new script,
`tools/ut-probe/usbpad-capture.sh`, started over adb as a user service
(`systemd-run --user --unit=utdeck-usbpad`; from adb's shell it needs `XDG_RUNTIME_DIR` and
`DBUS_SESSION_BUS_ADDRESS` set, which adb's shell lacks), waits for a USB device, and for each one
writes its IDs and bound drivers, 12 s later `padcheck.sh` and the kernel's USB and controller
messages, and, if a game controller's evdev node appeared, a Wine test (`gptest.sh 60`) while the
user presses buttons. The log is collected when adb is back. (Its first version stopped after the first
device; a controller that drops off and comes back is now a new round.)

The clone over the OTG cable, 21:31:10: USB found it (`054c:05c4`, "Wireless Controller", its HID
interface bound to `usbhid`), then, as in the Dolphin port's stage 1:

```
sony 0003:054C:05C4.0009: failed to retrieve feature report 0x81 with the DualShock 4 MAC address
sony 0003:054C:05C4.0009: hidraw0: USB HID v81.11 Gamepad [Sony Computer Entertainment Wireless Controller] ...
sony 0003:054C:05C4.0009: failed to claim input
```

No input device, and the raw node went with the failed driver: nothing is left for SDL or Wine. About
15 s after plugging in the pad dropped off USB (`USB disconnect`), the disconnect the maintainer saw
(their second try fell outside that first script version).

**Reason, written down**: stone's `hid-sony` (Linux 5.4) gives up on a USB DualShock 4 that does not
answer feature report `0x81` (its MAC address); real DualShock 4s answer, this clone does not. No user
space fix can reach a device the kernel dropped. The ways out: Bluetooth (works fully, above), or a
patch to stone-ut's kernel that does not give up on a failed `0x81` read (a kernel build and flash:
the maintainer's decision).

## Step 1: the BETOP T6, wired (2026-11-22)

- The maintainer: the T6 has no Switch mode on the cable, and on a Windows PC it shows up as an Xbox
  360 controller. **On stone it came up as a Switch Pro Controller every time** (six plug-ins, the
  capture script's and the kernel's log): behind its own small hub (`1a40:0101` "USB 2.0 Hub"),
  `057e:2009` "BETOP T6 FOR NS", bound by `hid-nintendo`, an input device
  "Nintendo Switch Pro Controller". Likely the pad picks its mode from the host: Windows asks a USB
  device for Microsoft's own descriptor, Linux does not, and the T6 then takes its Switch mode (many
  pads do this). The Xbox mode on the cable would need the pad to be told otherwise (a button held
  while plugging in, if it has one) or the host to ask as Windows does.
- **Over USB, SDL picks yet another layout of its own** (GUID `030056fb7e0500000920000011010000`:
  bus 03, version `0111`): by label, D-pad again as a hat, so again wrong for stone's driver. The
  capture script applied the corrected layout to that GUID (the layout is the same; only the GUID
  differs per bus and version). With both fixes, through Wine: LB, RB, LT 255, RT 255, the right
  stick and RS arrived; the face buttons were pressed before Wine was ready.
- **It drops off USB**: after 2 to 6 s in four of the first five plug-ins, after 27 s once, and
  after about a minute in the captured run (`XInput 0 disconnected`); the DualShock 4 clone dropped
  after about 15 s on the same cable. The kernel's log shows `USB disconnect` (sometimes the whole
  host controller removed and registered again right after), with nothing from the port controller
  to explain it. Open: the phone's USB-C port, the OTG adapter or cable, or the pads. Bluetooth has
  no such drops.

## Rumble

| Controller | Rumble felt | Why |
| --- | --- | --- |
| T6, Xbox mode (Bluetooth) | yes | `hid-microsoft`, through SDL |
| T6, Switch mode | **no** | stone's `hid-nintendo` is an early version (820 lines, in stone-ut's `third_party/android_kernel_xiaomi_sm6375/drivers/hid/hid-nintendo.c`): no rumble and no motion device; both came with the full driver (Linux 5.16, `CONFIG_NINTENDO_FF`, absent here). Kernel-side only |
| DualSense (Bluetooth) | yes | `hid-playstation` with `CONFIG_PLAYSTATION_FF` |
| DualShock 4 clone (Bluetooth) | yes, through the Controllers page's test; not felt in `gptest`'s | the page's test (SDL directly, 75 % strength for 0.7 s) was felt, so `hid-sony`'s rumble reaches the clone. `gptest`'s (through Wine's XInput, 46 % for 1 s) was not: likely too weak for the clone's motors; to see in a game that rumbles |

## Step 2: the fixes in UTDeck (2026-11-22)

The maintainer chose the layout by position (2026-11-22).

- **`tools/ut-probe/controllers.reg`**: `Hidraw` = 0 under `WineBus\Devices\054c` (Sony) and
  `\057e` (Nintendo). `game-run.sh`, the start every game goes through (Steam's and, in 4.5, the
  user's own), imports it into the game's prefix whenever the file changes (its checksum in
  `<prefix>/utdeck-controllers-reg`), and logs "controllers: registry settings imported". Checked
  on stone in the games prefix: imported once, both keys there.
- **`tools/ut-probe/padlayout.py`**, run by `game-run.sh` before the game: for each controller SDL
  sees through evdev, SDL's layout is compared with what the kernel says the device has. Only if SDL
  has no layout for a gamepad, or its layout uses a D-pad hat the device lacks, a layout is made from
  the kernel's buttons by position (bottom A, right B, left X, top Y; the triggers from the analog
  axes if there are both, else the buttons; Capture as `misc1`) and given to SDL in
  `SDL_GAMECONTROLLERCONFIG`; what it did goes into the game's log ("padlayout: ..."). Numbered as SDL 2
  numbers evdev buttons and axes (buttons from `BTN_JOYSTICK` up, then those below; axes without the
  hats). Checked offline: for the T6's Switch mode it gives the hand-made layout of step 1, and for the
  DualSense exactly SDL's own layout. It does not touch a pad SDL already gets right, and a phone
  whose `hid-nintendo` is the full driver (whose layout SDL may fit) is left to SDL.
- Both files go to the phone with `runtime-push.sh` and in the click (`home-files.sh`): UTDeck
  0.3.16 carries them (checked in the click), installed on stone.
- **Checked in a game** (Ys: Memories of Celceta from UTDeck, 0.3.16, the maintainer, 2026-11-22):
  the **DualSense works** (before 4.4 it reached no game) and the **T6 in Switch mode works**. The
  game logs: with the DualSense `padlayout: "DualSense Wireless Controller" (050057564c...): SDL's
  layout fits`; with the T6 `padlayout: "Nintendo Switch Pro Controller" (050056fb...): SDL's layout
  expects a D-pad hat the device does not have; a layout by position from the kernel's buttons is
  used`. The registry settings went in once per prefix: Celceta's at its install script, Steam's own
  graphics check's two (`compatdata/0/pfx` and `pfx-proton`, one per Wine base).
- Limits: a controller connected after the game started keeps SDL's own layout (the registry part
  applies to it); SDL 2 must be on the phone (the Nothing Phone: to check).

## Steps 3 and 4: the Controllers page (2026-11-22, UTDeck 0.3.17)

The maintainer chose the hybrid (2026-11-22): instant from Linux, Wine on request.

- **Settings, Controllers** (`app/qml/ControllersPage.qml`, `app/src/Controllers.cpp`). The page
  reads the controllers **through the phone's own SDL 2** (`libSDL2-2.0.so.0`, loaded at run time
  with `dlopen`; the click does not carry it, and a phone without it gets a message), the library
  Wine's `winebus` reads them through. So each button shows under the name a game gets for an Xbox
  pad, with the same layouts: before opening the controllers the page runs `padlayout.py` and gives
  its layouts to its own SDL. (Reading the evdev nodes directly would show the T6's Xbox mode under
  generic codes, `hid-microsoft`, which only SDL's database turns into a layout.)
- For each controller: name, connection (Bluetooth or USB), USB IDs, kernel driver; "Games see: Xbox
  controller" (or "Joystick" when SDL has no layout), with "UTDeck's layout for this phone's driver"
  where `padlayout.py` made one, and "it reaches games through UTDeck's Wine setting" for Sony and
  Nintendo pads; the buttons lit while pressed, the sticks as dots in squares, the triggers as bars
  (polled 30 times a second, only while the page is open); "Test rumble" through SDL, which also says
  when the kernel driver has none ("No rumble on this controller on this phone").
- **A USB controller the kernel refused**: a USB device with a HID interface but no input device
  behind it, shown with the reason in words (for the DualShock 4 IDs: the address request, "Connect it
  over Bluetooth instead"). Followed once a second.
- **A controller connected or removed while the page is open**: the layouts are decided again and
  all controllers reopened.
- **Check in Wine** (not while a game runs): `game-run.sh controllers wine/padprobe gptest.exe 0`, as a
  game starts (the registry setting, `padlayout.py`), but not counted as one (`UTDECK_SETUP_RUN=1`, no
  pause, no boost); from its log, what XInput, DirectInput and HID offer games. `gptest.exe` goes into
  the click (`home-files.sh`: `wine/padprobe/`).
- A Bluetooth settings button (`settings:///system/bluetooth`) for pairing.

- **Checked on stone** (the maintainer, 2026-11-22): with the DualSense and with the T6 in Switch
  mode (connected while the page was open), everything as expected (buttons, sticks, triggers live,
  the rumble test, the Wine check), and the layout of the page fine. The Wine check's logs: the
  DualSense as XInput controller 0, DirectInput and HID "Wireless Controller" (`054c:0ce6`), SDL's
  layout; the T6 as XInput controller 0, DirectInput and HID "Nintendo Switch Pro Controller"
  (`057e:2009`), UTDeck's layout.

- **In a game, the DualShock 4 clone over Bluetooth works** (Celceta from UTDeck, the maintainer,
  2026-11-22; `padlayout: "Wireless Controller" (05009b514c...): SDL's layout fits`).
- `padlayout.py` now logs one line for every controller, also those it leaves to SDL without
  looking at their buttons (read by SDL's own driver, or no gamepad buttons from the kernel), so a
  game's log always names the controllers it started with.

- **In a game, the T6 in Xbox mode over Bluetooth works** (Celceta, 2026-11-22;
  `padlayout: "Xbox One S Controller" (0500672b5e...): SDL's layout fits`; SDL names it by its IDs,
  `045e:02e0`).
- UTDeck 0.3.18 carries the `padlayout.py` that logs every controller. Installed on stone while
  Celceta was running: the game went on, but the app was stopped under it (its library would not
  have come back at the game's end). `install-click.sh` now refuses while UTDeck's game file names a
  running game (`FORCE=1` to install anyway).

## Done (2026-11-22)

The exit, on stone:

| Controller | In a game from UTDeck | How |
| --- | --- | --- |
| T6, Xbox mode, Bluetooth | works (Celceta), rumble | SDL as it is |
| T6, Switch mode, Bluetooth | works (Celceta); no rumble, no gyro | `controllers.reg` + `padlayout.py`; rumble and gyro: the kernel driver (below) |
| T6, wired | comes up in Switch mode; works with the same fixes in the Wine test; drops off USB | reason written: the pad picks its mode from the host; the drops are open |
| DualSense, Bluetooth | works (Celceta), rumble | `controllers.reg` |
| DualShock 4 clone, Bluetooth | works (Celceta), rumble (the page's test) | `controllers.reg` |
| DualShock 4 clone, USB | refused by the kernel | reason written: `hid-sony` gives up on the `0x81` read; the page says so |
| Switch Pro (genuine) | untested (none) | the T6's Switch mode stands in |

The Controllers page shows each working controller live, with the rumble test and the check in Wine.

Open, for later stages or the maintainer's decision:
- **A controller connected while a game runs (K3)**: not looked at in 4.4. The registry part applies
  to it; `padlayout.py`'s layout does not (it is decided at the game's start).
- **SDL 2 on the Nothing Phone (1)**: Wine and the page need the phone's `libSDL2-2.0.so.0`; stone
  has it (Ubuntu's SDL 2.30), the Nothing Phone is to check (the runtime may have to carry it).
- **Raw-node access** (a udev rule on the phone, a system change): the DualSense's touchpad, gyro and
  light bar and a Switch pad's gyro for games with their own support. The maintainer's decision.
- **stone-ut's kernel** (a build and flash): the full `hid-nintendo` (rumble and gyro in Switch mode)
  and a `hid-sony` that does not give up on a clone's missing `0x81` answer. The maintainer's decision.
- **The USB drops** (the T6 and the clone on the OTG cable): the phone's port, the adapter, or the pads.
- **The T6's Xbox mode on the cable**: the pad picks Switch mode on Linux; a way to force its Xbox
  mode (a button held while plugging in), if it has one.

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

On stone: the BETOP T6 (Xbox mode over Bluetooth and wired, Switch mode), the DualShock 4 clone and
the DualSense each work in a game started from UTDeck, or have the reason they do not written down
(with the fix proposed, if it needs the maintainer's OK); a genuine Switch Pro marked untested
(the T6's Switch mode stands in); the Controllers page shows each working controller live, with
rumble.

## Answers from the maintainer (2026-11-22)

- There is no genuine Xbox controller: the BETOP T6 in its Xbox mode is the Xbox controller.
- The T6 also has a **Nintendo Switch mode, with gyro** (motion sensing, which its Xbox mode does not
  have): it stands in for a Switch Pro controller. Gyro reaches Windows games only through a path that
  keeps the Switch pad's own reports (hidraw, or SDL's sensor support); in XInput there is none.
- Still open: whether the T6 has a wired X-input mode, and how it is switched.
