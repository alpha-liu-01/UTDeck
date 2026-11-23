#!/bin/sh
# UTDeck stage 3.S: Steam as a hidden background service ("hidden Steam", plan-change-steam-early.md).
#
#   steam-service.sh start      start Xvfb (:50) and the Steam client on it, with no window
#   steam-service.sh stop       ask Steam to shut down, then stop Xvfb
#   steam-service.sh xvfb       start only Xvfb (:50), for the client's first update (stage 4.2)
#   steam-service.sh status     processes, memory (PSS) and CPU of Steam
#   steam-service.sh launch ID  start the game with that app id, then close any Steam window that
#                               shows up on the hidden screen
#   steam-service.sh hide       close Steam's windows on the hidden screen
#   steam-service.sh play ID    what a game's app drawer entry runs (stage 3.8, steam-library.py):
#                               start hidden Steam if it is not running, wait until it has signed
#                               in, then launch the game; a phone notification while it waits
#   steam-service.sh ready      the same without the game (stage 4.7: before installing one): hidden
#                               Steam running and signed in, or exit 1 after about 3 minutes
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
# 1280x800: Steam's main window fits whole (stage 4.3: a publisher's agreement is drawn inside it, and
# at 1200x540 its "I agree" button lay below the screen's edge).
SIZE=${UTDECK_STEAM_XSIZE:-1280x800}
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
state() { grep -o '^\[[^]]*\] \[[A-Za-z ]*,' "$CONN" 2>/dev/null | tail -1 | sed 's/.*\[//; s/,$//'; }
signed_in() {
    pgrep -x -u "$(id -u)" steam > /dev/null || return 1
    [ "$(state)" = "Logged On" ]
}

# xvfb: the hidden screen, unless it runs already.
xvfb() {
    if ! pgrep -u "$(id -u)" -f "[X]vfb $D " > /dev/null; then
        systemd-run --user --unit=utdeck-xvfb --collect \
            --setenv=LD_LIBRARY_PATH="$X/usr/lib/aarch64-linux-gnu" \
            "$X/usr/bin/Xvfb" "$D" -screen 0 "${SIZE}x24" -nolisten tcp > /dev/null
        for i in 1 2 3 4 5 6 7 8 9 10; do [ -e "/tmp/.X11-unix/X${D#:}" ] && break; sleep 0.5; done
    fi
}

case "${1:-}" in
xvfb)
    # Only the hidden screen (stage 4.2: the client's first update runs on it, with no window).
    xvfb
    ;;
start)
    # Stage 4.7: Steam's side brought up to UTDeck's tool first, while Steam is stopped (the tool's
    # new name, each game's shared user folder; steam-tools.sh, quick when nothing is left to do).
    pgrep -x -u "$(id -u)" steam > /dev/null || sh "$W/steam/steam-tools.sh" >> "$W/steam/steam-tools.log" 2>&1
    xvfb
    # UTDECK_STEAM_EXTRA: more client options, for measuring them (stage 3.4, item 7).
    # -noshaders (stage 4.7, "Disable the shader manager" in Steam's client): Steam's shader
    # pre-caching compiles the shader collections it downloads with SwiftShader (its CPU Vulkan
    # driver, the only one it finds on the phone), which our games (Turnip) cannot use; it cost CPU
    # and battery, held first starts on "Processing Vulkan shaders", and downloaded re-encoded game
    # videos no game here reads (760 MB for ATRI). The games' own Turnip cache stays (utdeck-compat).
    systemd-run --user --unit=utdeck-steam --collect --setenv=DISPLAY="$D" \
        "$W/steam/steam-run.sh" -silent -cef-disable-breakpad -noshaders ${UTDECK_STEAM_EXTRA:-} > /dev/null
    echo "started: Xvfb $D ($SIZE), Steam hidden (unit utdeck-steam)"
    # Keep-awake for downloads (stage 4.7, utdeck-awake.py): the phone does not go to sleep while
    # Steam downloads a game (it stopped with the phone locked); it ends with Steam.
    systemctl --user is-active -q utdeck-awake 2>/dev/null \
        || systemd-run --user --unit=utdeck-awake --collect python3 -I "$W/wine/utdeck-awake.py" downloads > /dev/null 2>&1
    python3 "$W/steam/steam-library.py" > /dev/null 2>&1 || true   # the app drawer entries (3.8)
    ;;
play|ready)
    [ "$1" = ready ] || [ -n "${2:-}" ] || { echo "usage: $0 play APPID" >&2; exit 2; }
    if ! signed_in; then
        [ "$1" = play ] && notify "Starting Steam, the game follows in about a minute"
        # A fresh start must sign in anew: the log's last line is then still the previous session's.
        n0=0
        if ! pgrep -x -u "$(id -u)" steam > /dev/null; then
            n0=$(wc -l < "$CONN" 2>/dev/null || echo 0)
            "$0" start > /dev/null
        fi
        i=0; restarted=; until signed_in && [ "$(wc -l < "$CONN")" -gt "$n0" ]; do
            i=$((i + 1)); [ $i -le 90 ] || { notify "Steam did not sign in, the game was not started"; exit 1; }
            # A running client that its server signed out ("Try another CM", then "Logged Off")
            # may schedule its next try minutes later (stage 3.6: 582 s, attempt 15): after 20 s
            # still signed out, restart it once; a fresh start signed in within 10 s.
            if [ $i -eq 10 ] && [ -z "$restarted" ] && [ "$(state)" = "Logged Off" ]; then
                restarted=1; n0=$(wc -l < "$CONN")
                "$0" stop > /dev/null; "$0" start > /dev/null
            fi
            sleep 2
        done
        sleep 3   # the client takes commands a moment after signing in
    fi
    [ "$1" = ready ] || "$0" launch "$2"
    ;;
stop)
    # Not while Steam uploads a game's saves to Steam Cloud (stage 4.7): right after a game ends it
    # runs an exit sync ("Starting sync (up,AC Exit,)") and uploads each changed file; a stop then
    # would leave the last save only on the phone. Waited for while the cloud log's last upload line
    # is one of an upload under way (not "Upload complete" or "Upload failed") and the log was written
    # in the last minute; at most 5 minutes. (Waiting for a quiet log instead held a stop right after
    # Steam's start for 99 s: Steam then checks every game's cloud state and writes for a minute.)
    CL=$HOME/.local/share/Steam/logs/cloud_log.txt
    cloud_busy() {
        [ -f "$CL" ] && [ $(( $(date +%s) - $(stat -c %Y "$CL") )) -lt 60 ] || return 1
        last=$(grep -E 'Starting sync \(up|Upload batch initiated|HTTP upload for file|Upload OK for file|Upload complete|Upload failed' "$CL" | tail -1)
        case "$last" in ''|*'Upload complete'*|*'Upload failed'*) return 1 ;; esac
    }
    for i in $(seq 1 150); do
        cloud_busy || break
        [ $i = 1 ] && echo "waiting for Steam Cloud to finish an upload"
        sleep 2
    done
    # Only to a running client: with none, "steam -shutdown" starts a new one, which without the
    # hidden screen shows "Unable to open a connection to X" in a box Lomiri cannot draw (an
    # endless spinner on the phone, stage 3.4).
    if pgrep -x -u "$(id -u)" steam > /dev/null; then
        cd "$HOME/.local/share/Steam/steamrtarm64" && DISPLAY=$D LD_LIBRARY_PATH=$PWD timeout 30 ./steam -shutdown > /dev/null 2>&1
    fi
    for i in $(seq 1 30); do pgrep -u "$(id -u)" -f '[s]teamrtarm64/' > /dev/null || break; sleep 1; done
    systemctl --user stop utdeck-steam utdeck-xvfb utdeck-awake 2>/dev/null
    # What is still ending (a client process outside the unit, the -shutdown call's own): up to 15 s
    # more, so that whoever stopped Steam to edit its files finds it gone (stage 4.7: set-default.py
    # found one right after a stop).
    for i in $(seq 1 15); do pgrep -u "$(id -u)" -f '[s]teamrtarm64/' > /dev/null || break; sleep 1; done
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
    echo "usage: $0 start|stop|status|launch ID|hide|play ID|ready" >&2; exit 2 ;;
esac
