#!/bin/sh
# UTDeck's click launcher (stage 4.1), after the Dolphin port's (dolphin-ut, ut/click/launcher.sh):
# Qt 6's Wayland shell integration bundled in the click (the phone has none), and on
# Ubuntu Touch 24.04-1.x the whole Qt 6 runtime (tools/app-build/build-click.sh, CLICK_UT).
# The output goes to the session journal:
#   journalctl --user -u 'lomiri-app-launch--application-click--utdeck.alphaliu01*'
app=${0%/*}
lib="$app/lib/aarch64-linux-gnu"
export QT_QPA_PLATFORM=wayland
export QT_PLUGIN_PATH="$lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -d "$lib/qt6/qml" ]; then
  export QML_IMPORT_PATH="$lib/qt6/qml"
  export QML2_IMPORT_PATH="$lib/qt6/qml"
fi
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1
# Lomiri's Wayland socket by its absolute path (the Dolphin port's stage 5): libwayland takes an
# absolute WAYLAND_DISPLAY; the folder comes from the session bus address.
runtime=${DBUS_SESSION_BUS_ADDRESS#unix:path=}
runtime=${runtime%/bus}
display="$runtime/${WAYLAND_DISPLAY:-wayland-0}"
[ -S "$display" ] && export WAYLAND_DISPLAY="$display"
exec "$app/utdeck" "$@"
