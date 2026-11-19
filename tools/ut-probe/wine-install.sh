#!/bin/sh
# UTDeck stage 2.4: run a Windows installer silently in the test prefix, logging Wine's errors.
#   wine-install.sh <label> <installer.exe> [installer arguments...]
# Start it as a user unit (an adbd restart cannot kill it):
#   systemd-run --user --unit=utdeck-install-<label> --collect wine-install.sh <label> ...
# Writes install-<label>.log and install-<label>.status in the wine test folder.
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
export WAYLAND_DISPLAY=wayland-0
label=$1; shift
{
  echo "start $(date +%T): $*"
  s=$(date +%s)
  WINEDEBUG=err+all,fixme-all timeout 1800 wine "$@" > $W/install-$label.log 2>&1
  rc=$?
  wineserver -w
  echo "exit $rc after $(( $(date +%s) - s )) s ($(date +%T))"
} > $W/install-$label.status 2>&1
