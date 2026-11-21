#!/bin/sh
# UTDeck stage 4.2: the first-time setup the app runs, on a phone with nothing but UTDeck's click.
#
#   utdeck-setup.sh run          the mandatory part: every step of it not done yet, in order; stops
#                                at the first that fails
#   utdeck-setup.sh run steam    the mandatory part, then Steam's (Steam is optional: the user can
#                                skip it, and set it up later from the app's settings)
#   utdeck-setup.sh check        each step's state now, without doing anything (quick)
#
# The app starts "run" as a systemd user unit (utdeck-setup), so that Lomiri does not pause it with
# the app, and reads each step's state from $XDG_RUNTIME_DIR/utdeck/setup/STEP, one line:
# "STATE DETAIL" (STATE: todo, running, done or failed; DETAIL: progress or the error, for people).
# A step already done is skipped, so running it again goes on where it stopped.
#
# The steps (stage 4.2's probe on the Nothing Phone (1) found each of them). Mandatory:
#   files     UTDeck's scripts and compatibility tools, from the click (home/, home-files.sh)
# Steam's (optional; Xvfb serves only hidden Steam):
#   xvfb      the hidden screen: Ubuntu's xvfb package, from Ubuntu's archive by name and SHA256
#             (24.04-1.x phones have no apt package lists)
#   runtime   the default Wine runtime (get-runtime.sh default; a file already in wine/dl with its
#             .sha256 is used instead of a download)
#   steam     Steam Runtime 3 and Valve's client (steam-install.sh 1), then the client's first
#             update, on the hidden screen (steam-install.sh 2 with DISPLAY=:50): no window shown
#   service   hidden Steam started (steam-service.sh start)
#   signin    waits until Steam is signed in; the app shows the QR code of Steam's hidden sign-in
#             window meanwhile. Only the state word of Steam's connection log is read.
#
# No account data is read, kept or written here.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
W=$HOME/utdeck-probe
STEAM=$HOME/.local/share/Steam
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}
ST=$XDG_RUNTIME_DIR/utdeck/setup
STEPS_BASE="files runtime"
STEPS_STEAM="xvfb steam service signin"
STEPS="$STEPS_BASE $STEPS_STEAM"

XVFB_DEB=xvfb_21.1.12-1ubuntu1.9_arm64.deb
XVFB_URL=http://ports.ubuntu.com/ubuntu-ports/pool/universe/x/xorg-server/$XVFB_DEB
XVFB_SHA256=15827310fc6f356ba8259b102301f5417264f742a9abf3bd7f3487dc1c5ad42c

# put STEP STATE DETAIL: the step's line, written whole (the app may read at any moment).
put() {
    mkdir -p "$ST"
    printf '%s %s\n' "$2" "${3:-}" > "$ST/.$1" && mv -f "$ST/.$1" "$ST/$1"
    echo "$1: $2 ${3:-}"
}

# The steps' "done" tests (check and run use the same ones).
files_version() { cat "$HERE/home/VERSION" 2>/dev/null; }
done_files()   { [ -n "$(files_version)" ] && [ "$(cat "$W/.files-version" 2>/dev/null)" = "$(files_version)" ]; }
done_xvfb()    { [ -x "$W/xvfb/root/usr/bin/Xvfb" ]; }
done_runtime() { [ -x "$W/wine/runtime/bin/wine" ]; }
done_steam()   { [ -f "$STEAM/steamrtarm64/steamui.so" ] && ls "$STEAM"/package/*.installed > /dev/null 2>&1; }
done_service() { pgrep -x -u "$(id -u)" steam > /dev/null; }
signin_state() {
    grep -o '^\[[^]]*\] \[[A-Za-z ]*,' "$STEAM/logs/connection_log.txt" 2>/dev/null | tail -1 | sed 's/.*\[//; s/,$//'
}
done_signin()  { done_service && [ "$(signin_state)" = "Logged On" ]; }

# label STEP TEXT: what the step does now, for the ticker.
label() { mkdir -p "$ST" && echo "$2" > "$ST/.label-$1" && put "$1" running "$2"; }
# ticker STEP PATH: while a download or unpacking runs, "LABEL, N MB" every 3 s from PATH's size.
# Started in the background; stopped with kill.
ticker() {
    while sleep 3; do
        put "$1" running "$(cat "$ST/.label-$1" 2>/dev/null), $(du -sm "$2" 2>/dev/null | cut -f1) MB"
    done
}

step_files() {
    # The tree home-files.sh made, as it is; then the version it came with.
    cp -r "$HERE/home/utdeck-probe" "$HOME/" && cp -r "$HERE/home/.local" "$HOME/" || return 1
    files_version > "$W/.files-version"
}

step_xvfb() {
    mkdir -p "$W/xvfb" && cd "$W/xvfb" || return 1
    wget -q -c --tries=10 -O "$XVFB_DEB" "$XVFB_URL" || { echo "download failed: $XVFB_URL"; return 1; }
    echo "$XVFB_SHA256  $XVFB_DEB" | sha256sum -c - > /dev/null || { rm -f "$XVFB_DEB"; echo "checksum mismatch: $XVFB_DEB"; return 1; }
    rm -rf root && dpkg-deb -x "$XVFB_DEB" root && rm -f "$XVFB_DEB"
}

step_runtime() {
    file=$(ls "$W"/wine/dl/utdeck-runtime-[0-9]*.tar.xz 2>/dev/null | tail -1)
    sh "$W/wine/get-runtime.sh" default ${file:+"$file"}
}

step_steam() {
    sh "$W/steam/steam-install.sh" 1 || return 1
    label steam "updating the Steam client (a few minutes)"
    sh "$W/steam/steam-service.sh" xvfb || return 1
    DISPLAY=:50 sh "$W/steam/steam-install.sh" 2
    done_steam
}

step_service() {
    sh "$W/steam/steam-service.sh" start
    for i in $(seq 1 30); do done_service && return 0; sleep 1; done
    echo "Steam did not start"; return 1
}

step_signin() {
    # 30 minutes for the user to scan the code; then the step fails and the app can run it again.
    for i in $(seq 1 900); do
        done_signin && return 0
        done_service || { echo "Steam stopped"; return 1; }
        sleep 2
    done
    echo "not signed in after 30 minutes"; return 1
}

case "${1:-}" in
check)
    for s in $STEPS; do
        if "done_$s"; then echo "$s done"; else echo "$s todo"; fi
    done
    ;;
run)
    todo=$STEPS_BASE
    [ "${2:-}" = steam ] && todo="$STEPS_BASE $STEPS_STEAM"
    for s in $todo; do
        if "done_$s"; then put "$s" done; else put "$s" todo; fi
    done
    for s in $todo; do
        "done_$s" && continue
        case $s in
            xvfb)    label xvfb "downloading Xvfb"; ticker xvfb "$W/xvfb" & T=$! ;;
            runtime) label runtime "downloading and unpacking the Wine runtime"; ticker runtime "$W/wine" & T=$! ;;
            steam)   label steam "downloading Steam"; ticker steam "$STEAM" & T=$! ;;
            signin)  put signin running "waiting for the QR code to be scanned"; T= ;;
            *)       put "$s" running; T= ;;
        esac
        # Each step's output: ~/utdeck-probe/setup-STEP.log (no account data in any of them).
        mkdir -p "$W"
        ( "step_$s" ) > "$W/setup-$s.log" 2>&1; rc=$?
        [ -n "$T" ] && kill "$T" 2>/dev/null
        if [ $rc -eq 0 ] && "done_$s"; then
            put "$s" done
        else
            put "$s" failed "$(grep -v '^$' "$W/setup-$s.log" | tail -1)"
            exit 1
        fi
    done
    ;;
*)
    echo "usage: $0 run [steam] | check" >&2; exit 2 ;;
esac
