#!/bin/sh
# UTDeck stage 1.4 step 2: frame pacing on screen, the vendor GLES driver against zink on our Turnip
# (which presents through the same android_wlegl path a Turnip Vulkan game uses).
#
#   cd /home/phablet/utdeck-probe && ./pacing.sh        (screen on and unlocked; do not touch it)
#
# Five runs of gltest's fullscreen window, 900 frames each, with stone-ut's fps-panel.py alongside
# (the panel's real refresh, frames the system compositor commits, GPU load and clock). Writes
# pacing-result.txt in the test folder.
P=/home/phablet/utdeck-probe
Z=$P/zink
OUT=$P/pacing-result.txt
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
: > $OUT
run() { # label driver(vendor|zink) gltest-args...
  label=$1; drv=$2; shift 2
  echo "===== $label" >> $OUT
  python3 $P/fps-panel.py 12 > $P/fps-$label.txt 2>&1 &
  fp=$!
  sleep 1
  if [ $drv = zink ]; then
    ( . $Z/env.sh; export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
      python3 $P/run-timed.py --frames 900 -- timeout 120 $Z/gltest wayland 900 "$@" ) >> $OUT 2>&1
  else
    ( export GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
      python3 $P/run-timed.py --frames 900 -- timeout 120 $Z/gltest wayland 900 "$@" ) >> $OUT 2>&1
  fi
  wait $fp
  echo "--- fps-panel (last lines)" >> $OUT
  tail -6 $P/fps-$label.txt >> $OUT
  sleep 3
}
run vendor-gles-i1 vendor --gles --interval 1
run vendor-gles-i0 vendor --gles --interval 0
run zink-gles-i1 zink --gles --interval 1
run zink-gles-i0 zink --gles --interval 0
run zink-gl-i1 zink --interval 1
echo done >> $OUT
echo "done: $OUT"
