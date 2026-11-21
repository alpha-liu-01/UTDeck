#!/bin/sh
# UTDeck stage 3.4: the stage 2 graphics tests (rt-gfx.sh) on one runtime with two settings,
# alternated, to measure a default (on the phone).
#
#   sh ab-env.sh ROUNDS "A SETTINGS" "B SETTINGS" [KIND[:FOLDER]]
#       e.g. sh ab-env.sh 2 "DXVK_LOG_LEVEL=none" "DXVK_LOG_LEVEL=info"
#
# SETTINGS: NAME=value words exported for that run ("-" for none); RT_WINEDEBUG sets the test
# programs' Wine log level. KIND[:FOLDER] as in ab-gfx.sh (default: utdeck, the installed runtime).
# Before each run: the hottest thermal zone, the free memory and the screen's backlight; the screen
# is kept on meanwhile. A summary goes to ab-env-<time>.txt in the wine folder.
set -u
W=/home/phablet/utdeck-probe/wine
OUT=$W/ab-env-$(date +%H%M%S).txt
RT=${4:-utdeck}
kind=${RT%%:*} dir=; case "$RT" in *:*) dir=${RT#*:} ;; esac
hot() { cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | sort -n | tail -1 | awk '{print int($1/1000)}'; }
# lomiri_ok: Lomiri answers on D-Bus within 5 s and its lock screen is not up. In stage 3.4 Lomiri
# stopped drawing during a run while still accepting windows, and every later number was a picture
# nobody drew; the backlight stayed on, so only asking Lomiri itself catches that.
lomiri_ok() {
    a=$(XDG_RUNTIME_DIR=/run/user/$(id -u) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus \
        gdbus call --session --timeout 5 --dest com.lomiri.LomiriGreeter --object-path /com/lomiri/LomiriGreeter \
        --method org.freedesktop.DBus.Properties.Get com.lomiri.LomiriGreeter IsActive 2>&1)
    case "$a" in *"<false>"*) return 0 ;; esac
    echo "  STOPPED: Lomiri not usable ($a)" >> "$OUT"; return 1
}
python3 -c 'import dbus, time
s = dbus.SystemBus().get_object("com.canonical.Unity.Screen", "/com/canonical/Unity/Screen")
s.keepDisplayOn(dbus_interface="com.canonical.Unity.Screen")
time.sleep(4 * 3600)' > /dev/null 2>&1 &
KEEP=$!
trap 'kill $KEEP 2>/dev/null' EXIT
for r in $(seq 1 "$1"); do
    for set in "$2" "$3"; do
        lomiri_ok || { echo "summary: $OUT (stopped)"; exit 1; }
        echo "round $r, [$set] on $RT: start $(date +%T), hottest zone $(hot) C, free $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB, backlight $(cat /sys/class/backlight/*/brightness | head -1)" >> "$OUT"
        ( [ "$set" = - ] || for v in $set; do export "$v"; done
          sh "$W/rt-gfx.sh" "$kind" $dir )
        L=$(ls -t "$W"/rt-gfx-"$kind"-*.log | head -1)
        echo "  log $(basename "$L")" >> "$OUT"
        awk '/^--- /{t=$0} /^swapchain:/{s=$2} /frame time over|frames: [0-9]+ in/{sub(/frame time over [0-9]+ frames: /, ""); print "  " substr(t, 5) " [" s "] | " $0} /^exit: [^0]/{print "  " substr(t, 5) " FAILED: " $0}' "$L" | cut -c1-140 >> "$OUT"
    done
done
echo "summary: $OUT"
