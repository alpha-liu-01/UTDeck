#!/bin/sh
# Particle Physics batch (200 steps, best of 3): isam (default) against ldib (UTDECK_IR3_NO_ISAM_SSBO=1).
P=/home/phablet/utdeck-probe; cd $P/vkcompute
T="env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-isam/turnip_icd.json MESA_SHADER_CACHE_DISABLE=true"
run() { L=$1; shift; "$@" ./spvrun $P/dlog-all/shader-9a1926a3d0ed015e.spv --spec 1=$L --spec 2=1 --spec 3=1 --spec 4=$((L*8)) --spec 5=$((L*4)) --groups 4096,1,1 --push-hex 0000484200004842b9fc073c001000008f53f43d --buf rand@0,50:8192 --buf zero:8192 --buf randf:8192 --buf zero:8192 --repeat 3 --batch 200 --hash 2>&1 | grep "^batch\|^hash b[13]" | sed 's/.* \([0-9.]*\) ms each.*/\1/' | sort | tr "\n" " "; }
for L in 64 128 256; do
  echo "local $L: isam: $(run $L $T)"
  echo "local $L: ldib: $(run $L $T UTDECK_IR3_NO_ISAM_SSBO=1)"
done
