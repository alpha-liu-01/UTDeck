#!/bin/sh
# UTDeck stage 2.4: d3d11test with a 1280x720 display mode change, three times, about 10 s each:
# UTDECK_SCALE=fit, stretch, center (patch 0006). Started from utdeck-scaletest.desktop (landscape).
# Log: /home/phablet/utdeck-probe/wine/m7-<time>.log
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
L=$W/m7-$(date +%H%M%S).log
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} MESA_VK_WSI_PRESENT_MODE=mailbox UTDECK_WAYLAND_LANDSCAPE=1
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core,d3d9,d3d8=n;mscoree,mshtml="
unset DISPLAY
cd $W/test
for sc in fit stretch center; do
  echo "===== UTDECK_SCALE=$sc" >> $L
  UTDECK_SCALE=$sc WINEDEBUG=err+all,trace+vulkan,trace+waylanddrv timeout 40 wine d3d11test.exe --frames 1500 --mode 1280x720 --load 4 >> $L 2>&1
  wineserver -k; sleep 2
done
