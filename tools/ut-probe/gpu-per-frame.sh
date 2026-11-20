#!/bin/sh
# UTDeck stage 3.3: the GPU time per frame of one test program on one runtime folder (on the phone).
#
#   sh gpu-per-frame.sh RUNTIME_DIR PREFIX [FRAMES]    e.g. sh gpu-per-frame.sh wine/runtime-proton wine/prefix-proton-gfx
#
# Runs d3d11test fullscreen (DXVK, MAILBOX as in games) for FRAMES frames (default 3000), samples the
# GPU's load and clock once a second for 8 seconds, from 2 s after the first frame, then prints the frame rate,
# the GPU load and the GPU time per frame (load / frame rate): two runtimes that draw the same picture
# should cost the same GPU time per frame; more means the runtime adds GPU work (a copy, a blit).
set -u
U=/home/phablet/utdeck-probe   # not W or P: env.sh sets those
RT=$1 PFX=$2 N=${3:-4000}
case "$RT" in /*) ;; *) RT=$U/$RT ;; esac
case "$PFX" in /*) ;; *) PFX=$U/$PFX ;; esac
. $U/wine/env.sh
export PATH=$RT/bin:$PATH WINEPREFIX=$PFX WAYLAND_DISPLAY=wayland-0 MESA_VK_WSI_PRESENT_MODE=mailbox \
       DXVK_LOG_LEVEL=none WINEDLLOVERRIDES="mscoree,mshtml=;d3d11,dxgi=n"   # keep env.sh's Mono/Gecko off: no install prompt on a prefix update
unset DISPLAY
cd $U/wine/test
wine d3d11test.exe --frames "$N" --fullscreen > /tmp/gpf.log 2>&1 &
i=0; until grep -q "frame 1 presented" /tmp/gpf.log 2>/dev/null || [ $i -ge 60 ]; do sleep 1; i=$((i + 1)); done
sleep 2   # past the first frames
K=/sys/class/kgsl/kgsl-3d0 b=0 c=0
L=$(pgrep -o -x lomiri); l0=$(awk '{print $14 + $15}' /proc/$L/stat)
for i in 1 2 3 4 5 6 7 8; do
    b=$((b + $(tr -d ' %' < $K/gpu_busy_percentage))); c=$((c + $(cat $K/gpuclk) / 1000000)); sleep 1
done
l1=$(awk '{print $14 + $15}' /proc/$L/stat)
wait
wineserver -k
fps=$(sed -n 's/.*frame time over [0-9]* frames: mean [0-9.]* ms (\([0-9.]*\) fps).*/\1/p' /tmp/gpf.log)
echo "$(basename "$RT"): $fps fps, GPU $((b / 8)) % at $((c / 8)) MHz, $(awk -v b=$((b / 8)) -v f="$fps" 'BEGIN {printf "%.1f", f ? b * 10 / f : 0}') ms of GPU per frame, Lomiri $(( (l1 - l0) * 100 / $(getconf CLK_TCK) / 8 )) % of one core"
