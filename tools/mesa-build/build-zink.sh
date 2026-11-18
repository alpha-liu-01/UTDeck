#!/bin/sh
# Build Mesa with zink (OpenGL and OpenGL ES on Vulkan), EGL for Wayland, and Turnip, for Ubuntu
# Touch (stage 1.3). Same pinned source and our patches as build-turnip.sh.
#
#   tools/mesa-build/build-zink.sh      -> build/zink/stage/... (an installed Mesa tree) + BUILD-INFO
#
# EGL is built as a glvnd vendor library (libEGL_mesa.so.0), so a program uses it through the
# phone's own libEGL.so.1 when __EGL_VENDOR_LIBRARY_FILENAMES points at our vendor file; nothing on
# the phone changes. Wayland only (Lomiri has no GPU path for X11), no GLX, no GBM, no LLVM.
set -eu

REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$REPO_DIR/build/zink"
SRC="$REPO_DIR/build/mesa-src"
IMAGE=utdeck-mesa-cross
MESA_COMMIT=adb4b85e82

[ -d "$SRC/.git" ] || { echo "run build-turnip.sh once first (it clones $SRC)" >&2; exit 1; }
PATCHES=${PATCHES-$(ls "$REPO_DIR"/tools/mesa-build/patches/*.patch 2>/dev/null || true)}
git -C "$SRC" checkout --quiet --force --detach "$MESA_COMMIT"
git -C "$SRC" clean -fdq
for p in $PATCHES; do git -C "$SRC" apply "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"; done

mkdir -p "$OUT"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/mesa-build"
docker run --rm -u "$(id -u):$(id -g)" \
    -v "$SRC":/mesa:ro -v "$OUT":/out -v "$REPO_DIR/tools/mesa-build":/cfg:ro \
    "$IMAGE" sh -euc '
        meson setup /out/build /mesa --cross-file /cfg/cross-aarch64.ini --wipe --wrap-mode=nofallback \
            --prefix=/usr --libdir=lib/aarch64-linux-gnu --buildtype release -Db_ndebug=true \
            -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dgallium-drivers=zink \
            -Dplatforms=wayland -Degl=enabled -Dglvnd=enabled -Dopengl=true -Dgles1=disabled -Dgles2=enabled \
            -Dglx=disabled -Dgbm=disabled -Dllvm=disabled -Dvulkan-layers= -Dtools= -Dvideo-codecs= \
            -Dxmlconfig=disabled \
            > /out/meson-setup.log 2>&1 || { tail -40 /out/meson-setup.log; exit 1; }
        ninja -C /out/build > /out/ninja.log 2>&1 || { tail -40 /out/ninja.log; exit 1; }
        rm -rf /out/stage
        DESTDIR=/out/stage meson install -C /out/build --no-rebuild > /out/install.log 2>&1 \
            || { tail -20 /out/install.log; exit 1; }
        find /out/stage -type f -name "*.so*" -exec aarch64-linux-gnu-strip --strip-unneeded {} \;
    '

{
    echo "source: $(git -C "$SRC" remote get-url origin)"
    echo "commit: $(git -C "$SRC" rev-parse HEAD) ($(git -C "$SRC" log -1 --format=%s))"
    echo "mesa version: $(cat "$SRC/VERSION")"
    echo "extra patches: $(for p in $PATCHES; do printf "%s " "$(basename "$p")"; done)"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    (cd "$OUT/stage" && find . -name "*.so*" -type f -exec sha256sum {} \;)
} > "$OUT/BUILD-INFO"
cat "$OUT/BUILD-INFO"
