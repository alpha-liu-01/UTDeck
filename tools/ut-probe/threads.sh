#!/bin/sh
# threads.sh SECONDS: the game's busiest threads over SECONDS (CPU % of one core) and the cores they
# ran on (sampled each second: little cpu0-5, big cpu6-7), plus the GPU's clock and busy share.
N=${1:-10}
P=$(ps -eo pid,args | grep "[Z]:.*\.exe" | grep -v -i -E "services|winedevice|plugplay|svchost|explorer|rpcss|steam.exe" | awk '{print $1}' | head -1)
[ -n "$P" ] || { echo "no game process"; exit 1; }
echo "game pid $P: $(tr '\0' ' ' < /proc/$P/cmdline | sed 's/.*\\\\//' | cut -c1-50)"
snap() { for t in /proc/$P/task/*; do echo "$(basename $t) $(awk '{print $14+$15, $39}' $t/stat) $(cat $t/comm)"; done; }
snap > /tmp/t0; : > /tmp/cores; g=0; c=0
for i in $(seq 1 $N); do sleep 1; snap >> /tmp/cores; g=$((g + $(tr -d ' %' < /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage))); c=$((c + $(cat /sys/class/kgsl/kgsl-3d0/gpuclk) / 1000000)); done
snap > /tmp/t1
echo "GPU: $((g / N)) % busy at $((c / N)) MHz (means); big cores now $(($(cat /sys/devices/system/cpu/cpu6/cpufreq/scaling_cur_freq)/1000)) MHz"
sort /tmp/t0 > /tmp/t0s; sort /tmp/t1 > /tmp/t1s; join /tmp/t0s /tmp/t1s | awk -v n=$N '{d=$5-$2; if(d>n*3) printf "%s %d %s\n", $1, d*100/(n*100), $4}' | sort -k2 -rn | head -6 | while read tid pct name; do
  big=$(awk -v t=$tid '$1==t && $3>=6' /tmp/cores | wc -l); all=$(awk -v t=$tid '$1==t' /tmp/cores | wc -l)
  echo "  thread $tid $name: $pct % of a core, on a big core in $big of $all samples"; done
