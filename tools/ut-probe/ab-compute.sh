#!/bin/sh
# Stage 1.4 step 4: route A (R11 through libhybris) against route B (turnip-utdeck), compute.
P=/home/phablet/utdeck-probe; cd $P/vkcompute
B="env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-utdeck/turnip_icd.json"
A="env GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so:$P/r11/hybris-barrier-hook.so HYBRIS_LD_LIBRARY_PATH=$P/r11/shim:/android/system/lib64:/android/vendor/lib64 DOLPHIN_UT_VK_DRIVER_DIR=$P/r11/driver"
PP="$P/dlog-all/shader-9a1926a3d0ed015e.spv --spec 1=128 --spec 2=1 --spec 3=1 --spec 4=1024 --spec 5=512 --groups 4096,1,1 --push-hex 0000484200004842b9fc073c001000008f53f43d --buf rand@0,50:8192 --buf zero:8192 --buf randf:8192 --buf zero:8192 --repeat 3 --batch 100 --hash"
SM="$P/dlog-all/shader-7cbee645264be65f.spv --spec 1=32 --spec 2=2 --spec 3=1 --groups 15,188,1 --push 450,375,512,0,7,11 --img randu:512x384 --img randu:512x384 --img randu:512x384 --buf rand@0,1000:168752 --img zero:512x384 --buf zero:168752 --repeat 3 --batch 10 --hash"
for r in A B; do
  eval E=\$$r
  echo "===== route $r"
  echo "-- intops: $($E ./intops 2>&1 | grep -v hooked | tail -2 | tr '\n' ' ')"
  echo "-- particle (ms per step, batch 100): $($E ./spvrun $PP 2>&1 | grep '^batch\|^hash b[13]' | sed 's/.* \([0-9.]*\) ms each.*/\1/' | tr '\n' ' ')"
  echo "-- stereo (ms per dispatch, batch 10): $($E ./spvrun $SM 2>&1 | grep '^batch\|^hash b[45]' | sed 's/.* \([0-9.]*\) ms each.*/\1/' | tr '\n' ' ')"
  echo "-- kgsl: $($E ./kgsl-trace -o /tmp/kt-$r.txt -- ./spvrun $PP 2>&1 >/dev/null | tail -0; grep -m1 -o 'flags 0x[0-9a-f]* ([^)]*)' /tmp/kt-$r.txt; grep -c GPU_COMMAND /tmp/kt-$r.txt) GPU_COMMANDs for 300 command buffers"
done
dmesg | grep -i kgsl | tail -3
