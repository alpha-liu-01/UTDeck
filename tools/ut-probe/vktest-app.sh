#!/bin/sh
# UTDeck stage 2.3: vktest.exe (x86-64, fullscreen, MAILBOX, landscape monitor) started as a Lomiri
# app, from utdeck-vktest.desktop, so that Lomiri knows the app and its orientation.
# Log: /home/phablet/utdeck-probe/wine/m4-<time>.log
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} MESA_VK_WSI_PRESENT_MODE=mailbox UTDECK_WAYLAND_LANDSCAPE=1
# Lomiri's launcher gives legacy apps an X11 display (Xwayland); with DISPLAY set Wine would pick
# its X11 driver, which our Turnip cannot present to. Wine must use its Wayland driver.
L=$W/m4-$(date +%H%M%S).log
{ echo "launched with: DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY"; } > $L
unset DISPLAY
cd $W/test
WAYLAND_DEBUG=client WINEDEBUG=err+all,trace+waylanddrv timeout 90 wine vktest-x86_64.exe --frames 6000 --fullscreen \
    >> $L 2>&1
# End Wine's background processes too: they live in this app's process group, and Lomiri
# considers the app running (and refuses a new start) until they are gone.
wineserver -k
