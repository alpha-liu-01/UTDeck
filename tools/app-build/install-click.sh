#!/bin/sh
# Installs UTDeck's click on the phone over adb (stage 4.1), as clickable does on 24.04: through the
# click service on the system bus (there is no pkcon). After the Dolphin port's ut/click/install-click.sh.
#   tools/app-build/install-click.sh [FILE.click]
#
# Without a file, it takes the newest click built for the phone's Ubuntu Touch release: the one in
# build/app-click/ for 24.04-2.x, or in build/app-click/ut-24.04-1.x/ for 24.04-1.x. With several
# phones connected, choose one with ANDROID_SERIAL=<serial> (adb devices).
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
if [ $# -ge 1 ]; then
  click=$1
else
  frameworks=$(adb shell ls /usr/share/click/frameworks/)
  case "$frameworks" in
    *ubuntu-touch-24.04-2.x.framework*) dir="$root/build/app-click"; ut=24.04-2.x ;;
    *ubuntu-touch-24.04-1.x.framework*) dir="$root/build/app-click/ut-24.04-1.x"; ut=24.04-1.x ;;
    *) echo "the phone is neither Ubuntu Touch 24.04-2.x nor 24.04-1.x" >&2; exit 1 ;;
  esac
  click=$(ls -t "$dir"/*.click 2>/dev/null | head -1) || true
  if [ -z "$click" ]; then
    echo "the phone runs $ut: build its click first (CLICK_UT=$ut tools/app-build/build-click.sh)" >&2
    exit 1
  fi
  echo "Ubuntu Touch $ut: $click"
fi
file=$(basename "$click")
# A running copy is stopped first (stage 4.1): Lomiri pauses an app in the background, and an app
# paused while its click was upgraded could not be closed any more (Lomiri resumed the new version's
# name, "Invalid app ID: ..._0.1.0.2x"), and held every new start in a queue. Stopping the app's
# systemd unit ends it (systemd wakes a paused process to end it). adb's shell has no session
# environment, so the user bus is given by hand (lomiri-app-list lists nothing even then).
package=${file%%_*}
# Not while a game runs (stage 4.4): stopping the app takes the library away from under the game
# (Celceta kept running, but the library did not come back when it ended). The game file
# (game-run.sh: "LABEL PID FOCUS") names a running game-run.sh then. FORCE=1 installs anyway.
if [ "${FORCE:-0}" != 1 ]; then
  game=$(adb shell 'f=/run/user/$(id -u)/utdeck/game; [ -f "$f" ] && read -r label pid _ < "$f" && kill -0 "$pid" 2>/dev/null && echo "$label"' | tr -d '\r')
  if [ -n "$game" ]; then
    echo "a game is running on the phone ($game): quit it first, or FORCE=1 to install anyway" >&2
    exit 1
  fi
fi
adb shell "export XDG_RUNTIME_DIR=/run/user/\$(id -u) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/\$(id -u)/bus
  for u in \$(systemctl --user list-units --plain --no-legend 'lomiri-app-launch--application-click--${package}_*' | awk '{print \$1}'); do
    echo \"stopping \$u\"; systemctl --user stop \"\$u\"
  done"
adb push "$click" "/home/phablet/$file" >/dev/null
adb shell "gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click \
  --method com.lomiri.click.Install /home/phablet/$file >/dev/null && sleep 8 && rm -f /home/phablet/$file \
  && click list | grep utdeck"
