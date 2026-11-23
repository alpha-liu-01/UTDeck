#!/bin/sh
# UTDeck stage 2.4: a gamepad through Wine (winebus: SDL2, hidraw or evdev), read with XInput and
# winmm by gptest.exe. Connect the controller first.
#   cd /home/phablet/utdeck-probe/wine && ./gptest.sh [SECONDS]      (0: list the controllers only)
# Stage 4.4: first what Linux makes of the controller (padcheck.sh: driver, nodes, permissions), then
# what Windows offers games (XInput, winmm, DirectInput 8, HID), then winebus' own decisions from its
# log gp-<time>.log: which path (SDL, udev "input" = evdev, hidraw) added each controller, and which
# it ignored (winebus takes a PlayStation pad only through hidraw).
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0
L=$W/gp-$(date +%H%M%S).log
echo "=== Linux"
/home/phablet/utdeck-probe/padcheck.sh
echo
echo "=== Windows (gptest.exe)"
cd $W/test
WINEDEBUG=err+all,warn+hid,trace+hid wine gptest.exe ${1:-30} 2> $L
wineserver -k
echo
echo "=== winebus (from $L)"
grep -E "ignoring|_add_device|bus_create_hid_device|disabled in registry|failed to open|Permission denied|SDL.*(version|init)" $L \
    | sed -E 's/^[0-9a-f]+:(trace|warn|err):hid:/  /' | cut -c1-200 | sort -u | head -40
