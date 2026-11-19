# UTDeck: the environment for Wine on the phone, with our driver (route B). Installed as
# /home/phablet/utdeck-probe/wine/env.sh by tools/ut-probe/runtime-push.sh; for a shell on the phone:
#   . /home/phablet/utdeck-probe/wine/env.sh
#
# UTDECK_RUNTIME picks the Windows runtime:
#   utdeck    our own build (stage 3.1, tools/wine-build/build-runtime.sh), in wine/runtime/ (default)
#   hangover  Hangover 11.16's packages with our two .so files (stage 2), kept for comparison
P=/home/phablet/utdeck-probe
W=$P/wine
case "${UTDECK_RUNTIME:-utdeck}" in
    hangover) export PATH=$W/hangover-11.16/usr/bin:$PATH ;;
    *)        export PATH=$W/runtime/bin:$PATH ;;
esac
export WINEPREFIX=${WINEPREFIX:-$W/prefix}
export WINEDEBUG=${WINEDEBUG:--all}
# No Gecko (mshtml); Mono (mscoree) stays off unless a program needs .NET (then WINEDLLOVERRIDES=mshtml=).
export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-mscoree,mshtml=}
# Our driver, never the system's libhybris one: the Khronos loader and our Turnip for Vulkan,
# our Mesa (zink) as the EGL vendor.
export LD_LIBRARY_PATH=$P/zink/lib:$P/vk/lib
export VK_DRIVER_FILES=$P/zink/turnip_icd.json
export __EGL_VENDOR_LIBRARY_FILENAMES=$P/zink/egl_vendor.json
export MESA_LOADER_DRIVER_OVERRIDE=zink
export XDG_RUNTIME_DIR=/run/user/32011
unset GLIBC_TUNABLES LD_PRELOAD
