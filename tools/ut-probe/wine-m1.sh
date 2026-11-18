#!/bin/sh
# UTDeck stage 2.2, manual run M1: Wine windows on Lomiri (screen on and unlocked).
#
#   cd /home/phablet/utdeck-probe/wine && ./wine-m1.sh
#
# Starts Wine's Notepad, then winecfg, each with every Wayland message logged
# (m1-notepad.log, m1-winecfg.log). Close each window yourself (File > Exit, or OK); each one is
# also closed after 3 minutes.
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0
for prog in notepad winecfg; do
  echo "== $prog: look at it, tap it, then close it"
  WAYLAND_DEBUG=client WINEDEBUG=err+all,warn+waylanddrv timeout 180 wine $prog > $W/m1-$prog.log 2>&1
  echo "   exit $? (124 = closed after 3 minutes)"
done
wineserver -k
echo done
