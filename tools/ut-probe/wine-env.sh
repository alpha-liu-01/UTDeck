# UTDeck: the environment for Wine on the phone, with our driver (route B). Installed as
# /home/phablet/utdeck-probe/wine/env.sh by tools/ut-probe/runtime-push.sh; for a shell on the phone:
#   . /home/phablet/utdeck-probe/wine/env.sh
#
# UTDECK_RUNTIME picks the Windows runtime:
#   utdeck    our own build (stage 3.1, tools/wine-build/build-runtime.sh), in wine/runtime/ (default)
#   hangover  Hangover 11.16's packages with our two .so files (stage 2), kept for comparison
#   proton    our runtime built on Proton 11's Wine (stage 3.3, WINE_FLAVOR=proton), in wine/runtime-proton/
P=/home/phablet/utdeck-probe
W=$P/wine
case "${UTDECK_RUNTIME:-utdeck}" in
    hangover) export PATH=$W/hangover-11.16/usr/bin:$PATH ;;
    proton)   export PATH=$W/runtime-proton/bin:$PATH ;;
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
# Games' videos and sounds: Wine plays them through the phone's GStreamer, plus the runtime's own
# plugins (FFmpeg's decoders, the WMV/WMA reader). GStreamer keeps a list of the plugins it found
# (the registry); with a plugin folder the system's programs do not have, ours lives in its own file,
# so the Media Player's list is not rebuilt each time a game starts, and the other way round.
export GST_PLUGIN_PATH=$W/runtime/lib/gstreamer-1.0
export GST_REGISTRY_1_0=$HOME/.cache/utdeck/gstreamer-registry.bin
# Sound: the volume keys (ayatana-indicator-sound) set the volume of a media role, which PulseAudio's
# stream-restore applies to every stream with that role. Wine gives its streams no role, so games
# played at a fixed 0 dB that no key could change. As "multimedia" they follow the media volume,
# like the Media Player (default -8 dB, /etc/pulse/touch-stream-restore.table).
export PULSE_PROP='media.role=multimedia'
unset GLIBC_TUNABLES LD_PRELOAD
