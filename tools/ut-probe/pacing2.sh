#!/bin/sh
# UTDeck stage 1.4 step 2, second run: when do Lomiri's frame callbacks arrive?
#
#   cd /home/phablet/utdeck-probe && ./pacing2.sh        (screen on and unlocked; do not touch it)
#
# Three paced (FIFO) clients with WAYLAND_DEBUG=client, so every request and event is time-stamped:
# the vendor GLES driver, zink GLES on our Turnip, and vkcube on our Turnip (Vulkan without zink).
# The logs (wl-*.log) are read on the PC: the time from each wl_surface.commit to the frame
# callback's "done", and to each buffer release.
P=/home/phablet/utdeck-probe
Z=$P/zink
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
( export GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
  WAYLAND_DEBUG=client timeout 60 $Z/gltest wayland 300 --gles --interval 1 > $P/wl-vendor.log 2>&1 )
sleep 3
( . $Z/env.sh; export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
  WAYLAND_DEBUG=client timeout 60 $Z/gltest wayland 300 --gles --interval 1 > $P/wl-zink.log 2>&1 )
sleep 3
( export LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-utdeck/turnip_icd.json
  WAYLAND_DEBUG=client timeout 60 $P/vk/bin/vkcube-wayland --c 300 > $P/wl-vkcube.log 2>&1 )
ls -la $P/wl-*.log
echo done
