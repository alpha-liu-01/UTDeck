#!/bin/sh
# UTDeck stage 3.2: start a Windows game on our runtime, with measurements.
#
#   game-run.sh LABEL GAME_DIR EXE [ARGS...]      (EXE relative to GAME_DIR, or a full path)
#
# Environment (optional): GAME_OVERRIDES (added to the DLL overrides), GAME_ENV (more variables,
# "A=1 B=2"), WINEPREFIX (default: the games prefix, wine/prefix-games).
# Started from a Lomiri desktop file (landscape) or from a shell. Writes into wine/games/:
#   LABEL-<time>.log      Wine's errors and the game's output
#   LABEL-<time>.frames   one line per second from win32u (patch 0007): fps, frame times, size
#   LABEL-<time>.sensors  every 2 s: CPU busy, big-core clock, GPU busy and clock, CPU and GPU
#                         temperature, free memory, the game's resident memory
set -u
LABEL=$1 DIR=$2 EXE=$3; shift 3
W=/home/phablet/utdeck-probe/wine
export WINEPREFIX=${WINEPREFIX:-$W/prefix-games}
. $W/env.sh
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
# DXVK for Direct3D 8-11 (its builds are in the prefix, prefix-dlls.sh); VKD3D-Proton for 12.
# mscoree (.NET's loader) stays on: many games import it without running .NET (Goat Simulator,
# UE3), and with it off such a game cannot load at all (c0000135). Games that run .NET code also
# need Wine Mono in the prefix (the runtime ships it; 3.4).
# (XInput: Wine's own copies are put back into Steam prefixes by utdeck-compat.)
for v in ${GAME_ENV:-}; do export "$v"; done
# The defaults every game gets (stage 3.4), from one file: defaults.env, one NAME=value per line,
# each with the measurement that decided it. A value already set (the game's utdeck-env, GAME_ENV,
# the caller) wins. Among them: MAILBOX presentation, the landscape monitor, fill mode (UTDECK_FILL=0
# turns it off for a game), the game's Wine log level.
DEFAULTS=
if [ -f $W/defaults.env ]; then
    while IFS= read -r line; do
        case "$line" in
        ''|'#'*) continue ;;
        [A-Za-z_]*=*)
            n=${line%%=*}
            if eval "[ -z \"\${$n+x}\" ]"; then export "$line"; DEFAULTS="$DEFAULTS $line"; fi ;;
        esac
    done < $W/defaults.env
fi
# DXVK and VKD3D-Proton as native DLLs (not a choice: our runtime's Direct3D), then the defaults'
# DLL choices (UTDECK_DLL_DEFAULTS: Gecko and winemenubuilder off), then the game's own.
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core,d3d9,d3d8=n;d3d12,d3d12core=n${UTDECK_DLL_DEFAULTS:+;$UTDECK_DLL_DEFAULTS}${GAME_OVERRIDES:+;$GAME_OVERRIDES}"
unset DISPLAY   # Lomiri gives legacy apps Xwayland; Wine must use its Wayland driver
mkdir -p $W/games
B=$W/games/$LABEL-$(date +%H%M%S)
export UTDECK_FRAMESTATS=$B.frames
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    wineboot -i > /dev/null 2>&1; wineserver -w; $W/prefix-dlls.sh > /dev/null
fi
{ printf 'game: %s, in %s: %s %s\n' "$LABEL" "$DIR" "$EXE" "$*"; echo "runtime: $(command -v wine), prefix $WINEPREFIX";
  echo "overrides: $WINEDLLOVERRIDES"; echo "extra: ${GAME_ENV:-}"; echo "defaults:$DEFAULTS"; } > $B.log
# GStreamer's plugin list (env.sh: GST_REGISTRY_1_0) is brought up to date here, outside Wine. Wine
# starts GStreamer with --gst-disable-registry-fork, so a Wine process loads every new or changed
# plugin into itself to list it; the phone's hybris plugin (Android's hardware decoders) cannot load
# without the session's GLIBC_TUNABLES and then ends the whole process: the game dies at its first
# video. Here GStreamer uses its helper process, which takes such failures and marks the plugin
# as unusable; Wine then only reads the list. About 3 s after a system update, 0.1 s otherwise.
python3 -c 'import ctypes; ctypes.CDLL("libgstreamer-1.0.so.0").gst_init(None, None)' >> $B.log 2>&1 \
    || echo "gstreamer: plugin list not updated" >> $B.log
K=/sys/class/kgsl/kgsl-3d0
# Sensor paths, found once by name (zone numbers are not fixed): the little and the big cluster,
# the GPU, the phone's skin. Stage 3.5 adds the allowed maximum clocks (the vendor's thermal service, mi_thermald,
# lowers them when it steps in), the skin and battery temperatures and the charging state.
zone() { for z in /sys/class/thermal/thermal_zone*; do [ "$(cat $z/type 2>/dev/null)" = "$1" ] && { echo $z/temp; return; }; done; echo /dev/null; }
ZC=$(zone cpuss-0-usr) ZB=$(zone cpuss-1-usr) ZG=$(zone gpuss-0-usr) ZS=$(zone sdm-skin-therm-usr) BAT=/sys/class/power_supply/battery
CF=/sys/devices/system/cpu/cpufreq
( prev=$(head -1 /proc/stat)
  while :; do
    sleep 2
    cur=$(head -1 /proc/stat)
    busy=$(echo "$prev
$cur" | awk '{i=$5+$6; t=0; for(n=2;n<=NF;n++)t+=$n; if(NR==1){pi=i;pt=t}else printf "%d", 100*((t-pt)-(i-pi))/(t-pt)}')
    prev=$cur
    t=" cpu=$(($(cat $ZC 2>/dev/null || echo 0)/1000)) gpu=$(($(cat $ZG 2>/dev/null || echo 0)/1000)) big_t=$(($(cat $ZB 2>/dev/null || echo 0)/1000))"
    rss=$(ps -eo rss,args | grep -i "[.]exe" | grep -v -i -E "services|winedevice|plugplay|svchost|explorer|rpcss|tabtip|conhost" | awk '{s+=$1} END{print int(s/1024)}')
    echo "$(date +%T) cpu ${busy}% big $(($(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq 2>/dev/null || echo 0)/1000))MHz gpu $(cat $K/gpu_busy_percentage | tr -d ' ') $(($(cat $K/gpuclk)/1000000))MHz$t mem_avail=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)MB game_rss=${rss}MB swap_used=$(awk '/SwapTotal/{t=$2} /SwapFree/{f=$2} END{print int((t-f)/1024)}' /proc/meminfo)MB max=$(($(cat $CF/policy0/scaling_max_freq)/1000))/$(($(cat $CF/policy6/scaling_max_freq)/1000))/$(($(cat $K/max_gpuclk)/1000000))MHz skin=$(($(cat $ZS 2>/dev/null || echo 0)/1000)) bat=$(($(cat $BAT/temp 2>/dev/null || echo 0)/10)) $(cat $BAT/status 2>/dev/null | tr 'A-Z ' 'a-z_')"
  done > $B.sensors ) &
S=$!
cd "$DIR"
WINEDEBUG=${WINEDEBUG_GAME:--all} wine "$EXE" "$@" >> $B.log 2>&1 &
G=$!
# CPU boost (stage 3.5): UTDECK_BOOST=N (0-1024) sets uclamp_min on the game's threads, so the
# frequency governor picks higher clocks from the CPU's own table; off unless set.
case "${UTDECK_BOOST:-0}" in
0|'') ;;
*) python3 "$W/utdeck-boost.py" "$G" "$UTDECK_BOOST" "$B.log" & BP=$! ;;
esac
wait $G
rc=$?
[ -n "${BP:-}" ] && wait $BP   # its summary line, within 2 s of the game's end
echo "exit $rc" >> $B.log
kill $S
# End Wine's background processes too: Lomiri considers the app running until they are gone.
wineserver -k
exit $rc
