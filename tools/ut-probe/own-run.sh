#!/bin/sh
# UTDeck stage 4.5: start one of the user's own games (not Steam's), as Steam's are started.
#
#   own-run.sh ID                      the game
#   own-run.sh ID --install SETUP_EXE  run the game's installer in the game's prefix (the installer
#                                      way of adding a game: its window over the app, as a game's)
#
# A game is a folder wine/own/ID/ with:
#   game.conf   NAME=, DIR= (the game's folder), EXE= (relative to DIR, or a full path), COVER=
#               (an image, optional): one NAME=value per line, read as text, never run as a script
#   utdeck-env  optional per-game settings, one NAME=value per line, as compatdata/<appid>/utdeck-env
#               for Steam's games (stage 3.4; the settings page is stage 4.6)
#   pfx/        its Wine prefix (pfx-proton/ with Proton 11's base), made here on the first start
#   own-run.log what this script did (the game's own log is wine/games/own-ID-<time>.log)
# The game's own files stay where the user put them (Documents/UTDeck/Games/...); an installed game
# lives inside its prefix.
set -u
W=/home/phablet/utdeck-probe/wine
ID=${1:?usage: own-run.sh ID [--install SETUP_EXE]}
O=$W/own/$ID
[ -f "$O/game.conf" ] || { echo "own-run: no game $ID ($O/game.conf)" >&2; exit 1; }
LOG=$O/own-run.log
echo "own-run: $(date '+%F %T') $*" >> "$LOG"
conf() { sed -n "s/^$1=//p" "$O/game.conf" | head -1; }

# Per-game settings, as utdeck-compat reads Steam's: lines starting with # are skipped.
if [ -f "$O/utdeck-env" ]; then
    while IFS= read -r line; do
        case "$line" in ''|'#'*) continue ;; *=*) export "$line"; printf '  per-game: %s\n' "$line" >> "$LOG" ;; esac
    done < "$O/utdeck-env"
fi
# The Wine base (stage 3.3), each with its own prefix.
export UTDECK_RUNTIME=${UTDECK_RUNTIME:-utdeck}
case "$UTDECK_RUNTIME" in
proton) export WINEPREFIX=$O/pfx-proton ;;
*)      export WINEPREFIX=$O/pfx ;;
esac

# The prefix, on the first start: made, DXVK and VKD3D-Proton put in, and Wine Mono (as utdeck-compat
# does for Steam's: .NET programs need it, 3DMark 11's interface among them). No window meanwhile.
(
    . $W/env.sh
    if [ ! -d "$WINEPREFIX/drive_c" ]; then
        t=$(date +%s)
        WINEDLLOVERRIDES="winewayland.drv,winex11.drv=d;mscoree,mshtml=;winemenubuilder.exe=d" \
            wineboot -i > /dev/null 2>&1; wineserver -w
        $W/prefix-dlls.sh > /dev/null && echo "  prefix made, DXVK and VKD3D-Proton in ($(( $(date +%s) - t )) s)" >> "$LOG"
    fi
    if [ ! -d "$WINEPREFIX/drive_c/windows/mono" ]; then
        M=$(ls "$(dirname "$(command -v wine)")"/../share/wine/mono/wine-mono-*.msi 2>/dev/null | tail -1)
        if [ -n "$M" ]; then
            t=$(date +%s)
            WINEDLLOVERRIDES="mshtml=;winemenubuilder.exe=d" wine msiexec /i "$(winepath -w "$M" 2>/dev/null)" /qn > /dev/null 2>&1
            wineserver -w
            [ -d "$WINEPREFIX/drive_c/windows/mono" ] && echo "  Wine Mono installed ($(( $(date +%s) - t )) s)" >> "$LOG" \
                || echo "  Wine Mono install failed" >> "$LOG"
        fi
    fi
    # Documents, Music, ... as real folders in the prefix (stage 4.7, utdeck-profile.py): Wine links
    # them to the phone's own folders, so a game's files landed in the phone's Documents (3DMark 11's
    # shader cache). No Steam Cloud here, so no shared folder: only the Windows user's own.
    case "$UTDECK_RUNTIME" in proton) wuser=steamuser ;; *) wuser=${USER:-$(id -un)} ;; esac
    python3 -I $W/utdeck-profile.py folders "$WINEPREFIX" "$wuser" >> "$LOG" 2>&1
)

# The prefix's desktop mode (UTDECK_RESOLUTION, stage 4.6), as utdeck-compat sets it for Steam's games:
# setmode.exe (in UTDeck's compatibility tool) stores the mode, Wine applies it at each start of the
# prefix; run only when the setting changed, with the game's display environment (landscape).
want=${UTDECK_RESOLUTION:-native}
if [ "${2:-}" != --install ] && [ "$(cat "$WINEPREFIX/utdeck-resolution" 2>/dev/null || echo native)" != "$want" ]; then
    SETMODE=$HOME/.local/share/Steam/compatibilitytools.d/utdeck/setmode.exe
    if [ -f "$SETMODE" ]; then
        out=$(. $W/env.sh; WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} UTDECK_WAYLAND_LANDSCAPE=1 DISPLAY=               wine "$(winepath -w "$SETMODE" 2>/dev/null)" "$want" 2>&1 | grep -v -i wayland; wineserver -w)
        printf '  resolution %s: %s\n' "$want" "$out" >> "$LOG"
        case "$out" in *"set and stored"*) echo "$want" > "$WINEPREFIX/utdeck-resolution" ;; esac
    else
        echo "  resolution $want: setmode.exe not found ($SETMODE)" >> "$LOG"
    fi
fi

if [ "${2:-}" = --install ]; then
    SETUP=${3:?own-run.sh ID --install SETUP_EXE}
    # Installers' 32-bit code under FEX, as Steam's install scripts (utdeck-compat): Box64 stalls on
    # installers that unpack their own code while running (3DMark 11's InstallShield setup, stage 2.4
    # and again here: 6 minutes at 100 % of a core and no window). No pause: an installer unpacking
    # has no window in front yet. A game's utdeck-env can still set either.
    export HODLL=${HODLL:-libwow64fex.dll} UTDECK_PAUSE=${UTDECK_PAUSE:-0}
    echo "  installer: $SETUP (HODLL=$HODLL)" >> "$LOG"
    exec $W/game-run.sh "own-$ID" "$(dirname "$SETUP")" "$(basename "$SETUP")"
fi
DIR=$(conf DIR) EXE=$(conf EXE)
[ -n "$DIR" ] && [ -n "$EXE" ] || { echo "  game.conf has no DIR or EXE" >> "$LOG"; exit 1; }
# Launch options (GAME_ARGS, stage 4.6), as utdeck-compat adds them for Steam's games: words, so
# "-force-vulkan -screen-width 1600" becomes two arguments.
set -- "own-$ID" "$DIR" "$EXE"
if [ -n "${GAME_ARGS:-}" ]; then
    # shellcheck disable=SC2086 # words on purpose
    set -- "$@" $GAME_ARGS
    echo "  game arguments added: $GAME_ARGS" >> "$LOG"
fi
exec $W/game-run.sh "$@"
