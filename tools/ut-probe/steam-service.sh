#!/bin/sh
# UTDeck stage 3.S: Steam as a hidden background service ("hidden Steam", plan-change-steam-early.md).
#
#   steam-service.sh start      start Xvfb (:50) and the Steam client on it, with no window
#   steam-service.sh stop       ask Steam to shut down, then stop Xvfb
#   steam-service.sh status     processes, memory (PSS) and CPU of Steam
#   steam-service.sh launch ID  start the game with that app id, then close any Steam window that
#                               shows up on the hidden screen
#   steam-service.sh hide       close Steam's windows on the hidden screen
#
# Steam runs as a systemd user unit (utdeck-steam), not as a Lomiri app: Lomiri pauses (SIGSTOP)
# apps that are not in front, and games need the client awake for Steam's DRM. Its X11 display is
# an Xvfb of our own (a virtual screen in memory, unpacked from Ubuntu's xvfb package into
# ~/utdeck-probe/xvfb), so it does not depend on Lomiri's Xwayland, which Lomiri starts and ends
# with its X11 apps. Steam signs in by itself with its saved login token.
set -u
W=/home/phablet/utdeck-probe
X=$W/xvfb/root
D=${UTDECK_STEAM_XDISPLAY:-:50}
SIZE=${UTDECK_STEAM_XSIZE:-1200x540}
export XDG_RUNTIME_DIR=/run/user/$(id -u) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus

case "${1:-}" in
start)
    if ! pgrep -u "$(id -u)" -f "[X]vfb $D " > /dev/null; then
        systemd-run --user --unit=utdeck-xvfb --collect \
            --setenv=LD_LIBRARY_PATH="$X/usr/lib/aarch64-linux-gnu" \
            "$X/usr/bin/Xvfb" "$D" -screen 0 "${SIZE}x24" -nolisten tcp > /dev/null
        for i in 1 2 3 4 5 6 7 8 9 10; do [ -e "/tmp/.X11-unix/X${D#:}" ] && break; sleep 0.5; done
    fi
    systemd-run --user --unit=utdeck-steam --collect --setenv=DISPLAY="$D" \
        "$W/steam/steam-run.sh" -silent -cef-disable-breakpad > /dev/null
    echo "started: Xvfb $D ($SIZE), Steam hidden (unit utdeck-steam)"
    ;;
stop)
    cd "$HOME/.local/share/Steam/steamrtarm64" && DISPLAY=$D LD_LIBRARY_PATH=$PWD timeout 30 ./steam -shutdown > /dev/null 2>&1
    for i in $(seq 1 30); do pgrep -u "$(id -u)" -f '[s]teamrtarm64/' > /dev/null || break; sleep 1; done
    systemctl --user stop utdeck-steam utdeck-xvfb 2>/dev/null
    echo "stopped; Steam processes left: $(pgrep -c -u "$(id -u)" -f '[s]teamrtarm64/')"
    ;;
launch|hide)
    # Sending -applaunch to the running client also brings its main window up, as starting Steam a
    # second time does on a desktop: on the hidden screen nobody sees it, but its Store page plays
    # trailers, drawn in software (Mesa's llvmpipe): 200-280 % CPU while a game runs. The window is
    # closed as its close button would (WM_DELETE_WINDOW): Steam hides it and the web helper idles.
    if [ "$1" = launch ]; then
        [ -n "${2:-}" ] || { echo "usage: $0 launch APPID" >&2; exit 2; }
        cd "$HOME/.local/share/Steam/steamrtarm64" && DISPLAY=$D LD_LIBRARY_PATH=$PWD timeout 30 ./steam -silent -applaunch "$2" > /dev/null 2>&1
    fi
    for i in 1 2 3 4 5 6 7 8 9 10; do
        sleep 1
        python3 "$W/xwin.py" "$D" --close | grep "close requested" && break
    done
    ;;
status)
    for p in $(pgrep -u "$(id -u)" -f '[s]teamrtarm64/|[s]team-runtime|^\./steam'); do
        [ -r "/proc/$p/smaps_rollup" ] || continue
        echo "$(awk '/^Pss:/{print $2}' "/proc/$p/smaps_rollup") $(awk '{print $3}' "/proc/$p/stat") $(tr '\0' ' ' < "/proc/$p/cmdline" | cut -c1-60)"
    done | sort -rn | awk '{s+=$1; printf "%7d kB %s %s %s\n", $1, $2, $3, $4} END{printf "total PSS: %d MB in %d processes\n", s/1024, NR}'
    # CPU over 60 s, from the processes' own counters (utime + stime, in clock ticks)
    t() { for p in $(pgrep -u "$(id -u)" -f '[s]teamrtarm64/|[s]team-runtime|^\./steam'); do awk '{print $14+$15}' "/proc/$p/stat" 2>/dev/null; done | awk '{s+=$1} END{print s+0}'; }
    a=$(t); sleep 60; b=$(t)
    echo "CPU over 60 s: $(( (b - a) * 100 / $(getconf CLK_TCK) / 60 )) % of one core"
    ;;
*)
    echo "usage: $0 start|stop|status" >&2; exit 2 ;;
esac
