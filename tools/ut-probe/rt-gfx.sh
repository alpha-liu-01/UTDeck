#!/bin/sh
# UTDeck stage 3.1: the stage 2.3/2.4 graphics tests on one runtime, for comparing our runtime with
# Hangover's. Runs with the screen off too (frames are still presented, stage 2.4).
#
#   rt-gfx.sh utdeck|hangover|proton [RUNTIME_DIR]   -> rt-gfx-<runtime>-<time>.log in the wine folder
#
# RUNTIME_DIR (a folder in the wine folder, e.g. runtime-p13) runs a test build instead of the
# runtime the kind normally picks; the prefix still follows the kind.
#
# Uses a prefix of its own (prefix-<runtime>-gfx, created the first time, with DXVK and, on our
# runtime, VKD3D-Proton), then: Vulkan (x86-64 and ARM64), Direct3D 11 (DXVK; windowed, fullscreen,
# and a display mode change, which needs patches 0005 and 0006), Direct3D 9 (DXVK, 32 and 64-bit),
# Direct3D 12 (VKD3D-Proton, our runtime only), sound (a quiet tone; PulseAudio is asked which
# streams are playing meanwhile) and the gamepad list (XInput, winmm).
set -u
export UTDECK_RUNTIME=$1
W=/home/phablet/utdeck-probe/wine
export WINEPREFIX=$W/prefix-$1-gfx
. $W/env.sh
[ -n "${2:-}" ] && export PATH=$W/$2/bin:$PATH
export WAYLAND_DISPLAY=wayland-0 MESA_VK_WSI_PRESENT_MODE=mailbox
# Their logs crowd out the results; stage 3.4 measures them on (ab-env.sh), so a caller's value wins.
export DXVK_LOG_LEVEL=${DXVK_LOG_LEVEL:-none} VKD3D_DEBUG=${VKD3D_DEBUG:-none} VKD3D_SHADER_DEBUG=${VKD3D_SHADER_DEBUG:-none}
unset DISPLAY
T=$W/test
LOG=$W/rt-gfx-$1-$(date +%H%M%S).log
exec > "$LOG" 2>&1
echo "runtime: $1${2:+ ($2)}, $(command -v wine), $(wine --version)"
if [ ! -d "$WINEPREFIX" ]; then
    wineboot -i > /dev/null 2>&1; wineserver -w
    if [ "$1" = hangover ]; then
        cp $W/dxvk-v2.7.1/arm64ec/*.dll "$WINEPREFIX/drive_c/windows/system32/"
        cp $W/dxvk-v2.7.1/x32/*.dll "$WINEPREFIX/drive_c/windows/syswow64/"
    else
        $W/prefix-dlls.sh
    fi
fi
DXVK="d3d11,dxgi,d3d10core,d3d9,d3d8=n;mscoree,mshtml="
run() {   # run LABEL OVERRIDES PROGRAM ARGS...
    l=$1 o=$2; shift 2
    echo "--- $l"
    WINEDLLOVERRIDES="$o" WINEDEBUG=${RT_WINEDEBUG:-err+all,fixme-all} timeout 60 wine "$@" > /tmp/rt-gfx.out 2>&1
    rc=$?
    # Wine's start-up notes about optional Wayland protocols Lomiri lacks are left out (stage 2.2),
    # and so are DXVK's and VKD3D-Proton's own log lines: with their logs on (stage 3.4) they filled
    # the 25 lines and pushed the results out.
    grep -v -E "err:(waylanddrv|cursor):|err:system:NtUserChangeDisplaySettings|err:explorer:initialize_display_settings|err:wgl:internal_context_create|^(info|warn|err|fixme): |:(info|warn|err|fixme|trace):vkd3d|^ *$" \
        /tmp/rt-gfx.out | head -25
    echo "exit: $rc"
    wineserver -w
}
cd $T
run "vktest x86-64, fullscreen" "mscoree,mshtml=" vktest-x86_64.exe --frames 600 --fullscreen
run "vktest ARM64, window" "mscoree,mshtml=" vktest-arm64.exe --frames 600
run "d3d11test DXVK, window" "$DXVK" d3d11test.exe --frames 600
run "d3d11test DXVK, fullscreen" "$DXVK" d3d11test.exe --frames 600 --fullscreen
# Mode changes need the landscape monitor (patch 0003), as 3DMark 11 is started; they exercise
# patches 0005 (no crash) and 0006 (the smaller picture scaled to the screen).
export UTDECK_WAYLAND_LANDSCAPE=1
run "d3d11test DXVK, mode 1280x720 (landscape monitor)" "$DXVK" d3d11test.exe --frames 300 --mode 1280x720
run "d3d11test DXVK, mode 1920x1080 (landscape monitor)" "$DXVK" d3d11test.exe --frames 300 --mode 1920x1080
unset UTDECK_WAYLAND_LANDSCAPE
run "d3d9test x86 DXVK, fullscreen" "$DXVK" d3d9test-i686.exe --frames 600 --fullscreen
run "d3d9test x86-64 DXVK, fullscreen" "$DXVK" d3d9test-x86_64.exe --frames 600 --fullscreen
if [ "$1" != hangover ]; then
    run "d3d12test VKD3D-Proton, window" "d3d12,d3d12core=n;$DXVK" d3d12test.exe --frames 600
    run "d3d12test VKD3D-Proton, fullscreen" "d3d12,d3d12core=n;$DXVK" d3d12test.exe --frames 600 --fullscreen
fi
echo "--- sndtest (PulseAudio's streams while it plays)"
WINEDLLOVERRIDES="mscoree,mshtml=" timeout 30 wine sndtest.exe 3 > /tmp/rt-snd.out 2>&1 &
sleep 4
pactl list sink-inputs 2>&1 | grep -E "^Sink Input|application.name|application.process.binary|sample_spec|Sample Specification" | sed 's/^/  pulse: /'
wait
grep -v -E "err:(waylanddrv|cursor):|err:system:NtUserChangeDisplaySettings|err:explorer:initialize_display_settings|^ *$" /tmp/rt-snd.out | head -10
wineserver -w
run "gptest (lists controllers)" "mscoree,mshtml=" gptest.exe 1
wineserver -k 2>/dev/null
echo "done $(date +%T)"
