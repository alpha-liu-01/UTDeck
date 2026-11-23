#!/bin/sh
# UTDeck stage 4.7: Steam's own tasks for one game, without Steam's interface.
#
#   steam-game.sh install APPID     install a game of the user's library
#   steam-game.sh uninstall APPID   uninstall an installed game
#   steam-game.sh validate APPID    Steam's "Verify integrity of game files": every file hashed against
#                                   Steam's copy, what is missing or damaged downloaded again (no
#                                   question; the result is in Steam's content log)
#
# Both make sure hidden Steam runs and is signed in (steam-service.sh ready), then hand Steam its own
# command (steam://install/APPID, steam://uninstall/APPID). Steam answers with a window of its own on
# the hidden screen (:50): "Install" (the game, its size, the drive and its free space) or "Uninstall"
# (a question); UTDeck shows it on its "Steam is asking" page, and the user answers it there.
#
# install, for a game that also has a Linux build (appinfo.py: its systems): first maps the game to
# UTDeck's compatibility tool (set-default.py --app, stage 3.7's rule), so that Steam downloads its
# Windows build. Steam rewrites its config.vdf while it runs, so for that Steam is stopped for a moment
# and started again (about 10 s, then its sign-in). Not again once mapped.
#
# What happens next is read by the app from Steam's files: the download in steamapps/downloading/APPID
# and the game's appmanifest_APPID.acf (installed: StateFlags 4), the uninstall's end (the .acf gone).
# Steam keeps a game's compatdata/APPID (its Windows folder, with saves not in Steam Cloud) on
# uninstall; deleting it is the app's separate step.
set -u
P=$HOME/utdeck-probe
S=$HOME/.local/share/Steam
SERVICE=$P/steam/steam-service.sh
TOOL=$S/compatibilitytools.d/utdeck
VERB=${1:-} APPID=${2:-}
case "$APPID" in ''|*[!0-9]*) echo "usage: steam-game.sh install|uninstall|validate APPID" >&2; exit 2 ;; esac
log() { echo "steam-game: $(date +%T) $*"; }

steam_url() {
    cd "$S/steamrtarm64" && DISPLAY=:50 LD_LIBRARY_PATH=$PWD timeout 30 ./steam -silent "$1" > /dev/null 2>&1
}

case "$VERB" in
install)
    systems=$(python3 -I "$P/appinfo.py" "$APPID" | cut -f3)
    log "app $APPID, systems: ${systems:-unknown}"
    case "$systems" in
    *linux*)
        if python3 "$TOOL/set-default.py" --show 2>/dev/null | grep -q "\"$APPID\""; then
            log "already mapped to UTDeck's tool"
        else
            log "a Linux build too: mapping the game to UTDeck's tool, so Steam installs its Windows build"
            "$SERVICE" stop > /dev/null
            python3 "$TOOL/set-default.py" --app "$APPID" utdeck-proton > /dev/null || { log "mapping failed"; exit 1; }
            "$SERVICE" start > /dev/null
        fi ;;
    esac
    "$SERVICE" ready || { log "Steam did not sign in"; exit 1; }
    log "asking Steam to install it"
    steam_url "steam://install/$APPID"
    ;;
uninstall)
    "$SERVICE" ready || { log "Steam did not sign in"; exit 1; }
    log "asking Steam to uninstall $APPID"
    steam_url "steam://uninstall/$APPID"
    ;;
validate)
    "$SERVICE" ready || { log "Steam did not sign in"; exit 1; }
    log "asking Steam to check $APPID's files"
    steam_url "steam://validate/$APPID"
    # Steam brings its main window up with it (stage 4.7, checked on the hidden screen): closed, as
    # after a launch (it shows the account and plays the store's videos in software).
    "$SERVICE" hide > /dev/null
    ;;
*)
    echo "usage: steam-game.sh install|uninstall|validate APPID" >&2; exit 2 ;;
esac
