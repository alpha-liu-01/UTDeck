#!/bin/sh
# UTDeck stage 3.6: what a running game does while the player is elsewhere (on the phone).
#
#   sh lifecycle-watch.sh SECONDS [OUT]
#
# Once a second: the screen's backlight, whether Lomiri's lock screen is up, the game's processes
# (the largest Wine program that is not one of Wine's services) with their states (R running,
# S sleeping, T stopped by a signal) and CPU use, the GPU's load, the frames the game presented in
# the last second (from game-run.sh's frames file), and its sound stream (PulseAudio: playing or
# corked, i.e. paused). One line per second to OUT (default: wine/games/lifecycle-<time>.txt).
set -u
N=${1:-120}
W=/home/phablet/utdeck-probe/wine
OUT=${2:-$W/games/lifecycle-$(date +%H%M%S).txt}
export XDG_RUNTIME_DIR=/run/user/$(id -u) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
K=/sys/class/kgsl/kgsl-3d0
BL=$(ls /sys/class/backlight/*/brightness | head -1)
# The game: the largest process whose command line names a .exe (with or without a Windows path),
# Wine's own services and Steam's helper left out.
game() { ps -eo pid,rss,args | grep -i "[.]exe" | grep -v -i -E "services|winedevice|plugplay|svchost|explorer|rpcss|steam\.exe|conhost|tabtip|game-run|lifecycle" | sort -k2 -rn | awk 'NR==1{print $1}'; }
ticks() { awk '{print $14 + $15}' /proc/$1/stat 2>/dev/null || echo 0; }
locked() {
    a=$(gdbus call --session --timeout 1 --dest com.lomiri.LomiriGreeter --object-path /com/lomiri/LomiriGreeter \
        --method org.freedesktop.DBus.Properties.Get com.lomiri.LomiriGreeter IsActive 2>/dev/null)
    case "$a" in *true*) echo locked ;; *false*) echo unlocked ;; *) echo lomiri-silent ;; esac
}
sound() {   # the stream of a Wine process: corked or playing ("-" when none)
    pactl list sink-inputs 2>/dev/null | awk '/^Sink Input/{c=""} /Corked:/{c=$2} /application.process.binary = "(wine|wine64|wine-preloader|wine64-preloader)"/ || /application.name = ".*\.exe"/{if(c!="") {print (c=="yes" ? "corked" : "playing"); exit}}' | head -1
}
echo "lifecycle watch, $N s, from $(date +%T)" > "$OUT"
P=$(game); t0=$(ticks ${P:-0})
for i in $(seq 1 "$N"); do
    sleep 1
    p=$(game)
    [ "$p" != "$P" ] && { P=$p; t0=$(ticks ${P:-0}); }
    t1=$(ticks ${P:-0}); cpu=$(( (t1 - t0) * 100 / $(getconf CLK_TCK) )); t0=$t1
    st=$( [ -n "$P" ] && for t in /proc/$P/task/*; do awk '{print $3}' $t/stat 2>/dev/null; done | sort | uniq -c | awk '{printf "%s%s ", $1, $2}')
    F=$(ls -t $W/games/*.frames 2>/dev/null | head -1)
    fr=$( [ -n "$F" ] && [ $(( $(date +%s) - $(stat -c %Y "$F") )) -le 2 ] && tail -1 "$F" | grep -o "[0-9]* frames in" | cut -d' ' -f1 || echo 0)
    echo "$(date +%T) screen $(cat $BL) $(locked) game ${P:-none} [${st:-}] cpu ${cpu}% gpu $(tr -d ' %' < $K/gpu_busy_percentage)% frames/s ${fr:-0} sound $(s=$(sound); echo ${s:--})" >> "$OUT"
done
echo "watch: $OUT"
