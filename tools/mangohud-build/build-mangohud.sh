#!/bin/sh
# Build MangoHud (the performance overlay, stage 5.1) for Ubuntu Touch 24.04 (arm64) from its pinned
# release source, in a cross container on top of Mesa's (tools/mangohud-build/Dockerfile).
#
#   tools/mangohud-build/build-mangohud.sh   -> build/mangohud/stage/... (an installed tree) + BUILD-INFO
#
# MangoHud is a Vulkan layer: every game here reaches the screen through Vulkan (DXVK, VKD3D-Proton,
# zink on Turnip), so the layer covers them all. The release's Source archive carries the libraries
# MangoHud builds in (Dear ImGui, ImPlot, spdlog, Vulkan's headers), so nothing is downloaded at build
# time. Left out: X11 (Lomiri runs games on Wayland), D-Bus (only for its media player line), the
# NVIDIA parts, the tests, mangoplot and the docs. Built in: Wayland (keyboard shortcuts), FEX's
# statistics (with_fex: the x86 emulator our runtime uses), mangohudctl (shows or hides the overlay of
# a running game through its control socket). Our patches: tools/mangohud-build/patches/.
set -eu

REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-"$REPO_DIR/build/mangohud"}
SRC="$REPO_DIR/build/mangohud-src"
DL="$REPO_DIR/build/src/dl"
IMAGE=utdeck-mangohud-cross
VERSION=v0.8.4
URL=https://github.com/flightlessmango/MangoHud/releases/download/$VERSION/MangoHud-$VERSION-Source.tar.xz
SHA256=bbe5a2b976313c53f21dc24b98bbed03226589460a0cce20d923cb2c36c9d8b0   # as GitHub lists it
PATCHES=${PATCHES-$(ls "$REPO_DIR"/tools/mangohud-build/patches/*.patch 2>/dev/null || true)}

mkdir -p "$DL" "$OUT"
f="$DL/$(basename "$URL")"
[ -f "$f" ] || { curl -fsSL -o "$f.part" "$URL" && mv "$f.part" "$f"; }
echo "$SHA256  $f" | sha256sum -c - > /dev/null || { echo "checksum mismatch: $f" >&2; rm -f "$f"; exit 1; }
rm -rf "$SRC" && mkdir -p "$SRC" && tar -C "$SRC" -xJf "$f" --strip-components=1
for p in $PATCHES; do patch -d "$SRC" -p1 --quiet < "$p"; done

docker image inspect utdeck-mesa-cross > /dev/null 2>&1 \
    || docker build -t utdeck-mesa-cross "$REPO_DIR/tools/mesa-build"
docker image inspect "$IMAGE" > /dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/mangohud-build"
docker run --rm -u "$(id -u):$(id -g)" \
    -v "$SRC":/src -v "$OUT":/out -v "$REPO_DIR/tools/mesa-build":/cfg:ro \
    "$IMAGE" sh -euc '
        meson setup /out/build /src --cross-file /cfg/cross-aarch64.ini --wipe --wrap-mode=nodownload \
            --prefix=/usr --libdir=lib/aarch64-linux-gnu --buildtype release -Db_ndebug=true \
            -Dappend_libdir_mangohud=false -Dinclude_doc=false \
            -Dwith_x11=disabled -Dwith_wayland=enabled -Dwith_dbus=disabled \
            -Dwith_nvml=disabled -Dwith_xnvctrl=disabled -Dwith_fex=true \
            -Dmangohudctl=true -Dmangoapp=false -Dmangoplot=disabled -Dtests=disabled \
            > /out/meson-setup.log 2>&1 || { tail -40 /out/meson-setup.log; exit 1; }
        ninja -C /out/build > /out/ninja.log 2>&1 || { tail -40 /out/ninja.log; exit 1; }
        rm -rf /out/stage
        DESTDIR=/out/stage meson install -C /out/build --no-rebuild > /out/install.log 2>&1 \
            || { tail -20 /out/install.log; exit 1; }
        find /out/stage -type f \( -name "*.so*" -o -path "*/bin/*" \) -exec aarch64-linux-gnu-strip --strip-unneeded {} \; 2>/dev/null || true
    '

{
    echo "source: $URL"
    echo "sha256: $SHA256"
    echo "mangohud version: $VERSION"
    echo "extra patches: $(for p in $PATCHES; do printf "%s " "$(basename "$p")"; done)"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    (cd "$OUT/stage" && find . -type f \( -name "*.so*" -o -path "*/bin/*" -o -name "*.json" \) -exec sha256sum {} \;)
} > "$OUT/BUILD-INFO"
cat "$OUT/BUILD-INFO"
