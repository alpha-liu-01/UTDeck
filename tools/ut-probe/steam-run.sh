#!/bin/sh
# UTDeck stage 3.S: start Valve's ARM64 Steam client on Ubuntu Touch (installed by steam-install.sh).
# Started from utdeck-steam.desktop, or from a shell for tests.
#
#   steam-run.sh [STEAM ARGUMENTS...]       e.g. steam-run.sh -gamepadui
#
# The client's own output goes to ~/utdeck-probe/steam/run-<time>.log; Steam's logs are in
# ~/.local/share/Steam/logs. Those can hold account names: they are read on the phone and never
# copied off it.
STEAM=$HOME/.local/share/Steam
LOG=$HOME/utdeck-probe/steam
mkdir -p "$LOG"
# Lomiri starts legacy apps with an X11 display; from a shell, its Xwayland is :1 on this phone.
export DISPLAY=${DISPLAY:-:1} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} LANG=${LANG:-C.UTF-8}
# Steam's own SDL3 is used (it has X11, which is all the client and its web helper need here; see
# the SDL notes below). UTDECK_STEAM_OWN_SDL=1 uses instead our build of Valve's SDL3 version with
# Wayland too (tools/steam-build/build-sdl3.sh, in Steam/utdeck-lib), kept for tests.
U=$STEAM/steamrtarm64
[ "${UTDECK_STEAM_OWN_SDL:-}" = 1 ] && U=$STEAM/utdeck-lib
# Any EGL a Steam part loads must not be Ubuntu Touch's system EGL, which is libhybris (Android's
# driver) and fails in these processes ("cannot allocate memory in static TLS block"), as Wine did
# in stage 2.1 (seen while SDL was on Wayland). Our own Mesa's EGL and our Turnip instead. Not
# MESA_LOADER_DRIVER_OVERRIDE=zink: the client's own X11 window uses the system's GLX, which that
# variable breaks ("failed to load driver: zink", then a fatal assert in vgui2).
P=/home/phablet/utdeck-probe
export LD_LIBRARY_PATH="$U:$STEAM/steamrtarm64:$STEAM/lib/aarch64-linux-gnu:$P/zink/lib:$P/vk/lib"
export VK_DRIVER_FILES=$P/zink/turnip_icd.json __EGL_VENDOR_LIBRARY_FILENAMES=$P/zink/egl_vendor.json
unset MESA_LOADER_DRIVER_OVERRIDE
# Vulkan's FIFO mode waits on Lomiri's late frame callbacks and can stall (stage 2.3): MAILBOX.
export MESA_VK_WSI_PRESENT_MODE=mailbox
unset GLIBC_TUNABLES LD_PRELOAD
# The client starts the runtime's steam-runtime-launcher-service by name ("not found" otherwise).
export PATH="$STEAM/steam-runtime-steamrt-arm64/pressure-vessel/bin:$PATH"
# SDL3 forces its Wayland driver on Ubuntu Touch (SDL_IsUbuntuTouch(): XDG_SESSION_DESKTOP=ubuntu-touch),
# whatever SDL_VIDEODRIVER says. Valve's SDL3 has no Wayland ("SDL_Init failed: wayland not
# available"), and the web helper's composer needs an X11 window (it reads SDL.window.x11.window and
# calls XChangeProperty). Without that variable SDL behaves as on any Linux: X11, like the client.
unset XDG_SESSION_DESKTOP
export SDL_VIDEODRIVER=${UTDECK_STEAM_SDL:-x11}
cd "$STEAM/steamrtarm64" || exit 1
# Exit code 42 means "updated myself, start me again" (as Valve's steam.sh handles it).
while :; do
    ./steam "$@" > "$LOG/run-$(date +%H%M%S).log" 2>&1
    rc=$?
    [ $rc -eq 42 ] || break
done
# Steam's shared memory segments stay in /dev/shm (RAM) when it is killed: 1.1 GB after a day of
# tests. Remove ours once no Steam process is left.
sleep 2
pgrep -u "$(id -u)" -f '[s]teamrtarm64/' > /dev/null || rm -f /dev/shm/u"$(id -u)"-Shm_*
exit $rc
