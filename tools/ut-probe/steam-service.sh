#!/bin/sh
# UTDeck stage 3.S: Steam as a hidden background service ("hidden Steam", plan-change-steam-early.md).
#
#   steam-service.sh start      start Xvfb (:50) and the Steam client on it, with no window
#   steam-service.sh stop       ask Steam to shut down, then stop Xvfb
#   steam-service.sh status     processes, memory (PSS) and CPU of Steam
#   steam-service.sh launch ID  start the game with that app id, then close any Steam window that
#                               shows up on the hidden screen
#   steam-service.sh hide       close Steam's windows on the hidden screen
#   steam-service.sh play ID    what a game's app drawer entry runs (stage 3.8, steam-library.py):
#                               start hidden Steam if it is not running, wait until it has signed
#                               in, then launch the game; a phone notification while it waits
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
CONN=$HOME/.local/share/Steam/logs/connection_log.txt

# notify TEXT: a phone notification (Lomiri's notification service, the freedesktop interface).
# Lomiri accepts it but does not draw it yet on this phone (its notification QML fails, stage 3.8).
notify() {
    gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.Notify UTDeck 0 "" UTDeck "$1" "[]" "{}" 5000 > /dev/null 2>&1
}
# signed_in: Steam runs and the last state in its connection log is "Logged On". The log's lines
# also hold the account's number; only the state word is read.
signed_in() {
    pgrep -x -u "$(id -u)" steam > /dev/null || return 1
    [ "$(grep -o '^\[[^]]*\] \[[A-Za-z ]*,' "$CONN" 2>/dev/null | tail -1 | sed 's/.*\[//; s/,$//')" = "Logged On" ]
}

case "${1:-}" in
start)
    if ! pgrep -u "$(id -u)" -f "[X]vfb $D " > /dev/null; then
        systemd-run --user --unit=utdeck-xvfb --collect \
            --setenv=LD_LIBRARY_PATH="$X/usr/lib/aarch64-linux-gnu" \
            "$X/usr/bin/Xvfb" "$D" -screen 0 "${SIZE}x24" -nolisten tcp > /dev/null
        for i in 1 2 3 4 5 6 7 8 9 10; do [ -e "/tmp/.X11-unix/X${D#:}" ] && break; sleep 0.5; done
    fi
    # UTDECK_STEAM_EXTRA: more client options, for measuring them (stage 3.4, item 7).
    systemd-run --user --unit=utdeck-steam --collect --setenv=DISPLAY="$D" \
        "$W/steam/steam-run.sh" -silent -cef-disable-breakpad ${UTDECK_STEAM_EXTRA:-} > /dev/null
    echo "started: Xvfb $D ($SIZE), Steam hidden (unit utdeck-steam)"
    python3 "$W/steam/steam-library.py" > /dev/null 2>&1 || true   # the app drawer entries (3.8)
    ;;
play)
    [ -n "${2:-}" ] || { echo "usage: $0 play APPID" >&2; exit 2; }
    if ! signed_in; then
        notify "Starting Steam, the game follows in about a minute"
        # A fresh start must sign in anew: the log's last line is then still the previous session's.
        n0=0
        if ! pgrep -x -u "$(id -u)" steam > /dev/null; then
            n0=$(wc -l < "$CONN" 2>/dev/null || echo 0)
            "$0" start > /dev/null
        fi
        i=0; until signed_in && [ "$(wc -l < "$CONN")" -gt "$n0" ]; do
            i=$((i + 1)); [ $i -le 90 ] || { notify "Steam did not sign in, the game was not started"; exit 1; }
            sleep 2
        done
        sleep 3   # the client takes commands a moment after signing in
    fi
    "$0" launch "$2"
    ;;
stop)
    # Only to a running client: with none, "steam -shutdown" starts a new one, which without the
    # hidden screen shows "Unable to open a connection to X" in a box Lomiri cannot draw (an
    # endless spinner on the phone, stage 3.4).
    if pgrep -x -u "$(id -u)" steam > /dev/null; then
        cd "$HOME/.local/share/Steam/steamrtarm64" && DISPLAY=$D LD_LIBRARY_PATH=$PWD timeout 30 ./steam -shutdown > /dev/null 2>&1
    fi
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
    true   # no Steam window to close is not a failure (the game started all the same)
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
    echo "usage: $0 start|stop|status|launch ID|hide|play ID" >&2; exit 2 ;;
esac
