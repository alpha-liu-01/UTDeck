#!/bin/sh
# UTDeck stage 3.3: the graphics tests (rt-gfx.sh) on two runtimes, alternated, so that warming up and
# other drift fall on both sides alike.
#
#   sh ab-gfx.sh ROUNDS RUNTIME_A RUNTIME_B      e.g. sh ab-gfx.sh 3 proton utdeck
#
# A runtime is a kind (utdeck, proton, hangover) or KIND:FOLDER for a test build, e.g.
# utdeck:runtime-p13 (see rt-gfx.sh). Each round runs A, then B (the next round starts with A
# again). Before each run: the hottest thermal zone, the free memory and the screen's backlight
# (0 = off: a run with the screen off measures a phone nobody plays on). The screen is kept on
# meanwhile (repowerd's keepDisplayOn, released when the script ends). The rt-gfx logs stay in the
# wine folder; a summary goes to ab-gfx-<time>.txt next to them.
set -u
W=/home/phablet/utdeck-probe/wine
OUT=$W/ab-gfx-$(date +%H%M%S).txt
hot() { cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | sort -n | tail -1 | awk '{print int($1/1000)}'; }
python3 -c 'import dbus, time
s = dbus.SystemBus().get_object("com.canonical.Unity.Screen", "/com/canonical/Unity/Screen")
s.keepDisplayOn(dbus_interface="com.canonical.Unity.Screen")
time.sleep(4 * 3600)' > /dev/null 2>&1 &
KEEP=$!
trap 'kill $KEEP 2>/dev/null' EXIT
for r in $(seq 1 "$1"); do
    for rt in "$2" "$3"; do
        kind=${rt%%:*} dir=; case "$rt" in *:*) dir=${rt#*:} ;; esac
        echo "round $r, $rt: start $(date +%T), hottest zone $(hot) C, free $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB, backlight $(cat /sys/class/backlight/*/brightness | head -1)" >> "$OUT"
        sh "$W/rt-gfx.sh" "$kind" $dir
        L=$(ls -t "$W"/rt-gfx-"$kind"-*.log | head -1)
        echo "  log $(basename "$L")" >> "$OUT"
        awk '/^--- /{t=$0} /^swapchain:/{s=$2} /frame time over|frames: [0-9]+ in/{sub(/frame time over [0-9]+ frames: /, ""); print "  " substr(t, 5) " [" s "] | " $0} /^exit: [^0]/{print "  " substr(t, 5) " FAILED: " $0}' "$L" | cut -c1-140 >> "$OUT"
    done
done
echo "summary: $OUT"
