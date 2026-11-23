#!/bin/sh
# UTDeck stage 3.2: start a Windows game on our runtime, with measurements.
#
#   game-run.sh LABEL GAME_DIR EXE [ARGS...]      (EXE relative to GAME_DIR, or a full path)
#
# Environment (optional): GAME_OVERRIDES (added to the DLL overrides), GAME_ENV (more variables,
# "A=1 B=2"), WINEPREFIX (default: the games prefix, wine/prefix-games).
# Started from a Lomiri desktop file (landscape) or from a shell. Writes into wine/games/:
#   LABEL-<time>.log      Wine's errors and the game's output
#   LABEL-<time>.frames   one line per second from win32u (patch 0007): fps, frame times, size
#   LABEL-<time>.sensors  every 2 s: CPU busy, big-core clock, GPU busy and clock, CPU and GPU
#                         temperature, free memory, the game's resident memory
set -u
LABEL=$1 DIR=$2 EXE=$3; shift 3
W=/home/phablet/utdeck-probe/wine
export WINEPREFIX=${WINEPREFIX:-$W/prefix-games}
. $W/env.sh
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
# DXVK for Direct3D 8-11 (its builds are in the prefix, prefix-dlls.sh); VKD3D-Proton for 12.
# mscoree (.NET's loader) stays on: many games import it without running .NET (Goat Simulator,
# UE3), and with it off such a game cannot load at all (c0000135). Games that run .NET code also
# need Wine Mono in the prefix (the runtime ships it; 3.4).
# (XInput: Wine's own copies are put back into Steam prefixes by utdeck-compat.)
for v in ${GAME_ENV:-}; do export "$v"; done
# The defaults every game gets (stage 3.4), from one file: defaults.env, one NAME=value per line,
# each with the measurement that decided it. A value already set (the game's utdeck-env, GAME_ENV,
# the caller) wins. Among them: MAILBOX presentation, the landscape monitor, fill mode (UTDECK_FILL=0
# turns it off for a game), the game's Wine log level.
DEFAULTS=
if [ -f $W/defaults.env ]; then
    while IFS= read -r line; do
        case "$line" in
        ''|'#'*) continue ;;
        [A-Za-z_]*=*)
            n=${line%%=*}
            if eval "[ -z \"\${$n+x}\" ]"; then export "$line"; DEFAULTS="$DEFAULTS $line"; fi ;;
        esac
    done < $W/defaults.env
fi
# UTDeck's app (stage 4.1): while it runs, the game's windows carry its app id (Wine patch 0016) so
# that Lomiri shows them as part of the app (its orientation, one card, its icon), and the game's
# processes join the app (DESKTOP_FILE_HINT) so that Lomiri keeps the app while its library window
# is hidden. The id comes from the running app's own APP_ID (package_app_version; the short form
# package_app is enough). Without the app running nothing is set: a game started from its app drawer
# entry stays a window of its own, as before.
if [ -z "${UTDECK_APP_ID+x}" ]; then
    for p in $(pgrep -x utdeck); do
        id=$(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | sed -n 's/^APP_ID=\([^_]*_[^_]*\).*/\1/p')
        if [ -n "$id" ]; then
            export UTDECK_APP_ID=$id DESKTOP_FILE_HINT=${DESKTOP_FILE_HINT:-$id}
            break
        fi
    done
fi
# DXVK and VKD3D-Proton as native DLLs (not a choice: our runtime's Direct3D), then the defaults'
# DLL choices (UTDECK_DLL_DEFAULTS: Gecko and winemenubuilder off), then the game's own.
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core,d3d9,d3d8=n;d3d12,d3d12core=n${UTDECK_DLL_DEFAULTS:+;$UTDECK_DLL_DEFAULTS}${GAME_OVERRIDES:+;$GAME_OVERRIDES}"
unset DISPLAY   # Lomiri gives legacy apps Xwayland; Wine must use its Wayland driver
mkdir -p $W/games
B=$W/games/$LABEL-$(date +%H%M%S)
export UTDECK_FRAMESTATS=$B.frames
# Wine (patch 0015) writes the window's keyboard focus here, 1 or 0: "in front or not" for the
# pause helper (stage 3.6).
export UTDECK_FOCUS_FILE=$B.focus
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    wineboot -i > /dev/null 2>&1; wineserver -w; $W/prefix-dlls.sh > /dev/null
fi
{ printf 'game: %s, in %s: %s %s\n' "$LABEL" "$DIR" "$EXE" "$*"; echo "runtime: $(command -v wine), prefix $WINEPREFIX";
  echo "overrides: $WINEDLLOVERRIDES"; echo "extra: ${GAME_ENV:-}"; echo "defaults:$DEFAULTS";
  echo "app: ${UTDECK_APP_ID:-none (UTDeck not running)}"; } > $B.log
# Controllers (stage 4.4). controllers.reg: Wine takes PlayStation and Nintendo pads through SDL, not
# only through their raw nodes, which phablet cannot open (without it such a pad reaches no game);
# imported into this prefix again whenever the file changes. padlayout.py: for a pad whose kernel
# driver SDL's own layout does not fit (stone's hid-nintendo: the D-pad and every button after
# Capture), a layout by position, given to SDL (Wine's winebus) in SDL_GAMECONTROLLERCONFIG. Only
# pads connected now get it; one connected later keeps SDL's layout.
if [ -f $W/controllers.reg ]; then
    sum=$(sha256sum $W/controllers.reg | cut -c1-16)
    if [ "$(cat "$WINEPREFIX/utdeck-controllers-reg" 2>/dev/null)" != "$sum" ]; then
        wine regedit /S "$(winepath -w $W/controllers.reg 2>/dev/null)" > /dev/null 2>&1 && wineserver -w &&
            echo "$sum" > "$WINEPREFIX/utdeck-controllers-reg" && echo "controllers: registry settings imported" >> $B.log
    fi
fi
L=$(python3 $W/padlayout.py 2>> $B.log)
if [ -n "$L" ]; then
    export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:+$SDL_GAMECONTROLLERCONFIG
}$L"
fi
# GStreamer's plugin list (env.sh: GST_REGISTRY_1_0) is brought up to date here, outside Wine. Wine
# starts GStreamer with --gst-disable-registry-fork, so a Wine process loads every new or changed
# plugin into itself to list it; the phone's hybris plugin (Android's hardware decoders) cannot load
# without the session's GLIBC_TUNABLES and then ends the whole process: the game dies at its first
# video. Here GStreamer uses its helper process, which takes such failures and marks the plugin
# as unusable; Wine then only reads the list. About 3 s after a system update, 0.1 s otherwise.
python3 -c 'import ctypes; ctypes.CDLL("libgstreamer-1.0.so.0").gst_init(None, None)' >> $B.log 2>&1 \
    || echo "gstreamer: plugin list not updated" >> $B.log
K=/sys/class/kgsl/kgsl-3d0
# Sensor paths, found once by name (zone numbers are not fixed): the little and the big cluster,
# the GPU, the phone's skin. Stage 3.5 adds the allowed maximum clocks (the vendor's thermal service, mi_thermald,
# lowers them when it steps in), the skin and battery temperatures and the charging state.
zone() { for z in /sys/class/thermal/thermal_zone*; do [ "$(cat $z/type 2>/dev/null)" = "$1" ] && { echo $z/temp; return; }; done; echo /dev/null; }
ZC=$(zone cpuss-0-usr) ZB=$(zone cpuss-1-usr) ZG=$(zone gpuss-0-usr) ZS=$(zone sdm-skin-therm-usr) BAT=/sys/class/power_supply/battery
CF=/sys/devices/system/cpu/cpufreq
( prev=$(head -1 /proc/stat)
  while :; do
    sleep 2
    cur=$(head -1 /proc/stat)
    busy=$(echo "$prev
$cur" | awk '{i=$5+$6; t=0; for(n=2;n<=NF;n++)t+=$n; if(NR==1){pi=i;pt=t}else printf "%d", 100*((t-pt)-(i-pi))/(t-pt)}')
    prev=$cur
    t=" cpu=$(($(cat $ZC 2>/dev/null || echo 0)/1000)) gpu=$(($(cat $ZG 2>/dev/null || echo 0)/1000)) big_t=$(($(cat $ZB 2>/dev/null || echo 0)/1000))"
    rss=$(ps -eo rss,args | grep -i "[.]exe" | grep -v -i -E "services|winedevice|plugplay|svchost|explorer|rpcss|tabtip|conhost" | awk '{s+=$1} END{print int(s/1024)}')
    echo "$(date +%T) cpu ${busy}% big $(($(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq 2>/dev/null || echo 0)/1000))MHz gpu $(cat $K/gpu_busy_percentage | tr -d ' ') $(($(cat $K/gpuclk)/1000000))MHz$t mem_avail=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)MB game_rss=${rss}MB swap_used=$(awk '/SwapTotal/{t=$2} /SwapFree/{f=$2} END{print int((t-f)/1024)}' /proc/meminfo)MB max=$(($(cat $CF/policy0/scaling_max_freq)/1000))/$(($(cat $CF/policy6/scaling_max_freq)/1000))/$(($(cat $K/max_gpuclk)/1000000))MHz skin=$(($(cat $ZS 2>/dev/null || echo 0)/1000)) bat=$(($(cat $BAT/temp 2>/dev/null || echo 0)/10)) $(cat $BAT/status 2>/dev/null | tr 'A-Z ' 'a-z_')"
  done > $B.sensors ) &
S=$!
cd "$DIR"
WINEDEBUG=${WINEDEBUG_GAME:--all} wine "$EXE" "$@" >> $B.log 2>&1 &
G=$!
# For UTDeck's app (stage 4.1): which game runs, this script's pid and its focus file, in
# $XDG_RUNTIME_DIR/utdeck/game. The app hides its library window while the game's window is in
# front (one card in Lomiri's app switcher, a fullscreen game) and shows it again the moment the
# file goes, right after the game's own process ends: before Wine's leftovers are stopped below,
# so that Lomiri does not close the app for having no window.
STATE=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/utdeck
# An install script (UTDECK_SETUP_RUN, set by utdeck-compat) is not the game: no file for it.
[ "${UTDECK_SETUP_RUN:-0}" = 1 ] && STATE=/nonexistent
[ "$STATE" != /nonexistent ] && mkdir -p "$STATE" && printf '%s %s %s\n' "$LABEL" "$$" "$UTDECK_FOCUS_FILE" > "$STATE/game.tmp" \
    && mv -f "$STATE/game.tmp" "$STATE/game"
# Stopped by a signal (stage 4.2: Steam's d3ddriverquery64.exe run, label steam-0, left its file
# behind), the file goes too; the app also ignores a file whose pid is not a running game-run.sh.
trap 'read -r _ pid _ < "$STATE/game" 2>/dev/null && [ "$pid" = $$ ] && rm -f "$STATE/game"; exit 143' HUP INT TERM
# CPU boost (stage 3.5): UTDECK_BOOST=N (0-1024) sets uclamp_min on the game's threads, so the
# frequency governor picks higher clocks from the CPU's own table; off unless set.
case "${UTDECK_BOOST:-0}" in
0|'') ;;
*) python3 "$W/utdeck-boost.py" "$G" "$UTDECK_BOOST" "$B.log" & BP=$! ;;
esac
# Pause (stage 3.6): UTDECK_PAUSE=1 freezes the game's Windows processes while Lomiri's lock screen
# is up, the screen is off or another app is in front, and wakes them on return; off unless set.
[ "${UTDECK_PAUSE:-0}" = 1 ] && { python3 "$W/utdeck-pause.py" "$G" "$B.log" & PP=$!; }
# Install scripts (stage 4.3): a Visual C++ installer that logged its exit but never ends is ended
# (utdeck-setupwatch.py), so the install script and the game's first launch go on.
[ "${UTDECK_SETUP_RUN:-0}" = 1 ] && python3 "$W/utdeck-setupwatch.py" "$G" "$B.log" &
wait $G
rc=$?
# A program that restarts itself (stage 4.5: wiliwili after its language was changed) ends its first
# process while a new one runs on in the same prefix: the game is followed until no Windows program of
# this prefix runs any more. Wine's own background programs, Steam's launcher and crash reporters do not
# count. A Windows program's name is looked for in its process name and the first two words of its
# command line, whichever ends in .exe: the kernel cuts process names to 15 characters (3DMark's
# "3DMark_11_v103_installer.exe" is "3DMark_11_v103_", its command line names it), while a 64-bit
# program's command line can start with Wine's loader (wiliwili's process name names it).
# Only programs started at most 15 s before the first process ended, or later, are the game going on:
# a restart starts its new process as the old one ends, while a service or helper started long before,
# as the game ran (3DMark 11's SystemInfo service FMSISvc.exe and its hardware scan fmsiscan.exe, stage
# 4.5), is not the game. Their parent cannot tell: Wine starts Windows programs detached (systemd).
# And a game the user swiped away (Wine patch 0019's .closed file) is not followed at all: whatever of
# its prefix is left is ended, as the starting page's "End the game" does.
TICKS=$(getconf CLK_TCK)
ended_at=$(( $(cut -d. -f1 /proc/uptime) - 15 ))
prefix_program() {
    for p in /proc/[0-9]*; do
        c=
        for n in "$(cat "$p/comm" 2>/dev/null)" $({ tr '\0' '\n' < "$p/cmdline" | head -2 | tr ' ' '?'; } 2>/dev/null); do
            n=${n##*\\}; n=${n##*/}
            case "$n" in *.exe|*.EXE) c=$n; break ;; esac
        done
        [ -n "$c" ] || continue
        case "$c" in
        services.exe|winedevice.exe|plugplay.exe|svchost.exe|explorer.exe|rpcss.exe|tabtip.exe|conhost.exe) continue ;;
        start.exe|wineboot.exe|winemenubuilder.exe|rundll32.exe|steam.exe|UnityCrashHandler64.exe|UnityCrashHandler32.exe|iscriptevaluator.exe) continue ;;
        *.exe|*.EXE) ;;
        *) continue ;;
        esac
        # Its start, in s since boot: field 22 of stat, counted after the name in parentheses (a name
        # may hold spaces).
        start=$({ sed 's/.*) //' "$p/stat" | awk -v t="$TICKS" '{print int($20 / t)}'; } 2>/dev/null)
        [ -n "$start" ] && [ "$start" -ge "$ended_at" ] || continue
        { tr '\0' '\n' < "$p/environ"; } 2>/dev/null | grep -qxF "WINEPREFIX=$WINEPREFIX" && { echo "${p#/proc/} $c"; return; }
    done
}
followed=
if [ -f "$UTDECK_FOCUS_FILE.closed" ]; then
    echo "the first process ended ($rc) after the game was closed from outside: the rest of its prefix is ended" >> $B.log
    wineserver -k 2>/dev/null
fi
while next=$(prefix_program); [ -n "$next" ]; do
    [ -z "$followed" ] && echo "the first process ended ($rc), followed on: ${next#* } (pid ${next%% *})" >> $B.log
    followed=1
    sleep 2
done
# The game's process has ended: the app's library comes back (only if the file is still this run's).
read -r _ pid _ < "$STATE/game" 2>/dev/null && [ "$pid" = $$ ] && rm -f "$STATE/game"
[ -n "${BP:-}" ] && wait $BP   # its summary line, within 2 s of the game's end
[ -n "${PP:-}" ] && wait $PP
echo "exit $rc" >> $B.log
kill $S
# End Wine's background processes too: Lomiri considers the app running until they are gone.
wineserver -k
exit $rc
