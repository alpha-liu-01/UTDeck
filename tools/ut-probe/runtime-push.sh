#!/bin/sh
# UTDeck stage 3.1: copy the runtime built by tools/wine-build/build-runtime.sh to the phone, over adb.
#
#   tools/ut-probe/runtime-push.sh
#
# Unpacks build/runtime/utdeck-runtime-<version>.tar.xz into wine/runtime-<version>/ on the phone and
# points wine/runtime at it (older versions stay, for going back); installs env.sh (from
# wine-env.sh) and prefix-dlls.sh. Does not touch any prefix.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
W=/home/phablet/utdeck-probe/wine
TAR=$(ls "$REPO_DIR"/build/runtime/utdeck-runtime-*.tar.xz | tail -1)
VER=$(basename "$TAR" .tar.xz | sed 's/^utdeck-runtime-//')
adb push -q "$TAR" "$W/utdeck-runtime.tar.xz" > /dev/null
adb push -q "$REPO_DIR/tools/ut-probe/wine-env.sh" "$W/env.sh" > /dev/null
adb push -q "$REPO_DIR/tools/ut-probe/prefix-dlls.sh" "$W/prefix-dlls.sh" > /dev/null
# This phone's adb shell always exits 0: the result is read from the output.
adb shell "set -e; cd $W; rm -rf runtime-$VER.new; mkdir runtime-$VER.new
    tar -C runtime-$VER.new -xJf utdeck-runtime.tar.xz; rm utdeck-runtime.tar.xz
    rm -rf runtime-$VER; mv runtime-$VER.new/utdeck-runtime runtime-$VER; rmdir runtime-$VER.new
    ln -sfn runtime-$VER runtime; chmod +x prefix-dlls.sh
    echo \"installed: \$(head -1 runtime/BUILD-INFO), \$(du -sh runtime-$VER | cut -f1)\""
