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
C=$S/compatibilitytools.d/utdeck CP=$S/compatibilitytools.d/utdeck-proton
mkdir -p "$P/steam" "$P/wine" "$S/utdeck-lib" "$C" "$CP"

# The Steam client (stage 3.S): its start, install and service scripts, the drawer entries, the
# hidden screen's window tool, and SDL3 with Wayland (tools/steam-build/build-sdl3.sh).
for f in steam-run.sh steam-install.sh steam-service.sh steam-library.py; do cp "$T/$f" "$P/steam/"; done
cp "$T/xwin.py" "$P/"
cp "$REPO_DIR/build/steam/libSDL3.so.0" "$S/utdeck-lib/"

# UTDeck's compatibility tools: the default runtime's and Proton 11's (stage 3.3).
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat set-default.py steam-prefix.reg; do
    cp "$T/steam-compat/$f" "$C/"
done
cp "$REPO_DIR/build/win/setmode.exe" "$C/"   # tools/ut-probe/win/build.sh
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat; do cp "$T/steam-compat/proton/$f" "$CP/"; done

# The Wine runtime's scripts and defaults (runtime-push.sh's list); env.sh is wine-env.sh.
cp "$T/wine-env.sh" "$P/wine/env.sh"
# The Controllers page's check in Wine (stage 4.4): gptest.exe (tools/ut-probe/win/build.sh).
mkdir -p "$P/wine/padprobe"
cp "$REPO_DIR/build/win/gptest.exe" "$P/wine/padprobe/"
for f in prefix-dlls.sh get-runtime.sh runtimes.list defaults.env game-run.sh utdeck-boost.py \
         uclamp-show.py utdeck-pause.py utdeck-setupwatch.py lifecycle-watch.sh controllers.reg padlayout.py own-run.sh gamecheck.py; do
    cp "$T/$f" "$P/wine/"
done

chmod 755 "$P"/steam/*.sh "$P/steam/steam-library.py" "$P/xwin.py" "$P"/wine/*.sh \
    "$C/utdeck-compat" "$C/set-default.py" "$CP/utdeck-compat"
