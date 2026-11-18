#!/bin/sh
# UTDeck stage 2.3, manual run M2: a Windows Vulkan program (our vktest.exe, x86-64 through FEX) on
# Lomiri, through Wine's Vulkan and our Turnip (screen on and unlocked).
#
#   cd /home/phablet/utdeck-probe/wine && ./wine-m2.sh
#
# vktest draws a color that slowly changes and a white vertical bar moving across the picture.
# Logs: m2-<time>-<part>.log (program output and every Wayland message).
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0
T=$(date +%H%M%S)
cd $W/test
part() { # name seconds description, then vktest arguments
  name=$1; secs=$2; desc=$3; shift 3
  echo
  echo "== $name: $desc"
  WAYLAND_DEBUG=client WINEDEBUG=err+all timeout $secs wine vktest-x86_64.exe "$@" > $W/m2-$T-$name.log 2>&1
  rc=$?
  grep -E "^(window [0-9]+: [0-9]|frame time|  bands|swapchain|FAILED)" $W/m2-$T-$name.log | sed 's/^/   /'
  echo "   exit $rc (124 = stopped by the time limit)"
  wineserver -k; sleep 2
}
export MESA_VK_WSI_PRESENT_MODE=mailbox
part a 60 "a window (not fullscreen), 10 seconds" --frames 3000 --size 960x540
part b 60 "fullscreen, three times in a row (watch for an old picture left behind between them)" --frames 1000 --fullscreen --cycles 3
part c 90 "fullscreen, about 25 seconds: ROTATE the phone to landscape and back while it runs" --frames 8000 --fullscreen
unset MESA_VK_WSI_PRESENT_MODE
part d 30 "fullscreen in FIFO (vsync) mode, which hangs with the screen off: does it with the screen on?" --frames 600 --fullscreen
echo
echo done
