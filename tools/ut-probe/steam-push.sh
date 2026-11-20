#!/bin/sh
# UTDeck stage 3.S: copy UTDeck's pieces for the Steam client to the phone: the start script, the
# install script, the service script, SDL3 with Wayland (tools/steam-build/build-sdl3.sh) into
# Steam's utdeck-lib, and UTDeck's compatibility tools (steam-compat/) into compatibilitytools.d/utdeck
# and, for Proton 11's Wine base, compatibilitytools.d/utdeck-proton (steam-compat/proton/).
# Steam reads compatibility tools when it starts: restart it after a change to the .vdf files.
#
# Everything goes over in one archive, with one push and one shell: the phone's adbd crashed (SEGV)
# three times in a row during the fifteen quick pushes this script used to make, and after such a
# crash it does not attach to USB again until the cable is plugged in again (stage 3.3).
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# The archive holds the files at their places under /home/phablet.
P=utdeck-probe/steam S=.local/share/Steam C=$S/compatibilitytools.d/utdeck CP=$S/compatibilitytools.d/utdeck-proton
mkdir -p "$T/$P" "$T/$S/utdeck-lib" "$T/$C" "$T/$CP"
for f in steam-run.sh steam-install.sh steam-service.sh steam-library.py; do cp "$REPO_DIR/tools/ut-probe/$f" "$T/$P/"; done
cp "$REPO_DIR/tools/ut-probe/xwin.py" "$T/utdeck-probe/"   # used by steam-service.sh
cp "$REPO_DIR/build/steam/libSDL3.so.0" "$T/$S/utdeck-lib/"
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat set-default.py steam-prefix.reg; do
    cp "$REPO_DIR/tools/ut-probe/steam-compat/$f" "$T/$C/"
done
cp "$REPO_DIR/build/win/setmode.exe" "$T/$C/"   # tools/ut-probe/win/build.sh
# The second tool (stage 3.3), "UTDeck runtime (Proton 11)": its script calls the first one's.
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat; do
    cp "$REPO_DIR/tools/ut-probe/steam-compat/proton/$f" "$T/$CP/"
done
chmod +x "$T/$P"/*.sh "$T/$P/steam-library.py" "$T/$C/utdeck-compat" "$T/$C/set-default.py" "$T/$CP/utdeck-compat"
tar -C "$T" -czf "$T/steam-push.tar.gz" utdeck-probe .local
adb push -q "$T/steam-push.tar.gz" /home/phablet/utdeck-probe/steam-push.tar.gz > /dev/null
# libSDL3 is removed first: a running Steam may hold the old one open.
adb shell "cd /home/phablet && rm -f $S/utdeck-lib/libSDL3.so.0 && tar -xzf utdeck-probe/steam-push.tar.gz --no-same-owner &&
    rm utdeck-probe/steam-push.tar.gz && sha256sum $S/utdeck-lib/libSDL3.so.0"
grep sha256 "$REPO_DIR/build/steam/BUILD-INFO"
