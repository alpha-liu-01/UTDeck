#!/bin/sh
# UTDeck stage 3.S: Valve's ARM64 Linux Steam client on Ubuntu Touch, in the user's home
# (~/.local/share/Steam, as Steam expects), nothing installed system-wide. The method of ARMDeck's
# userspace/steam/stageA_steam.sh (from pocknix-os), without a container, with wget.
#
#   steam-install.sh 1     download and check the Steam Runtime 3 (arm64) and the client
#   steam-install.sh 2     let the client update itself once, on Lomiri's Xwayland, then exit, and
#                          link what the updated client needs from the runtime (also: steam-install.sh links)
#
# Sources: the client's manifest on client-update.steamstatic.com (the archive's name ends with
# its SHA-1, checked), the runtime from repo.steampowered.com (checked against its SHA256SUMS).
# Output goes to ~/utdeck-probe/steam/; no account data is involved in either phase.
set -eu
STEAM=$HOME/.local/share/Steam
DOT=$HOME/.steam
CHANNEL=${STEAM_CHANNEL:-steamdeck_stable}
MANIFEST=steam_client_${CHANNEL}_linuxarm64
RT_BASE=https://repo.steampowered.com/steamrt3c/images/latest-public-beta
CDN=https://client-update.steamstatic.com
LOG=$HOME/utdeck-probe/steam
mkdir -p "$STEAM" "$LOG"
cd "$STEAM"

phase1() {
    if [ ! -d steam-runtime-steamrt-arm64 ]; then
        wget -q -c --tries=20 -O steam-runtime-steamrt-arm64.tar.xz "$RT_BASE/steam-runtime-steamrt-arm64.tar.xz"
        wget -q -O SHA256SUMS.rt "$RT_BASE/SHA256SUMS"
        grep ' \*steam-runtime-steamrt-arm64.tar.xz$' SHA256SUMS.rt | sha256sum -c -
        tar xf steam-runtime-steamrt-arm64.tar.xz
        rm -f steam-runtime-steamrt-arm64.tar.xz SHA256SUMS.rt
        # steamwebhelper needs libibus (pocknix's fix)
        target=$(ls steam-runtime-steamrt-arm64/steamrt3c_platform_*/files/lib/aarch64-linux-gnu/libibus-1.0.so.5.* | head -n1)
        mkdir -p lib/aarch64-linux-gnu
        ln -sfn "../../$target" lib/aarch64-linux-gnu/libibus-1.0.so.5
    fi
    if [ ! -d steamrtarm64 ]; then
        f=$(wget -q -O - "$CDN/$MANIFEST" | tr -c '[:print:]' '\n' | grep -oE 'bins_linuxarm64_linuxarm64\.zip\.[0-9a-f]{40}' | head -n1)
        [ -n "$f" ] || { echo "the client archive is not in the manifest" >&2; exit 1; }
        wget -q -c --tries=20 -O linuxarm64.zip "$CDN/$f"
        want=${f##*.}
        got=$(sha1sum linuxarm64.zip | cut -d' ' -f1)
        echo "client archive $f: sha1 expected $want, got $got"
        [ "$want" = "$got" ] || { echo "SHA-1 differs, not continuing" >&2; exit 1; }
        unzip -o -q linuxarm64.zip
        rm -f linuxarm64.zip
        # The archive names a few entries with Windows separators ("steamrtarm64\libs\libcurl.so",
        # symbolic links); unzip makes them files in this folder. Put them where they belong.
        for e in steamrtarm64\\*; do
            [ -e "$e" ] || [ -L "$e" ] || continue
            if [ -d "$e" ] && [ ! -L "$e" ]; then rmdir "$e" 2>/dev/null || true; continue; fi
            d=$(printf '%s' "$e" | tr '\\' '/')
            mkdir -p "$(dirname "$d")"
            mv "$e" "$d"
        done
        chmod +x steamrtarm64/steam
        mkdir -p package
        echo "$CHANNEL" > package/beta
        mkdir -p "$DOT"
        for l in steam:. root:. sdk32:linux32 sdk64:linux64 sdkarm64:linuxarm64 bin32:ubuntu12_32 bin64:ubuntu12_64; do
            ln -sfn "../.local/share/Steam/${l#*:}" "$DOT/${l%%:*}"
        done
    fi
    echo "phase 1 done: $(du -sh "$STEAM" | cut -f1)"
}

# The updated client needs a few libraries Ubuntu Touch does not have (GTK 2 for steamui.so and
# vgui2_s.so); the Steam Runtime has them. Link only those, by name, into Steam's own library
# folder (first in the client's LD_LIBRARY_PATH after its own); never the C library itself.
links() {
    R=$(ls -d steam-runtime-steamrt-arm64/steamrt3c_platform_*/files | head -1)
    L=lib/aarch64-linux-gnu
    export LD_LIBRARY_PATH="$STEAM/steamrtarm64:$STEAM/$L"
    for round in 1 2 3 4 5 6; do
        miss=$(for f in steamrtarm64/steam steamrtarm64/*.so steamrtarm64/steamwebhelper steamrtarm64/gameoverlayui; do
                   ldd "$f" 2>/dev/null | grep "not found" | awk '{print $1}'; done | sort -u)
        [ -n "$miss" ] || break
        for m in $miss; do
            case "$m" in libc.so*|libm.so*|libpthread*|libdl.so*|ld-linux*|librt.so*) echo "not linked: $m (part of the C library)"; continue;; esac
            # FFmpeg 7 (.61/.59): only the unused unversioned libav*.so links ask for it
            case "$m" in libavutil.so.59|libavcodec.so.61) continue;; esac
            t=$(find "$R/lib/aarch64-linux-gnu" "$R/usr/lib/aarch64-linux-gnu" -name "$m*" 2>/dev/null | sort | head -1)
            if [ -n "$t" ]; then ln -sfn "../../$t" "$L/$m"; echo "linked $m -> $(basename "$t")"; else echo "not in the runtime: $m"; fi
        done
    done
}

phase2() {
    # Lomiri's Xwayland (rootless) is display :1 on this phone; Steam's windows appear on screen.
    export DISPLAY=${DISPLAY:-:1} XDG_RUNTIME_DIR=/run/user/$(id -u) LANG=C.UTF-8
    export LD_LIBRARY_PATH="$STEAM/steamrtarm64:$STEAM/lib/aarch64-linux-gnu"
    unset WAYLAND_DISPLAY GLIBC_TUNABLES LD_PRELOAD
    s=$(date +%s)
    timeout 900 "$STEAM/steamrtarm64/steam" -exitsteam > "$LOG/bootstrap.log" 2>&1 || echo "steam exited with code $?"
    echo "after $(( $(date +%s) - s )) s"
    if [ -f "package/$MANIFEST.installed" ] && [ -f steamrtarm64/steamui.so ]; then
        echo "bootstrap OK: $(du -sh "$STEAM" | cut -f1)"
        links
    else
        echo "bootstrap incomplete (see $LOG/bootstrap.log)"
    fi
}

case "${1:-}" in
    1) phase1 ;;
    2) phase2 ;;
    links) links ;;
    *) echo "usage: $0 1|2" >&2; exit 2 ;;
esac
