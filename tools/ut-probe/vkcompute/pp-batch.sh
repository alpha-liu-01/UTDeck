#!/bin/sh
# Particle Physics as Geekbench submits it: 200 command buffers in one vkQueueSubmit, 3 times.
P=/home/phablet/utdeck-probe; cd $P/vkcompute
L=${1:-128}; S4=$((L*8)); S5=$((L*4))
A="$P/dlog-all/shader-9a1926a3d0ed015e.spv --spec 1=$L --spec 2=1 --spec 3=1 --spec 4=$S4 --spec 5=$S5 --groups 4096,1,1 --push-hex 0000484200004842b9fc073c001000008f53f43d --buf rand@0,50:8192 --buf zero:8192 --buf randf:8192 --buf zero:8192 --repeat 3 --batch 200 --profile"
echo "== local size $L"
for d in turnip-utdeck turnip-wlegl; do echo "-- $d"; env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/$d/turnip_icd.json ./spvrun $A 2>&1 | grep "^batch"; done
echo "-- vendor"; env GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so ./spvrun $A 2>&1 | grep "^batch"
