#!/bin/sh
# UTDeck stage 4.2: lay out every UTDeck file a phone's home folder needs, at its place under
# /home/phablet, into DEST (DEST/utdeck-probe/..., DEST/.local/share/Steam/...):
#
#   tools/ut-probe/home-files.sh DEST
#
# The same files steam-push.sh and runtime-push.sh push over adb (the Steam client's scripts, SDL3,
# UTDeck's compatibility tools, the Wine runtime's scripts and defaults), for the app's click:
# build-click.sh puts the tree in the click, and the app's setup (utdeck-setup.sh) copies it into
# the home folder, so a phone needs no PC. Not the runtime itself (get-runtime.sh downloads it) nor
# Xvfb or Steam (utdeck-setup.sh downloads them).
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEST=${1:?usage: home-files.sh DEST}
T=$REPO_DIR/tools/ut-probe
P=$DEST/utdeck-probe S=$DEST/.local/share/Steam
C=$S/compatibilitytools.d/utdeck
mkdir -p "$P/steam" "$P/wine" "$S/utdeck-lib" "$C"

# The Steam client (stage 3.S): its start, install and service scripts, the drawer entries, the
# hidden screen's window tool, and SDL3 with Wayland (tools/steam-build/build-sdl3.sh).
for f in steam-run.sh steam-install.sh steam-service.sh steam-library.py steam-game.sh steam-tools.sh; do cp "$T/$f" "$P/steam/"; done
cp "$T/xwin.py" "$T/appinfo.py" "$P/"
cp "$REPO_DIR/build/steam/libSDL3.so.0" "$S/utdeck-lib/"

# UTDeck's compatibility tool, Steam's "utdeck-proton" (stage 4.7: one tool for both Wine bases; the
# folder keeps its name "utdeck"). The old second tool's folder (utdeck-proton) is removed on the
# phone by steam-tools.sh, after its games got their runtime setting.
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat set-default.py steam-prefix.reg utdeck-profile.py; do
    cp "$T/steam-compat/$f" "$C/"
done
cp "$REPO_DIR/build/win/setmode.exe" "$C/"   # tools/ut-probe/win/build.sh

# The Wine runtime's scripts and defaults (runtime-push.sh's list); env.sh is wine-env.sh.
cp "$T/wine-env.sh" "$P/wine/env.sh"
# The Controllers page's check in Wine (stage 4.4): gptest.exe (tools/ut-probe/win/build.sh).
mkdir -p "$P/wine/padprobe"
cp "$REPO_DIR/build/win/gptest.exe" "$P/wine/padprobe/"
for f in prefix-dlls.sh get-runtime.sh runtimes.list defaults.env game-run.sh utdeck-boost.py \
         uclamp-show.py utdeck-pause.py utdeck-awake.py utdeck-setupwatch.py lifecycle-watch.sh controllers.reg padlayout.py own-run.sh gamecheck.py; do
    cp "$T/$f" "$P/wine/"
done
cp "$T/steam-compat/utdeck-profile.py" "$P/wine/"   # own-run.sh's user folders (stage 4.7)

chmod 755 "$P"/steam/*.sh "$P/steam/steam-library.py" "$P/xwin.py" "$P"/wine/*.sh \
    "$C/utdeck-compat" "$C/set-default.py" "$C/utdeck-profile.py"
