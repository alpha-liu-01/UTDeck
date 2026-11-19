#!/bin/sh
# UTDeck: take a screenshot of the phone's screen from the PC, through Mir's own screencast tool.
#
#   tools/ut-probe/screenshot.sh [OUT.png]       (default: build/screenshots/<time>.png)
#
# mirscreencast uses Android's graphics through libhybris, which from an adb shell needs the session's
# glibc tunable (glibc.rtld.optional_static_tls); without it: "cannot allocate memory in static TLS
# block". The picture is 1080x2400 RGBA, saved at half size. Screenshots can show personal data (a
# Steam library, notifications): they stay in build/, which git ignores.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$REPO_DIR/build/screenshots/$(date +%H%M%S).png}
mkdir -p "$(dirname "$OUT")"
adb shell 'export XDG_RUNTIME_DIR=/run/user/32011 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/32011/bus
    T=$(systemctl --user show-environment | grep "^GLIBC_TUNABLES" | cut -d= -f2-)
    rm -f /tmp/utdeck-shot.rgba
    GLIBC_TUNABLES=$T timeout 10 mirscreencast -m /run/mir_socket -n 1 -f /tmp/utdeck-shot.rgba > /dev/null 2>&1'
adb pull -q /tmp/utdeck-shot.rgba "$OUT.rgba" > /dev/null
adb shell rm -f /tmp/utdeck-shot.rgba
python3 -c "
from PIL import Image
Image.frombytes('RGBA', (1080, 2400), open('$OUT.rgba', 'rb').read()).convert('RGB').resize((540, 1200)).save('$OUT')"
rm -f "$OUT.rgba"
echo "$OUT"
