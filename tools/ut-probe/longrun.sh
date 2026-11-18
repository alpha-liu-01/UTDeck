#!/bin/sh
# Stage 1.4 step 4: long run, one route for MIN minutes. Compute: Particle Physics batches of 100
# steps (about 0.8 s each) in a loop, outputs hashed each time; route B also runs vkcube (FIFO) for
# the whole time. Every 30 s: temperatures, GPU clock and busy. Writes longrun-<route>.txt.
#   longrun.sh A|B [MIN]
R=$1; MIN=${2:-10}
P=/home/phablet/utdeck-probe; cd $P/vkcompute; OUT=$P/longrun-$R.txt; K=/sys/class/kgsl/kgsl-3d0
export XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0
if [ $R = A ]; then E="env GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so:$P/r11/hybris-barrier-hook.so HYBRIS_LD_LIBRARY_PATH=$P/r11/shim:/android/system/lib64:/android/vendor/lib64 DOLPHIN_UT_VK_DRIVER_DIR=$P/r11/driver"
else E="env LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/turnip-utdeck/turnip_icd.json"; fi
PP="$P/dlog-all/shader-9a1926a3d0ed015e.spv --spec 1=128 --spec 2=1 --spec 3=1 --spec 4=1024 --spec 5=512 --groups 4096,1,1 --push-hex 0000484200004842b9fc073c001000008f53f43d --buf rand@0,50:8192 --buf zero:8192 --buf randf:8192 --buf zero:8192 --repeat 10 --batch 100 --hash"
t() { for z in /sys/class/thermal/thermal_zone*; do case $(cat $z/type) in cpuss-0-usr|gpuss-0-usr) printf "%s=%s " $(cat $z/type) $(cat $z/temp);; esac; done; }
{
echo "route $R, $MIN min, start $(date +%T) $(t)"
dmesg | grep -ic kgsl > /tmp/kgsl-before
end=$(( $(date +%s) + MIN * 60 ))
if [ $R = B ]; then $E timeout $((MIN * 60)) $P/vk/bin/vkcube-wayland > /tmp/longrun-vkcube.txt 2>&1 & vk=$!; fi
( while [ $(date +%s) -lt $end ]; do echo "sample $(date +%T) $(t) gpu=$(($(cat $K/gpuclk)/1000000))MHz busy=$(cat $K/gpu_busy_percentage)"; sleep 30; done ) &
mon=$!
n=0; bad=0; hashes=""
while [ $(date +%s) -lt $end ]; do
  o=$($E ./spvrun $PP 2>&1); rc=$?
  n=$((n + 1))
  h=$(echo "$o" | grep '^hash b[13]' | tr '\n' ' ')
  ms=$(echo "$o" | grep '^batch' | sed 's/.* \([0-9.]*\) ms each.*/\1/' | sort -n | sed -n '1p;$p' | tr '\n' ' ')
  [ -z "$hashes" ] && hashes="$h"
  if [ $rc -ne 0 ] || [ "$h" != "$hashes" ]; then bad=$((bad + 1)); echo "run $n FAILED rc=$rc: $(echo "$o" | tail -2 | tr '\n' ' ')"; fi
  [ $((n % 10)) -eq 0 ] && echo "run $n: ms per step min/max $ms"
done
wait $mon
if [ $R = B ]; then wait $vk; echo "vkcube exit $? (124 = ran the whole time): $(tail -2 /tmp/longrun-vkcube.txt | tr '\n' ' ')"; fi
echo "end $(date +%T) $(t): $n compute runs (1000 steps each), $bad failed or differed; reference $hashes"
echo "new KGSL kernel messages: $(( $(dmesg | grep -ic kgsl) - $(cat /tmp/kgsl-before) ))"
dmesg | grep -i kgsl | tail -3
} > $OUT 2>&1
