#!/bin/sh
# Stereo Matching's main shader (7cbee645), Geekbench's sizes: 450x375 images, 8 disparities per
# dispatch, window 11. Batches of 10 dispatches (best of 3), both drivers, outputs hashed.
P=/home/phablet/utdeck-probe; cd $P/vkcompute
T="env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-utdeck/turnip_icd.json"
V="env GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so"
run() { LX=$1; LY=$2; GX=$3; GY=$4; shift 4; "$@" ./spvrun $P/dlog-all/shader-7cbee645264be65f.spv --spec 1=$LX --spec 2=$LY --spec 3=1 --groups $GX,$GY,1 \
  --push 450,375,512,0,7,11 --img randu:512x384 --img randu:512x384 --img randu:512x384 --buf rand@0,1000:168752 --img zero:512x384 --buf zero:168752 \
  --repeat 3 --batch 10 --hash 2>&1 | grep "^batch\|^hash b[45]\|rror\|failed" | sed 's/.* \([0-9.]*\) ms each.*/\1/' | sort | tr "\n" " "; }
echo "32x2  turnip: $(run 32 2 15 188 $T)"
echo "32x2  vendor: $(run 32 2 15 188 $V)"
echo "32x64 turnip: $(run 32 64 15 6 $T)"
echo "32x32 turnip: $(run 32 32 15 12 $T)"
echo "32x32 vendor: $(run 32 32 15 12 $V)"
