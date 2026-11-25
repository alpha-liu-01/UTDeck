#!/bin/sh
# UTDeck stage 3.1: copy the runtime built by tools/wine-build/build-runtime.sh to the phone, over adb.
#
#   tools/ut-probe/runtime-push.sh          the default runtime (Hangover's Wine 11.16 base)
#   tools/ut-probe/runtime-push.sh proton   the second one, on Proton 11's Wine (WINE_FLAVOR=proton)
#
# Default: unpacks build/runtime/utdeck-runtime-<version>.tar.xz into wine/runtime-<version>/ on the
# phone and points wine/runtime at it (older versions stay, for going back); installs env.sh (from
# wine-env.sh), prefix-dlls.sh, get-runtime.sh, game-run.sh and defaults.env (stage 3.4). Proton's: pushes
# build/runtime-proton/utdeck-runtime-proton<version>.tar.xz with its checksum and installs it with
# get-runtime.sh, the same script that downloads it on request (stage 3.3, step 5.5). Does not
# touch any prefix.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
W=/home/phablet/utdeck-probe/wine
# Free space first (stage 4.3: stone's disk ran full in the middle of a push, which left a half-unpacked
# runtime and an empty steam-run.sh behind). The archive plus the unpacked runtime: about 1.5 GB.
free=$(adb shell "df -m /home/phablet | awk 'NR == 2 {print \$4}'" | tr -d '\r\n ')
[ "${free:-0}" -ge 1600 ] || { echo "not enough space on the phone: ${free:-?} MB free, 1600 MB needed; nothing was changed" >&2; exit 1; }
adb push -q "$REPO_DIR/tools/ut-probe/wine-env.sh" "$W/env.sh" > /dev/null
adb push -q "$REPO_DIR/tools/ut-probe/prefix-dlls.sh" "$W/prefix-dlls.sh" > /dev/null
adb push -q "$REPO_DIR/tools/ut-probe/get-runtime.sh" "$REPO_DIR/tools/ut-probe/runtimes.list" "$REPO_DIR/tools/ut-probe/defaults.env" "$REPO_DIR/tools/ut-probe/game-run.sh" "$REPO_DIR/tools/ut-probe/utdeck-boost.py" "$REPO_DIR/tools/ut-probe/uclamp-show.py" "$REPO_DIR/tools/ut-probe/utdeck-pause.py" "$REPO_DIR/tools/ut-probe/utdeck-awake.py" "$REPO_DIR/tools/ut-probe/utdeck-setupwatch.py" "$REPO_DIR/tools/ut-probe/lifecycle-watch.sh" "$REPO_DIR/tools/ut-probe/controllers.reg" "$REPO_DIR/tools/ut-probe/padlayout.py" "$REPO_DIR/tools/ut-probe/dotnet32.py" "$REPO_DIR/tools/ut-probe/own-run.sh" "$REPO_DIR/tools/ut-probe/steam-compat/utdeck-profile.py" "$W/" > /dev/null
adb shell "chmod +x $W/prefix-dlls.sh $W/get-runtime.sh $W/game-run.sh $W/own-run.sh"

if [ "${1:-}" = proton ]; then
    TAR=$(ls "$REPO_DIR"/build/runtime-proton/utdeck-runtime-proton*.tar.xz | tail -1)
    sha256sum "$TAR" | sed 's|  .*||' > "$TAR.sha256"
    adb push -q "$TAR" "$W/utdeck-runtime-proton.tar.xz" > /dev/null
    adb push -q "$TAR.sha256" "$W/utdeck-runtime-proton.tar.xz.sha256" > /dev/null
    rm -f "$TAR.sha256"
    # get-runtime.sh names the folder after the file: give it the real name on the phone.
    N=$(basename "$TAR")
    adb shell "cd $W; mv utdeck-runtime-proton.tar.xz $N; mv utdeck-runtime-proton.tar.xz.sha256 $N.sha256
        sh get-runtime.sh proton $W/$N && rm -f $N $N.sha256"
    exit 0
fi

TAR=$(ls "$REPO_DIR"/build/runtime/utdeck-runtime-*.tar.xz | tail -1)
VER=$(basename "$TAR" .tar.xz | sed 's/^utdeck-runtime-//')
adb push -q "$TAR" "$W/utdeck-runtime.tar.xz" > /dev/null
# This phone's adb shell always exits 0: the result is read from the output.
adb shell "set -e; cd $W; rm -rf runtime-$VER.new; mkdir runtime-$VER.new
    tar -C runtime-$VER.new -xJf utdeck-runtime.tar.xz; rm utdeck-runtime.tar.xz
    rm -rf runtime-$VER; mv runtime-$VER.new/utdeck-runtime runtime-$VER; rmdir runtime-$VER.new
    ln -sfn runtime-$VER runtime
    echo \"installed: \$(head -1 runtime/BUILD-INFO), \$(du -sh runtime-$VER | cut -f1)\""
