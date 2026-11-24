#!/bin/sh
# UTDeck stage 4.9: everything UTDeck put on the phone removed, except what the user keeps.
#
#   utdeck-uninstall.sh plan [--delete-games] [--delete-saves]   what would be removed or kept, with
#                                                                sizes; nothing is changed
#   utdeck-uninstall.sh run  [--delete-games] [--delete-saves] [--wait-app PID] [--remove-app]
#
# By default the games and the saves are kept (the maintainer, 2026-11-23), left where they are, so
# that installing UTDeck again finds them: Steam's games (steamapps: common, the appmanifest files,
# shadercache, unfinished downloads), each Steam game's Windows folder with its saves and UTDeck's
# settings for it (steamapps/compatdata), Steam's userdata (the saves of games that use Steam Cloud
# through Steam's own interface), the user's own games (wine/own/<id>: settings, Windows folder, an
# installed game) and the game folders the user copied (Documents/UTDeck/Games).
#   --delete-games   the Steam games and the copied folders go; an own game's Windows folder stays
#                    with the saves (its game.conf set aside, as removing it in the app does, so it is
#                    found again if its folder is added again; a game installed into that folder
#                    stays in it)
#   --delete-saves   compatdata, userdata and the own games' folders go (with them an own game that
#                    was installed into its Windows folder)
# Always removed: UTDeck's scripts and Wine runtimes (~/utdeck-probe), Steam's client and runtimes,
# Steam's sign-in (Steam/config, ~/.steam), the app drawer entries of the games, Wine's old menu files,
# the app's own cache and settings, and (in RAM) what Steam and the hidden screen left in /tmp and
# /dev/shm. Nothing outside the user's own folders is touched (UTDeck puts nothing there, stage 4.9
# step 1). The phone's shared GPU cache (~/.cache/mesa_shader_cache) is left alone: other apps may use
# it.
#
# plan prints one line per item, tab-separated: ACTION KB KEY WHERE
#   ACTION   remove or keep
#   KEY      utdeck, own, steamclient, signin, games, saves, copied, entries, appdata, memory (the
#            app words them)
#   WHERE    the path under ~ (Steam's userdata is named as a whole: its folders carry the account)
# run writes each step's state as utdeck-setup.sh does, into $XDG_RUNTIME_DIR/utdeck-uninstall/STEP
# ("STATE DETAIL"), steps: stop steam utdeck entries memory appdata (app). The app starts it as a
# systemd user unit (utdeck-uninstall) from its click, so that closing the app does not stop it
# halfway.
# --wait-app PID: the app's own folders are removed once that process has ended (it writes its settings
# while it closes), waiting at most 10 minutes.
# --remove-app: then the app itself is removed, through the phone's click service (com.lomiri.click,
# Remove with the package name, as the OpenStore removes an app); its files, this script among them,
# go with it (a running script is not disturbed by its file going). Step "app".
# A home with the DEVELOPMENT marker: plan's first line is "refuse 0 development ...", and run fails at
# its first step.
#
# Safety:
# - with HOME pointing anywhere but the user's real home (a made-up home for testing), nothing is
#   stopped and nothing outside HOME is touched;
# - a home with utdeck-probe/DEVELOPMENT (stone, the development phone) is never removed: plan only.
# No account data is read or printed.
set -u
MODE=${1:-}
[ $# -gt 0 ] && shift
DEL_GAMES= DEL_SAVES= APP_PID= REMOVE_APP=
while [ $# -gt 0 ]; do
    case "$1" in
    --delete-games) DEL_GAMES=1 ;;
    --delete-saves) DEL_SAVES=1 ;;
    --wait-app) APP_PID=${2:-}; shift ;;
    --remove-app) REMOVE_APP=1 ;;
    *) MODE=usage ;;
    esac
    shift
done
case "$MODE" in plan|run) ;; *) sed -n '2,8p' "$0" >&2; exit 2 ;; esac

UID_=$(id -u)
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$UID_}
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}
REAL_HOME=$(getent passwd "$UID_" | cut -d: -f6)
TEST=
[ "$HOME" != "$REAL_HOME" ] && TEST=1
P=$HOME/utdeck-probe
S=$HOME/.local/share/Steam
CLICK=utdeck.alphaliu01
if [ -n "$TEST" ]; then ST=$HOME/.uninstall-state; else ST=$XDG_RUNTIME_DIR/utdeck-uninstall; fi

if [ "$MODE" = run ] && [ -e "$P/DEVELOPMENT" ]; then
    echo "utdeck-uninstall: $P/DEVELOPMENT exists (the development phone): nothing is removed here" >&2
    rm -rf "$ST"; mkdir -p "$ST"
    echo "failed this is UTDeck's development phone (utdeck-probe/DEVELOPMENT): nothing is removed" > "$ST/stop"
    exit 3
fi

# --- What is kept ---
keep_games() { [ -z "$DEL_GAMES" ]; }
keep_saves() { [ -z "$DEL_SAVES" ]; }

# What each top-level entry of Steam's folder and of steamapps is: steamclient, signin, games, saves.
steam_kind() {
    case "$1" in
    config) echo signin ;;
    userdata) echo saves ;;
    steamapps) echo steamapps ;;
    *) echo steamclient ;;
    esac
}
steamapps_kind() {
    case "$1" in
    compatdata) echo saves ;;
    temp) echo steamclient ;;
    *) echo games ;;   # common, appmanifest_*.acf, shadercache, downloading, workshop, sourcemods, libraryfolders.vdf
    esac
}
kept() {
    case "$1" in
    games|copied) keep_games ;;
    saves|own) keep_saves ;;
    *) false ;;
    esac
}

kb() { du -sk "$@" 2>/dev/null | awk '{s += $1} END {print s + 0}'; }
shown() { case "$1" in "$HOME"/*) echo "~${1#"$HOME"}" ;; *) echo "$1" ;; esac; }

# --- The items, for plan and run alike ---
# Each yields "KEY PATH" lines (paths that exist).
items() {
    # UTDeck's own folder; the own games' folders apart.
    if [ -d "$P" ]; then
        for e in "$P"/* "$P"/.[!.]*; do
            [ -e "$e" ] || [ -L "$e" ] || continue
            if [ "${e##*/}" = wine ] && [ -d "$e" ] && [ ! -L "$e" ]; then
                for w in "$e"/* "$e"/.[!.]*; do
                    [ -e "$w" ] || [ -L "$w" ] || continue
                    if [ "${w##*/}" = own ]; then echo "own $w"; else echo "utdeck $w"; fi
                done
            else
                echo "utdeck $e"
            fi
        done
    fi
    # Steam's folder.
    if [ -d "$S" ]; then
        for e in "$S"/* "$S"/.[!.]*; do
            [ -e "$e" ] || [ -L "$e" ] || continue
            k=$(steam_kind "${e##*/}")
            if [ "$k" = steamapps ] && [ -d "$e" ] && [ ! -L "$e" ]; then
                for a in "$e"/* "$e"/.[!.]*; do
                    [ -e "$a" ] || [ -L "$a" ] || continue
                    echo "$(steamapps_kind "${a##*/}") $a"
                done
            else
                echo "$k $e"
            fi
        done
    fi
    [ -e "$HOME/.steam" ] && echo "signin $HOME/.steam"
    [ -d "$HOME/Documents/UTDeck/Games" ] && echo "copied $HOME/Documents/UTDeck/Games"
    # The app drawer: UTDeck's game entries, Steam's own shortcuts (steam://rungameid), Wine's menu files.
    A=$HOME/.local/share/applications
    for f in "$A"/utdeck-*.desktop; do [ -e "$f" ] && echo "entries $f"; done
    for f in "$A"/*.desktop; do
        [ -f "$f" ] && grep -q '^Exec=.*steam://rungameid/' "$f" 2>/dev/null && echo "entries $f"
    done
    [ -d "$A/wine" ] && echo "entries $A/wine"
    for f in "$HOME"/.local/share/desktop-directories/wine-*.directory "$HOME"/.config/menus/applications-merged/wine-*.menu \
             "$HOME"/.local/share/icons/hicolor/*/apps/steam_icon_*.png; do
        [ -e "$f" ] && echo "entries $f"
    done
    # The app's own folders.
    for f in "$HOME/.cache/utdeck" "$HOME/.cache/$CLICK" "$HOME/.config/$CLICK" "$HOME/.local/share/$CLICK"; do
        [ -e "$f" ] && echo "appdata $f"
    done
}

# What Steam and the hidden screen left in RAM (not in a made-up home's test).
memory_items() {
    [ -n "$TEST" ] && return
    for f in /tmp/steam* /tmp/.com.valvesoftware.Steam.* /tmp/steam_chrome_shmem_* /tmp/.X50-lock /tmp/.X11-unix/X50 \
             /dev/shm/u"$UID_"-Shm_* /dev/shm/u"$UID_"-ValveIPCSharedObj-*; do
        [ -e "$f" ] || [ -L "$f" ] || continue
        [ "$(stat -c %u "$f" 2>/dev/null)" = "$UID_" ] && echo "memory $f"
    done
    [ -d "$XDG_RUNTIME_DIR/utdeck" ] && echo "memory $XDG_RUNTIME_DIR/utdeck"
}

# --- plan ---
if [ "$MODE" = plan ]; then
    [ -e "$P/DEVELOPMENT" ] && printf 'refuse\t0\tdevelopment\t%s\n' "$(shown "$P/DEVELOPMENT")"
    { items; memory_items; } | while read -r key path; do
        if kept "$key"; then act=keep; else act=remove; fi
        # Steam's userdata named as a whole (its folders carry the account number).
        printf '%s\t%s\t%s\t%s\n' "$act" "$(kb "$path")" "$key" "$(shown "$path")"
    done | awk -F'\t' '
        # One line per ACTION and KEY, the paths joined (the app lists the kinds, not every file).
        { k = $1 "\t" $3; s[k] += $2; if (!(k in p)) { o[++n] = k; p[k] = $4 } else if (c[k] < 3) p[k] = p[k] ", " $4; c[k]++ }
        END { for (i = 1; i <= n; i++) { split(o[i], a, "\t"); w = p[o[i]]; if (c[o[i]] > 3) w = w ", ..."; printf "%s\t%d\t%s\t%s\n", a[1], s[o[i]], a[2], w } }'
    exit 0
fi

# --- run ---
put() {
    mkdir -p "$ST"
    printf '%s %s\n' "$2" "${3:-}" > "$ST/.$1" && mv -f "$ST/.$1" "$ST/$1"
    echo "$1: $2 ${3:-}"
}
rm -rf "$ST"
for s in stop steam utdeck entries memory appdata $( [ -n "$REMOVE_APP" ] && echo app ); do put "$s" todo; done

steam_procs() { pgrep -u "$UID_" -f '[s]teamrtarm64/|[u]buntu12_(32|64)/' > /dev/null; }

# 1. What runs: Steam (its stop waits for a Steam Cloud upload in progress), then UTDeck's units.
put stop running
if [ -z "$TEST" ]; then
    f=$XDG_RUNTIME_DIR/utdeck/game
    if [ -f "$f" ] && read -r label pid _ < "$f" && kill -0 "$pid" 2> /dev/null; then
        put stop failed "a game is running ($label): quit it first"
        exit 1
    fi
    if steam_procs && [ -x "$P/steam/steam-service.sh" ]; then
        put stop running "Steam: stopping, after any Steam Cloud upload in progress"
        sh "$P/steam/steam-service.sh" stop
    fi
    for u in $(systemctl --user list-units --all --plain --no-legend 'utdeck-*' 2>/dev/null | awk '{print $1}'); do
        case "$u" in utdeck-uninstall*) continue ;; esac
        systemctl --user stop "$u" 2> /dev/null
    done
    if steam_procs; then
        pkill -TERM -u "$UID_" -f '[s]teamrtarm64/|[u]buntu12_(32|64)/'
        sleep 3
        pkill -KILL -u "$UID_" -f '[s]teamrtarm64/|[u]buntu12_(32|64)/'
    fi
    pkill -u "$UID_" -x Xvfb 2> /dev/null
fi
put stop done

# 2-4. Steam's folder, UTDeck's folder, the app drawer.
remove_step() {   # STEP KEY...: removes the not-kept items of these keys
    step=$1; shift
    put "$step" running
    n=0
    items | while read -r key path; do
        for want in "$@"; do
            [ "$key" = "$want" ] || continue
            kept "$key" && continue
            rm -rf -- "$path" && echo "  removed $(shown "$path")"
        done
    done
    put "$step" done
}
remove_step steam steamclient signin games saves
rmdir "$S" 2> /dev/null

# An own game whose saves are kept but whose game goes: set aside, as removing it in the app does.
if keep_saves && ! keep_games; then
    for c in "$P"/wine/own/*/game.conf; do
        [ -f "$c" ] && mv -f "$c" "${c%game.conf}game.conf.removed" && echo "  set aside $(shown "$c")"
    done
fi
remove_step utdeck utdeck own copied
rmdir "$P/wine" "$P" "$HOME/Documents/UTDeck" 2> /dev/null
remove_step entries entries

# 5. RAM: only once nothing of Steam or the hidden screen runs any more.
put memory running
if [ -z "$TEST" ]; then
    if steam_procs || pgrep -u "$UID_" -x Xvfb > /dev/null; then
        put memory done "left: Steam or the hidden screen still runs (a restart clears it)"
    else
        memory_items | while read -r key path; do rm -rf -- "$path"; done
        put memory done
    fi
else
    put memory done "test home: skipped"
fi

# 6. The app's own folders, once the app has closed.
put appdata running
if [ -n "$APP_PID" ]; then
    put appdata running "waiting for the app to close"
    i=0
    while kill -0 "$APP_PID" 2> /dev/null && [ $i -lt 600 ]; do sleep 1; i=$((i + 1)); done
fi
items | while read -r key path; do [ "$key" = appdata ] && rm -rf -- "$path" && echo "  removed $(shown "$path")"; done
put appdata done

# 7. The app itself.
if [ -n "$REMOVE_APP" ]; then
    put app running
    if [ -n "$TEST" ]; then
        put app done "test home: skipped"
    elif gdbus call --system --dest com.lomiri.click --object-path /com/lomiri/click \
            --method com.lomiri.click.Remove "$CLICK" > /dev/null; then
        put app done
    else
        put app failed "the app could not be removed: remove it in the OpenStore"
    fi
fi
echo "utdeck-uninstall: done"
