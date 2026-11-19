#!/bin/sh
# UTDeck stage 2.3, manual run M3: rotation with patch 0003 (screen on and unlocked, rotation lock
# off). vktest (x86-64, fullscreen, MAILBOX) for about 35 seconds per part.
#
#   cd /home/phablet/utdeck-probe/wine && ./wine-m3.sh
#
# Logs: m3-<time>-<part>.log (program output and every Wayland message).
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0 MESA_VK_WSI_PRESENT_MODE=mailbox
T=$(date +%H%M%S)
cd $W/test
part() { # name landscape-option description
  name=$1; land=$2; desc=$3
  echo
  echo "== $name: $desc"
  wineserver -k; sleep 2
  UTDECK_WAYLAND_LANDSCAPE=$land WAYLAND_DEBUG=client WINEDEBUG=err+all,trace+waylanddrv timeout 90 \
      wine vktest-x86_64.exe --frames 8000 --fullscreen > $W/m3-$T-$name.log 2>&1
  rc=$?
  grep -E "^(window [0-9]+: |swapchain|frame time|FAILED)" $W/m3-$T-$name.log | sed 's/^/   /'
  echo "   exit $rc (124 = stopped by the time limit)"
}
part a 0 "start in portrait; while it runs, rotate to landscape, wait, back to portrait, then landscape again"
part b 1 "landscape monitor: HOLD THE PHONE IN LANDSCAPE before it starts; then rotate to portrait and back"
wineserver -k
echo
echo done
