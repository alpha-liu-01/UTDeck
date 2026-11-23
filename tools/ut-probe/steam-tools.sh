#!/bin/sh
# UTDeck stage 4.7: Steam's side brought up to UTDeck's current compatibility tool, with Steam stopped.
#
#   steam-tools.sh             only while Steam does not run (steam-service.sh start runs it first)
#   steam-tools.sh --restart   if Steam runs and something is left to do: Steam stopped for it (not
#                              while a game runs), started again afterwards (the setup's files step,
#                              after an app update)
#
# Until stage 4.7 UTDeck had two tools: "utdeck" (Hangover's Wine base) and "utdeck-proton" (Proton
# 11's base, compatibilitytools.d/utdeck-proton). Steam brings a Windows game's cloud saves into its
# prefix only for a tool whose name contains "proton" (stage4.7-steam-tasks.md), so there is now one
# tool, named "utdeck-proton", in the folder compatibilitytools.d/utdeck; a game's Wine base is its
# UTDECK_RUNTIME (utdeck-env, the app's settings page). Here, each part only if not done yet:
#  1. the games mapped to the old second tool get UTDECK_RUNTIME=proton (unless their utdeck-env sets a
#     runtime already), and its folder goes (two tools of one name would confuse Steam);
#  2. every mapping to "utdeck" (the default, app "0", and each game's) becomes "utdeck-proton"
#     (config.vdf, which Steam rewrites while it runs: hence Steam stopped);
#  3. each game's Windows user folder is laid out once (utdeck-profile.py migrate: the one folder
#     Steam Cloud uses, shared by both Wine bases; the cloud's saves winning over copies made on the
#     phone, which are kept aside);
#  4. what Steam's shader pre-caching downloaded goes (Steam now runs with -noshaders): per game in
#     steamapps/shadercache/<appid>, the shader collections compiled for SwiftShader (fozpipelinesv6),
#     the re-encoded videos and sounds (transcoded_*.foz, fozmediav1) and the empty nvidiav1 and
#     DXVK_state_cache folders; Turnip's own cache (mesa_shader_cache_sf) stays.
# No account data is read or printed (config.vdf only through set-default.py's mapping block).
set -u
S=$HOME/.local/share/Steam
TOOL=$S/compatibilitytools.d/utdeck
OLD=$S/compatibilitytools.d/utdeck-proton
SERVICE=$HOME/utdeck-probe/steam/steam-service.sh
SD="python3 -I $TOOL/set-default.py"
log() { echo "steam-tools: $*"; }
steam_runs() { pgrep -x -u "$(id -u)" steam > /dev/null; }

old_tool() { [ -f "$OLD/compatibilitytool.vdf" ] && grep -q 'UTDeck runtime (Proton 11)' "$OLD/compatibilitytool.vdf"; }
profiles_left() {
    for d in "$S"/steamapps/compatdata/[0-9]*; do
        [ "${d##*/}" = 0 ] && continue
        [ -d "$d/pfx/drive_c/users" ] || [ -d "$d/pfx-proton/drive_c/users" ] || continue
        [ "$(cat "$d/utdeck-profile" 2>/dev/null)" = 1 ] || return 0
    done
    return 1
}
precache_left() { ls -d "$S"/steamapps/shadercache/*/fozpipelinesv6 "$S"/steamapps/shadercache/*/transcoded_*.foz "$S"/steamapps/shadercache/*/fozmediav1 > /dev/null 2>&1; }
needed() { old_tool || [ -n "$($SD --apps utdeck 2>/dev/null)" ] || profiles_left || precache_left; }

# Nothing to do without Steam set up, or with an older tool than this script expects.
[ -f "$S/config/config.vdf" ] && [ -f "$TOOL/utdeck-profile.py" ] || exit 0
needed || exit 0
restart=
if steam_runs; then
    [ "${1:-}" = --restart ] || { log "Steam is running: left for its next start"; exit 0; }
    [ -e "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/utdeck/game" ] && { log "a game is running: left for Steam's next start"; exit 0; }
    log "stopping Steam for it"
    sh "$SERVICE" stop > /dev/null
    restart=1
fi

# 1. The old second tool's games keep Proton's base; then its folder goes.
if old_tool; then
    for app in $($SD --apps utdeck-proton 2>/dev/null); do
        [ "$app" = 0 ] && continue
        env=$S/steamapps/compatdata/$app/utdeck-env
        if grep -q '^UTDECK_RUNTIME=' "$env" 2>/dev/null; then
            log "app $app: keeps its own runtime setting ($(grep '^UTDECK_RUNTIME=' "$env" | tail -1))"
        else
            mkdir -p "${env%/*}"
            printf '# Stage 4.7: this game used the old tool "UTDeck runtime (Proton 11)"\nUTDECK_RUNTIME=proton\n' >> "$env"
            log "app $app: UTDECK_RUNTIME=proton (it used the old Proton 11 tool)"
        fi
    done
    rm -f "$OLD/compatibilitytool.vdf" "$OLD/toolmanifest.vdf" "$OLD/utdeck-compat"
    rmdir "$OLD" 2>/dev/null
    log "the old second tool removed"
fi

# 2. Mappings to the old name.
$SD --rename utdeck utdeck-proton | sed 's/^/steam-tools: /'

# 3. The Windows user folders.
python3 -I "$TOOL/utdeck-profile.py" migrate "$S" | sed 's/^utdeck-profile: /steam-tools: /'

# 4. Steam's pre-caching downloads (only these names, only in shadercache/<digits>).
for d in "$S"/steamapps/shadercache/[0-9]*; do
    [ -d "$d" ] || continue
    before=$(du -sm "$d" | cut -f1)
    rm -rf "$d/fozpipelinesv6" "$d/fozmediav1" "$d/nvidiav1" "$d/DXVK_state_cache" "$d"/transcoded_*.foz
    after=$(du -sm "$d" | cut -f1)
    [ "$before" != "$after" ] && log "app ${d##*/}: Steam's pre-caching files removed ($before MB -> $after MB; Turnip's cache kept)"
done

[ -n "$restart" ] && { log "starting Steam again"; sh "$SERVICE" start > /dev/null; }
exit 0
