#!/bin/sh
# UTDeck stage 2.4: 3DMark 11 (Advanced Edition) as a Lomiri app, from utdeck-3dmark11.desktop.
# Wine (Hangover 11.16 + our winewayland), DXVK 2.7.1 (ARM64EC), Wine Mono for its .NET GUI,
# MAILBOX forced, landscape monitor. Logs: /home/phablet/utdeck-probe/wine/m6-<time>.log (Wine and
# DXVK) and m6-<time>-sensors.txt (GPU load and clock, temperatures, every 2 s).
W=/home/phablet/utdeck-probe/wine
. $W/env.sh
T=$(date +%H%M%S)
L=$W/m6-$T.log
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} MESA_VK_WSI_PRESENT_MODE=mailbox UTDECK_WAYLAND_LANDSCAPE=1
export WINEDLLOVERRIDES="mshtml=;d3d11,dxgi,d3d10core,d3d9,d3d8=n" DXVK_LOG_LEVEL=info
unset DISPLAY   # Lomiri gives legacy apps Xwayland; Wine must use its Wayland driver
K=/sys/class/kgsl/kgsl-3d0
( while :; do
    t=""; for z in /sys/class/thermal/thermal_zone*; do case $(cat $z/type) in cpuss-0-usr|gpuss-0-usr) t="$t $(cat $z/type)=$(($(cat $z/temp)/1000))";; esac; done
    echo "$(date +%T) gpu $(cat $K/gpu_busy_percentage) $(($(cat $K/gpuclk)/1000000))MHz$t mem_avail=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)MB"
    sleep 2
  done > $W/m6-$T-sensors.txt ) &
S=$!
cd "$W/prefix/drive_c/Program Files/Futuremark/3DMark11/bin/x64"
WINEDEBUG=err+all,fixme-all wine 3DMark11.exe > $L 2>&1
echo "exit $?" >> $L
kill $S
wineserver -k
