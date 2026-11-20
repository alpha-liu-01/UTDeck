#!/bin/sh
# UTDeck stage 3.S: copy UTDeck's pieces for the Steam client to the phone: the start script, the
# install script, the service script, SDL3 with Wayland (tools/steam-build/build-sdl3.sh) into
# Steam's utdeck-lib, and UTDeck's compatibility tools (steam-compat/) into compatibilitytools.d/utdeck
# and, for Proton 11's Wine base, compatibilitytools.d/utdeck-proton (steam-compat/proton/).
# Steam reads compatibility tools when it starts: restart it after a change to the .vdf files.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
P=/home/phablet/utdeck-probe/steam
adb shell "mkdir -p $P /home/phablet/.local/share/Steam/utdeck-lib; rm -f /home/phablet/.local/share/Steam/utdeck-lib/libSDL3.so.0"
for f in steam-run.sh steam-install.sh steam-service.sh; do adb push -q "$REPO_DIR/tools/ut-probe/$f" "$P/" > /dev/null; done
adb push -q "$REPO_DIR/tools/ut-probe/xwin.py" /home/phablet/utdeck-probe/ > /dev/null   # used by steam-service.sh
adb push -q "$REPO_DIR/build/steam/libSDL3.so.0" /home/phablet/.local/share/Steam/utdeck-lib/ > /dev/null
C=/home/phablet/.local/share/Steam/compatibilitytools.d/utdeck
adb shell "mkdir -p $C"
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat set-default.py steam-prefix.reg; do
    adb push -q "$REPO_DIR/tools/ut-probe/steam-compat/$f" "$C/" > /dev/null
done
adb push -q "$REPO_DIR/build/win/setmode.exe" "$C/" > /dev/null   # tools/ut-probe/win/build.sh
# The second tool (stage 3.3), "UTDeck runtime (Proton 11)": its script calls the first one's.
CP=/home/phablet/.local/share/Steam/compatibilitytools.d/utdeck-proton
adb shell "mkdir -p $CP"
for f in compatibilitytool.vdf toolmanifest.vdf utdeck-compat; do
    adb push -q "$REPO_DIR/tools/ut-probe/steam-compat/proton/$f" "$CP/" > /dev/null
done
adb shell "chmod +x $P/*.sh $C/utdeck-compat $C/set-default.py $CP/utdeck-compat; sha256sum /home/phablet/.local/share/Steam/utdeck-lib/libSDL3.so.0"
grep sha256 "$REPO_DIR/build/steam/BUILD-INFO"
