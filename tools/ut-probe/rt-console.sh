#!/bin/sh
# UTDeck stage 3.1: the stage 2.1 console tests on one runtime, in a fresh prefix, for comparing
# our runtime with Hangover's. No screen needed.
#
#   rt-console.sh utdeck|hangover          -> rt-console-<runtime>-<time>.log in the wine folder
#
# 1. a new prefix (prefix-<runtime>-test): wineboot time and size;
# 2. wtest in every architecture (ARM64, ARM64EC, x86-64 through FEX, x86 through Box64 and FEX);
# 3. start-up time, cold (no wineserver) and warm, of `wtest --quick`;
# 4. 7-Zip's benchmark in one thread: ARM64 and x86-64 builds.
# The test prefix is deleted at the end.
set -u
export UTDECK_RUNTIME=$1
W=/home/phablet/utdeck-probe/wine
export WINEPREFIX=$W/prefix-$1-test
. $W/env.sh
T=$W/test
LOG=$W/rt-console-$1-$(date +%H%M%S).log
exec > "$LOG" 2>&1
ms() { date +%s%3N; }
echo "runtime: $1, $(command -v wine), $(wine --version)"
rm -rf "$WINEPREFIX"
s=$(ms); wineboot -i > /dev/null 2>&1; wineserver -w; e=$(ms)
echo "prefix: created in $(awk "BEGIN{printf \"%.1f\", ($e - $s) / 1000}") s, $(du -sh "$WINEPREFIX" | cut -f1)"
for a in arm64 arm64ec x86_64 i686; do
    echo "--- wtest-$a"
    wine "$T/wtest-$a.exe" 2>&1 | grep -v "^ *$"
done
echo "--- wtest-i686 with FEX"
HODLL=libwow64fex.dll wine "$T/wtest-i686.exe" --quick 2>&1 | grep -i -E "built|process|bench|int|float|copy"
echo "--- start-up (ms): cold, warm, warm"
for a in arm64 x86_64 i686; do
    wineserver -k 2>/dev/null; wineserver -w
    r="$a:"
    for i in 1 2 3; do s=$(ms); wine "$T/wtest-$a.exe" --quick > /dev/null 2>&1; r="$r $(( $(ms) - s ))"; done
    echo "$r"
done
echo "--- 7-Zip benchmark, one thread (total rating, MIPS)"
for a in arm64 x64; do
    printf "%s: " $a
    wine "$T/7z/$a/7za.exe" b -mmt1 2>&1 | grep "^Tot:" | awk '{print $NF}'
done
wineserver -k 2>/dev/null; wineserver -w
rm -rf "$WINEPREFIX"
echo "done $(date +%T)"
