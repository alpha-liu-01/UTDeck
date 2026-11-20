#!/bin/sh
# watch-game.sh APPID EXE_PATTERN MINUTES: after a launch, report every 30 s the frames drawn,
# heap-lock errors and the game's memory; stop when the game exits or a hang is seen.
A=$1 PAT=$2 MIN=$3
G=/home/phablet/utdeck-probe/wine/games
for i in $(seq 1 30); do pgrep -u phablet -f "$PAT" > /dev/null && break; sleep 2; done
B=$(ls -t $G/steam-$A-*.log | head -1); B=${B%.log}
echo "log $B"
prev=0; still=0
for i in $(seq 1 $((MIN * 2))); do
    sleep 30
    n=$(awk '{for(k=1;k<=NF;k++) if($k=="frames" && $(k+1)=="in") s+=$(k-1)} END{print s+0}' $B.frames 2>/dev/null); n=${n:-0}
    h=$(grep -c "RtlpWaitForCriticalSection" $B.log)
    rss=$(tail -1 $B.sensors 2>/dev/null | grep -o "game_rss=[0-9]*MB")
    alive=$(pgrep -u phablet -f "$PAT" > /dev/null && echo running || echo exited)
    echo "t=$((i * 30))s frames=$n (+$((n - prev))) lock_waits=$h $rss $alive"
    [ "$alive" = exited ] && break
    if [ $((n - prev)) -eq 0 ] && [ "$n" -gt 0 ]; then still=$((still + 1)); else still=0; fi
    [ $still -ge 4 ] && { echo "no frame for 2 minutes: hung"; break; }
    prev=$n
done
tail -3 $B.frames
