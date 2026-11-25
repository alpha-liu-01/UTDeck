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
#   LABEL-<time>.sensors  every 2 s: CPU load (overall and per cluster), clocks and their allowed
#                         maximums, GPU load and clock, temperatures, the battery, free memory, the
#                         game's resident memory (the sampler below)
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
# Before them, the user's own choices for all games (stage 5.1), from user.env, which the app's
# Settings page writes (defaults.env is replaced with each app update, user.env stays): only UTDeck's
# own names (UTDECK_*). So: the game's own setting, then the user's, then the defaults.
DEFAULTS= USERSET=
if [ -f $W/user.env ]; then
    while IFS= read -r line; do
        case "$line" in
        UTDECK_[A-Z_]*=*)
            n=${line%%=*}
            case "$n" in *[!A-Z0-9_]*) continue ;; esac   # a plain name only: it goes through eval
            if eval "[ -z \"\${$n+x}\" ]"; then export "$line"; USERSET="$USERSET $line"; fi ;;
        esac
    done < $W/user.env
fi
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
# entry stays a window of its own, as before. Nor for Steam's install scripts and own runs
# (UTDECK_SETUP_RUN, stage 4.6): their processes have no window, and joined to the app they kept
# Lomiri from closing it when the user swiped its card away (it waited for them, then killed the app
# minutes later, and the game then started as a window of its own).
if [ -z "${UTDECK_APP_ID+x}" ] && [ "${UTDECK_SETUP_RUN:-0}" != 1 ]; then
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
  [ -n "$USERSET" ] && echo "user settings:$USERSET"
  echo "app: ${UTDECK_APP_ID:-none (UTDeck not running)}"; } > $B.log
# The performance overlay (stage 5.1) and the frame rate limit (stage 5.2), both MangoHud's, from the
# game's settings or the user's (user.env):
#   UTDECK_OVERLAY = off, fps, frametime or full; UTDECK_OVERLAY_POSITION = top-left, top-center or
#   top-right; UTDECK_FPS_LIMIT = off, 28, 30, 40, 45, 48 or 60 (each divides a refresh rate phones'
#   panels offer: 144/5, 60/2, 120/3, 90/2, 144/3, 60).
# Either one on: the runtime's MangoHud layer (lib/gpu/layers, listed by env.sh) is enabled by name,
# with a settings file for this run next to its logs ($B.overlay.conf; it replaces one a game's own
# settings may name). legacy_layout=0 shows exactly the listed items, in that order. A limit without
# the overlay loads MangoHud hidden (no_display: about 0.06 ms a picture). The limit sleeps before the
# picture is handed over (fps_limit_method=early): 3-15 times evener than MangoHud's default (late) and
# DXVK's own limiter, at a lower CPU load (stage 5.2's measurements), and the same for every API
# (Direct3D through DXVK and VKD3D-Proton, OpenGL through zink). Both off (and Steam's install
# scripts): nothing is set, so nothing is loaded. Left out of the overlay on purpose: the CPU power
# line (a phone has no source for it) and the VRAM lines (a phone's GPU shares its memory). Left and
# right sit 100 pixels in from the side, clear of the screen's rounded corners: MangoHud adds
# offset_x in every position, so the right side takes a negative one (MangoHud reads it with strtol,
# and the sign survives its unsigned type: GCC converts modulo, checked on the phone), and keeps its
# own 10-pixel margin there (-90 is 100 in all).
overlay=${UTDECK_OVERLAY:-off} limit=${UTDECK_FPS_LIMIT:-off}
[ "${UTDECK_SETUP_RUN:-0}" = 1 ] && overlay=off limit=off
case "$overlay" in fps|frametime|full|off) ;; *) echo "overlay: unknown setting \"$overlay\", left off" >> $B.log; overlay=off ;; esac
case "$limit" in 28|30|40|45|48|60|off) ;; *) echo "frame rate limit: unknown setting \"$limit\", left off" >> $B.log; limit=off ;; esac
if [ "$overlay" != off ] || [ "$limit" != off ]; then
    if [ -f $W/runtime/lib/gpu/layers/MangoHud.json ]; then
        pos=${UTDECK_OVERLAY_POSITION:-top-left}
        case "$pos" in top-left) side=100 ;; top-right) side=-90 ;; top-center) side=0 ;; *) pos=top-left side=100 ;; esac
        {
            echo "# UTDeck: the performance overlay ($overlay, $pos) and the frame rate limit ($limit) for this run, written by game-run.sh"
            echo "legacy_layout=0"
            if [ "$overlay" = off ]; then
                echo "no_display=1"
            else
                echo "position=$pos"
                echo "offset_x=$side"
                echo "background_alpha=0.4"
            fi
            case "$overlay" in
            fps) printf '%s\n' fps fps_only ;;
            frametime) printf '%s\n' fps frametime frame_timing ;;
            full) printf '%s\n' fps frametime frame_timing cpu_stats cpu_temp cpu_mhz gpu_stats gpu_temp \
                      gpu_core_clock ram battery battery_watt throttling_status ;;
            esac
            if [ "$limit" != off ]; then
                echo "fps_limit=$limit"
                echo "fps_limit_method=early"
            fi
        } > $B.overlay.conf
        # The layer's name once, before any other layers already asked for.
        layers=VK_LAYER_MANGOHUD_overlay_aarch64
        for l in $(echo "${VK_LOADER_LAYERS_ENABLE:-}" | tr ',' ' '); do
            [ "$l" = VK_LAYER_MANGOHUD_overlay_aarch64 ] || layers="$layers,$l"
        done
        export MANGOHUD=1 MANGOHUD_CONFIGFILE=$B.overlay.conf VK_LOADER_LAYERS_ENABLE=$layers
        [ "$overlay" != off ] && echo "overlay: $overlay, $pos" >> $B.log
        [ "$limit" != off ] && echo "frame rate limit: $limit" >> $B.log
    else
        echo "overlay ($overlay) and frame rate limit ($limit) asked, but this runtime has no MangoHud (lib/gpu/layers)" >> $B.log
    fi
fi
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
# The sensor sampler (stage 3.2, made phone-independent in stage 5.1): one line every 2 s into
# $B.sensors. Nothing in it is written for one phone: the CPU clusters are the cpufreq policy* folders
# (stone: policy0 and policy6; the Nothing Phone (1): policy0, policy4 and policy7), the thermal zones
# are found by name once (their numbers are not fixed). Every reading has a fallback: a file that is
# missing or not readable (the Nothing Phone (1)'s policy0/scaling_max_freq) gives "?" for that one
# figure, where the old shell loop's arithmetic on the empty value ended the whole sampler (all 19 of
# that phone's files were empty). One awk process does it all, so a sample starts 3 small processes
# (sleep, date, ps) instead of about 25.
# A line: time; CPU load overall, then per cluster (cpus, load, clock/allowed maximum in MHz: the
# vendor's thermal service lowers the maximum when it steps in); GPU load and clock/maximum (KGSL);
# temperatures in °C: the hottest CPU zone, the hottest GPU zone, the skin, the board sensor the
# Nothing Phone (1)'s thermal profiles watch (quiet-therm) where there is one, the battery; the
# battery's current in mA as the kernel reports it; free memory, the game's resident memory (all
# its .exe processes but Wine's own), swap in use; the charging state.
CF=/sys/devices/system/cpu/cpufreq BAT=/sys/class/power_supply/battery
CL= ZC= ZG= ZS= ZQ=
for p in $CF/policy*; do [ -d "$p" ] && CL="$CL${p##*/}:$(cat $p/related_cpus 2>/dev/null);"; done
for z in /sys/class/thermal/thermal_zone*; do
    case "$(cat $z/type 2>/dev/null)" in
    cpu-*-usr|cpuss-*-usr) ZC="$ZC $z/temp" ;;
    gpuss-*-usr) ZG="$ZG $z/temp" ;;
    sdm-skin-therm-usr|msm-skin-therm-usr|skin-therm-usr) [ -z "$ZS" ] && ZS=$z/temp ;;
    quiet-therm-usr) ZQ=$z/temp ;;
    esac
done
echo "# sampler: clusters ${CL:-none}, $(echo $ZC | wc -w) CPU zones, $(echo $ZG | wc -w) GPU zones, skin ${ZS:-none}, board ${ZQ:-none}" > $B.sensors
awk -v CL="$CL" -v ZC="$ZC" -v ZG="$ZG" -v ZS="$ZS" -v ZQ="$ZQ" -v K=$K -v CF=$CF -v BAT=$BAT '
function rd(f,   l, r) { if (f == "") return ""; r = (getline l < f); close(f); return r > 0 ? l : "" }
function num(f,   v) { v = rd(f); gsub(/[^0-9-]/, "", v); return v }
function scaled(f, d,   v) { v = num(f); return v == "" ? "?" : int(v / d) }
function temp(list,   n, a, i, v, m) {
    n = split(list, a, " "); m = ""
    for (i = 1; i <= n; i++) {
        v = num(a[i]); if (v == "") continue
        v += 0; if (v >= 1000 || v <= -1000) v /= 1000
        if (v < -40 || v > 150) continue
        if (m == "" || v > m) m = v
    }
    return m == "" ? "?" : int(m + 0.5)
}
function readstat(   f, l, a, n, i, t) {
    f = "/proc/stat"
    while ((getline l < f) > 0) {
        n = split(l, a, " "); if (a[1] !~ /^cpu/) break
        t = 0; for (i = 2; i <= n; i++) t += a[i]
        T[a[1]] = t; I[a[1]] = a[5] + a[6]
    }
    close(f)
}
function load(keys,   n, k, i, dt, di) {
    n = split(keys, k, " "); dt = 0; di = 0
    for (i = 1; i <= n; i++) if ((k[i] in T) && (k[i] in PT)) { dt += T[k[i]] - PT[k[i]]; di += I[k[i]] - PI[k[i]] }
    return dt > 0 ? int(100 * (dt - di) / dt + 0.5) "%" : "?"
}
function rss(   c, l, a, s) {
    c = "ps -eo rss,args"; s = 0
    while ((c | getline l) > 0) {
        if (tolower(l) !~ /[.]exe/ || tolower(l) ~ /services|winedevice|plugplay|svchost|explorer|rpcss|tabtip|conhost/) continue
        split(l, a, " "); s += a[1]
    }
    close(c); return int(s / 1024)
}
function meminfo(   f, l, a) {
    f = "/proc/meminfo"; MA = "?"; ST = ""; SF = ""
    while ((getline l < f) > 0) {
        split(l, a, " ")
        if (a[1] == "MemAvailable:") MA = int(a[2] / 1024)
        else if (a[1] == "SwapTotal:") ST = a[2]
        else if (a[1] == "SwapFree:") SF = a[2]
    }
    close(f)
}
BEGIN {
    nc = split(CL, cl, ";")
    for (i = 1; i <= nc; i++) {
        if (cl[i] == "") continue
        split(cl[i], pc, ":"); m = split(pc[2], cpus, " "); if (m == 0) continue
        np++; PATH[np] = CF "/" pc[1]; LBL[np] = "c" cpus[1] (m > 1 ? "-" cpus[m] : ""); KEYS[np] = ""
        for (j = 1; j <= m; j++) KEYS[np] = KEYS[np] " cpu" cpus[j]
    }
    readstat(); for (k in T) { PT[k] = T[k]; PI[k] = I[k] }
    while (1) {
        system("sleep 2")
        d = "date +%T"; now = ""; d | getline now; close(d)
        readstat()
        line = now " cpu " load("cpu")
        for (i = 1; i <= np; i++)
            line = line " " LBL[i] " " load(KEYS[i]) " " scaled(PATH[i] "/scaling_cur_freq", 1000) "/" scaled(PATH[i] "/scaling_max_freq", 1000) "MHz"
        b = num(K "/gpu_busy_percentage")
        line = line " gpu " (b == "" ? "?" : b "%") " " scaled(K "/gpuclk", 1000000) "/" scaled(K "/max_gpuclk", 1000000) "MHz"
        line = line " cpu=" temp(ZC) " gpu=" temp(ZG) " skin=" temp(ZS) (ZQ == "" ? "" : " board=" temp(ZQ))
        bt = num(BAT "/temp"); line = line " bat=" (bt == "" ? "?" : int(bt / 10)) " bat_mA=" scaled(BAT "/current_now", 1000)
        meminfo()
        line = line " mem_avail=" MA "MB game_rss=" rss() "MB swap_used=" (ST == "" || SF == "" ? "?" : int((ST - SF) / 1024)) "MB"
        st = rd(BAT "/status"); gsub(/ /, "_", st); line = line " " (st == "" ? "?" : tolower(st))
        print line; fflush()
        for (k in T) { PT[k] = T[k]; PI[k] = I[k] }
    }
}' >> $B.sensors 2>/dev/null &
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
# Keep-awake (stage 4.7): UTDECK_KEEP_AWAKE=1 keeps the screen on while the game is in front (played
# with a controller, nobody touches the screen, and its timeout turned it off mid-game); not for an
# install script. Released when the game ends, or as soon as this helper ends for any reason.
[ "${UTDECK_KEEP_AWAKE:-0}" = 1 ] && [ "${UTDECK_SETUP_RUN:-0}" != 1 ] && { python3 -I "$W/utdeck-awake.py" game $$ "$B.log" & AP=$!; }
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
[ -n "${AP:-}" ] && kill $AP 2>/dev/null   # the screen's own timeout applies again
[ -n "${BP:-}" ] && wait $BP   # its summary line, within 2 s of the game's end
[ -n "${PP:-}" ] && wait $PP
echo "exit $rc" >> $B.log
kill $S
# End Wine's background processes too: Lomiri considers the app running until they are gone.
wineserver -k
exit $rc
