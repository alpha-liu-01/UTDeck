#!/bin/sh
# UTDeck stage 1.2 step 3: does a long Turnip job make other GPU work wait?
#
#   preempt-test.sh <label> [VAR=value ...]
#
# A long job: Geekbench's horizontal blur shader on Turnip (build $ICD, default turnip-prio),
# 4000x3000 with a diameter of 127 at local size 32x1, 4 dispatches of about 0.9 s each (each below
# KGSL's 2 s limit). VAR=value arguments go to the long job only (e.g. UTDECK_TU_KGSL_PRIORITY=12).
# A probe, standing in for the compositor: a tiny dispatch through the vendor driver every 50 ms,
# each timed. If the probe's dispatches take about 0.9 s, the long job blocks other GPU work.
# Run in the phone's test folder (vkcompute/ and spirv-dump/ next to it).
label=$1; shift
P=/home/phablet/utdeck-probe
cd $P/vkcompute
blur=$(ls $P/spirv-dump/0xf1157a0883*.comp)
tiny=$(ls $P/spirv-dump/0x2bb37fe79e*.comp)
(
  export LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/${ICD:-turnip-prio}/turnip_icd.json
  for kv in "$@"; do export "$kv"; done
  ./spvrun $blur --spec 1=32 --spec 2=1 --spec 3=1 --push 4000,3000,4000,127 --buf randu:12000000 \
      --buf zero:12000000 --buf gauss:127 --groups 125,3000,1 --repeat 4 > long-$label.txt 2>&1
) &
long=$!
sleep 1
GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so \
  ./spvrun $tiny --spec 1=64 --spec 2=1 --spec 3=1 --push 1000,1 --buf randf:4096 --buf zero:4096 \
      --groups 8,1,1 --repeat 70 --interval 50 > probe-$label.txt 2>&1
wait $long
echo "== $label: long job (Turnip $*)"
grep -E "dispatch|best|failed" long-$label.txt
echo "== $label: probe (vendor driver, tiny dispatch every 50 ms)"
grep -E "best|failed" probe-$label.txt
awk '/^dispatch/ { t = $(NF-1); n++; if (t > 100) slow++; if (t > 500) vslow++ }
     END { printf "probe dispatches: %d, over 100 ms: %d, over 500 ms: %d\n", n, slow, vslow }' probe-$label.txt
dmesg | grep -i "kgsl.*spvrun" | tail -2
