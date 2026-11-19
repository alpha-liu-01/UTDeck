#!/bin/sh
# UTDeck stage 2.4: a gamepad through Wine (winebus: SDL2, hidraw or evdev), read with XInput and
# winmm by gptest.exe. Connect the controller first.
#   cd /home/phablet/utdeck-probe/wine && ./gptest.sh [SECONDS]
# Also lists the Linux input devices, and logs winebus' messages to gp-<time>.log.
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0
L=$W/gp-$(date +%H%M%S).log
echo "Linux input devices:"
for n in /sys/class/input/input*/name; do echo "  $(basename $(dirname $n)): $(cat $n)"; done
ls -la /dev/hidraw* 2>/dev/null | sed 's/^/  /'
cd $W/test
WINEDEBUG=err+all,warn+hid,trace+hid wine gptest.exe ${1:-30} 2> $L
wineserver -k
echo "winebus messages: $(grep -c . $L) lines in $L; devices it saw:"
grep -a -i -E "sdl|hidraw|evdev|udev|bus_create_hid_device|gamepad" $L | grep -a -v "^\s*$" | sort -u | head -12 | cut -c1-160
