#!/bin/sh
# UTDeck stage 1.4 step 2, third run: the two fixes suggested by pacing2.sh, on zink (our Turnip).
#
#   cd /home/phablet/utdeck-probe && ./pacing3.sh        (screen on and unlocked; do not touch it)
#
# 1. --flush: the GPU work of a frame is sent before EGL waits for Lomiri's frame callback, so
#    rendering overlaps the wait (one run with WAYLAND_DEBUG for the message timing, one with
#    fps-panel.py).
# 2. Swap interval 0 (zink then presents in MAILBOX on Wayland) with an app-side frame limiter at
#    60, 90 and 120 fps, as a game with DXVK's limiter would run.
# Writes pacing3-result.txt and wl-zink-flush.log in the test folder.
P=/home/phablet/utdeck-probe
Z=$P/zink
OUT=$P/pacing3-result.txt
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
: > $OUT
( . $Z/env.sh; export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
  WAYLAND_DEBUG=client timeout 60 $Z/gltest wayland 300 --gles --interval 1 --flush > $P/wl-zink-flush.log 2>&1 )
sleep 3
run() { # label gltest-args...
  label=$1; shift
  echo "===== $label" >> $OUT
  python3 $P/fps-panel.py 9 > $P/fps-$label.txt 2>&1 &
  fp=$!
  sleep 1
  ( . $Z/env.sh; export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
    python3 $P/run-timed.py --frames 600 -- timeout 60 $Z/gltest wayland 600 "$@" ) >> $OUT 2>&1
  wait $fp
  echo "--- fps-panel" >> $OUT
  cat $P/fps-$label.txt >> $OUT
  sleep 3
}
run zink-i1-flush --gles --interval 1 --flush
run zink-i0-limit60 --gles --interval 0 --limit 60
run zink-i0-limit90 --gles --interval 0 --limit 90
run zink-i0-limit120 --gles --interval 0 --limit 120
echo done >> $OUT
echo "done: $OUT"
