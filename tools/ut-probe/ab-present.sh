#!/bin/sh
# Stage 1.4 step 4: vkcube on route A (R11 through libhybris) and route B (turnip-utdeck), 600 frames.
P=/home/phablet/utdeck-probe
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
B="env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-utdeck/turnip_icd.json"
A="env GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so:$P/r11/hybris-barrier-hook.so HYBRIS_LD_LIBRARY_PATH=$P/r11/shim:/android/system/lib64:/android/vendor/lib64 DOLPHIN_UT_VK_DRIVER_DIR=$P/r11/driver"
for r in A B; do eval E=\$$r
  for m in 2 0; do  # vkcube --present_mode: 2 = FIFO, 0 = IMMEDIATE (falls back to what the WSI offers)
    printf "route %s, present mode %s: " $r $m
    $E python3 $P/run-timed.py --frames 600 -- timeout 90 $P/vk/bin/vkcube-wayland --c 600 --present_mode $m 2>&1 | grep -E "^exit|rror|not supported|Selected" | tr '\n' ' '; echo
  done
done
