#!/bin/sh
# UTDeck stage 3.3: what a running game spends on wineserver round trips (on the phone, as phablet).
#
#   sh wineserver-cost.sh EXE_PATTERN [SECONDS]      e.g. sh wineserver-cost.sh ys9.exe 20
#
# Finds the game's process (the newest one whose command line matches EXE_PATTERN), its prefix
# (WINEPREFIX in its environment) and that prefix's wineserver, then measures twice, SECONDS each
# (default 20):
#   1. untouched: the game's frame rate (its .frames log, patch 0007), wineserver's CPU time and its
#      context switches per second (each request wakes the server once, so this approximates the
#      requests per second at no cost to the game);
#   2. with `strace -c -f` attached to wineserver: its system calls, counted (a request is a read and
#      a write on its pipe; waits that block are answered later), and the game's frame rate while the
#      server is slowed down by the tracing: the drop shows how much the game waits on the server.
# Reads only; strace detaches at the end. Kernel 5.4: waits go through wineserver (no fsync, no
# NTSYNC), which is what an esync port would change.
set -u
PAT=$1 SECS=${2:-20}
G=/home/phablet/utdeck-probe/wine/games
TCK=$(getconf CLK_TCK)
game=""   # the newest Wine process (its executable is Wine's loader) whose command line matches
for p in $(pgrep -u "$(id -u)" -f "$PAT"); do
    case "$(readlink /proc/$p/exe)" in */wine|*/wine-preloader|*/wine64*) game=$p ;; esac
done
[ -n "$game" ] || { echo "no running game matching $PAT" >&2; exit 1; }
prefix=$(tr '\0' '\n' < /proc/$game/environ | sed -n 's/^WINEPREFIX=//p')
server=""
for p in $(pgrep -u "$(id -u)" -x wineserver); do
    [ "$(tr '\0' '\n' < /proc/$p/environ | sed -n 's/^WINEPREFIX=//p')" = "$prefix" ] && server=$p
done
[ -n "$server" ] || { echo "no wineserver for $prefix" >&2; exit 1; }
frames=$(tr '\0' '\n' < /proc/$game/environ | sed -n 's/^UTDECK_FRAMESTATS=//p')   # set by game-run.sh
[ -f "$frames" ] || frames=/dev/null
echo "game pid $game, wineserver pid $server, prefix $prefix"
echo "frame log $frames"

cpu() { awk '{print $14 + $15}' /proc/$1/stat; }
csw() { awk '/^voluntary_ctxt_switches|^nonvoluntary_ctxt_switches/ {s += $2} END {print s}' /proc/$1/status; }
fps() {  # mean fps and frame time of the frame-log lines after line $1
    tail -n +$(($1 + 1)) "$frames" | awk '{for (i = 1; i <= NF; i++) {if ($i == "fps,") f += $(i-1); if ($i == "mean") m += $(i+1)} n++}
        END {if (n) printf "%.1f fps, %.1f ms per frame (%d s)", f/n, m/n, n; else printf "no frames"}'
}
gcpu() {  # the game's busiest threads, % of one core, between two snapshots
    for t in $(ls /proc/$game/task); do echo "$t $(awk '{print $14 + $15}' /proc/$game/task/$t/stat 2>/dev/null) $(tr ' ' _ < /proc/$game/task/$t/comm 2>/dev/null)"; done | sort
}

# 1. untouched
n0=$(wc -l < "$frames"); c0=$(cpu $server); s0=$(csw $server); gcpu > /tmp/wsc-a
K=/sys/class/kgsl/kgsl-3d0 gb=0 gc=0 i=0
while [ $i -lt "$SECS" ]; do   # the GPU's load and clock, once a second
    gb=$((gb + $(tr -d ' %' < $K/gpu_busy_percentage))); gc=$((gc + $(cat $K/gpuclk) / 1000000)); i=$((i + 1)); sleep 1
done
n1=$(wc -l < "$frames"); c1=$(cpu $server); s1=$(csw $server); gcpu > /tmp/wsc-b
echo "untouched, $SECS s: $(fps $n0); GPU $((gb / SECS)) % at $((gc / SECS)) MHz"
echo "  wineserver: $(( (c1 - c0) * 100 / TCK / SECS )) % of one core, $(( (s1 - s0) / SECS )) context switches/s"
echo "  the game's busiest threads (% of one core):"
join /tmp/wsc-a /tmp/wsc-b | awk -v t=$TCK -v s=$SECS '{d = ($4 - $2) * 100 / t / s; if (d >= 5) printf "    %3d %% %s\n", d, $3}' | sort -rn | head -6

# 2. traced
n2=$(wc -l < "$frames")
timeout -s INT "$SECS" strace -c -f -p "$server" -o /tmp/wsc-strace.txt 2>/dev/null
n3=$(wc -l < "$frames")
echo "with wineserver traced, $SECS s: $(fps $n2)"
echo "  wineserver system calls (all, then the top ones):"
awk '$NF == "total" {printf "    total %s calls, %s per second\n", $4, int($4 / '"$SECS"')}' /tmp/wsc-strace.txt
grep -E "^ *[0-9.]+ +[0-9.]+ +[0-9]+ +[0-9]+" /tmp/wsc-strace.txt | grep -v " total$" | sort -k4 -rn | head -6 | awk '{printf "    %-14s %8d calls\n", $NF, $4}'
rm -f /tmp/wsc-a /tmp/wsc-b
