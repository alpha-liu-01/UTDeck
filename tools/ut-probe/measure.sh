#!/bin/sh
# measure.sh SECONDS: frame rate, frame time, GPU busy/clock, CPU of the newest game run, over SECONDS
B=$(ls -t /home/phablet/utdeck-probe/wine/games/steam-*.frames | head -1)
K=/sys/class/kgsl/kgsl-3d0
n0=$(wc -l < $B); g=0; c=0; i=0
while [ $i -lt $1 ]; do g=$((g + $(cat $K/gpu_busy_percentage | tr -d ' %'))); c=$((c + $(cat $K/gpuclk) / 1000000)); i=$((i + 1)); sleep 1; done
tail -n +$((n0 + 1)) $B | awk -v g=$((g / $1)) -v c=$((c / $1)) '{for(i=1;i<=NF;i++){if($i=="fps,")f+=$(i-1); if($i=="mean")m+=$(i+1); if($i=="max")x=($(i+1)>x?$(i+1):x)}; n++; s=$NF} END{printf "%d s: %.1f fps, frame time %.1f ms (worst %.0f ms), GPU %d%% at %d MHz, %s\n", n, f/n, m/n, x, g, c, s}'
tail -1 ${B%.frames}.sensors | cut -c1-110
