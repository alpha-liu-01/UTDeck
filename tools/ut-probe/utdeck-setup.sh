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
#   xvfb      the hidden screen: Ubuntu's current xvfb package for 24.04, found in the archive's
#             signed index (InRelease, checked with gpgv and Ubuntu's archive key, which every phone
#             has; then the package list it vouches for; then the package that list vouches for).
#             24.04-1.x phones have no apt package lists. A known package is the last resort.
#   runtime   the default Wine runtime (get-runtime.sh default; a file already in wine/dl with its
#             .sha256 is used instead of a download)
#   steam     Steam Runtime 3 and Valve's client (steam-install.sh 1), then the client's first
#             update, on the hidden screen (steam-install.sh 2 with DISPLAY=:50): no window shown
#   service   hidden Steam started (steam-service.sh start)
#   signin    waits until Steam is signed in; the app shows the QR code of Steam's hidden sign-in
#             window meanwhile. Only the state word of Steam's connection log is read.
#   tool      UTDeck's compatibility tool made Steam's default for Windows games (set-default.py:
#             app "0", as Steam's settings page writes it), with Steam stopped (it rewrites its
#             config.vdf while it runs) and started again. After the sign-in, since only a signed-in
#             Steam has written config.vdf; the restart (with -silent) also closes the main window
#             Steam opens after a first sign-in. (Stage 4.2, the Nothing Phone (1): without it Steam
#             ran Ys IX through its own Proton Experimental under FEX, and it ended after 2 s.)
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
STEPS_STEAM="xvfb steam service signin tool"
STEPS="$STEPS_BASE $STEPS_STEAM"

ARCHIVE=http://ports.ubuntu.com/ubuntu-ports
KEYRING=/usr/share/keyrings/ubuntu-archive-keyring.gpg
# The last resort, if no signed index can be read: the package of stage 4.2's probe. Ubuntu drops
# superseded files from pool/ after a while, so this one stops working some day.
XVFB_KNOWN="pool/universe/x/xorg-server/xvfb_21.1.12-1ubuntu1.9_arm64.deb 15827310fc6f356ba8259b102301f5417264f742a9abf3bd7f3487dc1c5ad42c"
# Free space Steam needs: its download and first update (3.7 GB), and what it adds soon after
# (5.5 GB on the Nothing Phone (1) within hours), with some room left.
STEAM_FREE_MB=7000

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
TOOL=$STEAM/compatibilitytools.d/utdeck
done_tool()    {
    python3 "$TOOL/set-default.py" --show 2> /dev/null |
        awk '$1 == "\"0\"" {z = 1} z && $1 == "\"name\"" {print $2; exit}' | grep -qx '"utdeck-proton"'
}

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
    # Steam's side brought up to the new files (stage 4.7, steam-tools.sh: the tool's name, each
    # game's user folder): Steam stopped for it and started again if it runs; not while a game runs.
    sh "$W/steam/steam-tools.sh" --restart
}

# xvfb_in SUITE: "PATH SHA256" of xvfb (arm64) in SUITE's signed index, or nothing.
xvfb_in() {
    i=$W/xvfb/index
    rm -rf "$i" && mkdir -p "$i" || return 1
    wget -q --tries=5 -O "$i/InRelease" "$ARCHIVE/dists/$1/InRelease" || return 1
    # Only the text the signature covers is read (--output), never the file as downloaded.
    gpgv --keyring "$KEYRING" --output "$i/Release" "$i/InRelease" 2> "$i/gpgv.log" || return 1
    sum=$(awk '/^SHA256:/{s=1; next} /^[A-Za-z]/{s=0} s && $3 == "universe/binary-arm64/Packages.gz" {print $1}' "$i/Release")
    [ -n "$sum" ] || return 1
    wget -q --tries=5 -O "$i/Packages.gz" "$ARCHIVE/dists/$1/universe/binary-arm64/Packages.gz" || return 1
    echo "$sum  $i/Packages.gz" | sha256sum -c - > /dev/null || return 1
    zcat "$i/Packages.gz" | awk -v RS= '/(^|\n)Package: xvfb\n/' |
        awk '/^Filename:/ {f = $2} /^SHA256:/ {h = $2} END {if (f && h) print f, h}'
}

step_xvfb() {
    mkdir -p "$W/xvfb" && cd "$W/xvfb" || return 1
    found=
    for suite in noble-updates noble-security noble; do
        found=$(xvfb_in "$suite") && [ -n "$found" ] && { echo "xvfb from $suite's signed index: ${found%% *}"; break; }
        found=
    done
    rm -rf index
    [ -n "$found" ] || { found=$XVFB_KNOWN; echo "no signed index could be read; the known package: ${found%% *}"; }
    path=${found%% *} sum=${found##* } deb=$(basename "${found%% *}")
    wget -q -c --tries=10 -O "$deb" "$ARCHIVE/$path" || { rm -f "$deb"; echo "download failed: $ARCHIVE/$path"; return 1; }
    echo "$sum  $deb" | sha256sum -c - > /dev/null || { rm -f "$deb"; echo "checksum mismatch: $deb"; return 1; }
    rm -rf root && dpkg-deb -x "$deb" root && rm -f "$deb"
}

step_runtime() {
    file=$(ls "$W"/wine/dl/utdeck-runtime-[0-9]*.tar.xz 2>/dev/null | tail -1)
    sh "$W/wine/get-runtime.sh" default ${file:+"$file"}
}

step_steam() {
    free=$(df -m "$HOME" | awk 'NR == 2 {print $4}')
    [ "$free" -ge "$STEAM_FREE_MB" ] || { echo "not enough space for Steam: $free MB free, $STEAM_FREE_MB MB needed"; return 1; }
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

step_tool() {
    sh "$W/steam/steam-service.sh" stop || return 1
    python3 "$TOOL/set-default.py" || return 1
    sh "$W/steam/steam-service.sh" start
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
            tool)    put tool running "making UTDeck's runtime Steam's choice for Windows games"; T= ;;
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
