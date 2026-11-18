#!/bin/sh
# Build Mesa Turnip (Vulkan, glibc, KGSL) for Ubuntu Touch, with the android_wlegl WSI patch.
#
#   tools/mesa-build/build-turnip.sh            # fetch the pinned source, build, package
#   SRC=/path/to/mesa tools/mesa-build/build-turnip.sh   # build another Mesa tree
#
# Source (default): panxiao81/debian-mesa, branch droidian, commit adb4b85e82: Mesa 26.2.5 with
# "vulkan/wsi/wayland: present through android_wlegl gralloc buffers" on top. Output in
# build/turnip/: libvulkan_freedreno.so, an ICD file for the phone's test folder, and BUILD-INFO.
#
# Meson options follow Droid-Deck/Drivers' scripts/build_linux.sh, limited to Wayland (Lomiri has
# no GPU path for X11 clients) and to KGSL (the phone has no msm GPU node).
set -eu

REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$REPO_DIR/build/turnip"
IMAGE=utdeck-mesa-cross
MESA_URL=https://github.com/panxiao81/debian-mesa.git
MESA_COMMIT=adb4b85e82
PHONE_DIR=/home/phablet/utdeck-probe/turnip-wlegl

mkdir -p "$OUT"
if [ -z "${SRC:-}" ]; then
    SRC="$REPO_DIR/build/mesa-src"
    if [ ! -d "$SRC/.git" ]; then
        git clone --branch droidian --depth 50 "$MESA_URL" "$SRC"
    fi
    git -C "$SRC" fetch --depth 50 origin droidian
    git -C "$SRC" checkout --detach "$MESA_COMMIT"
fi

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/mesa-build"

docker run --rm -u "$(id -u):$(id -g)" \
    -v "$SRC":/mesa:ro -v "$OUT":/out -v "$REPO_DIR/tools/mesa-build":/cfg:ro \
    "$IMAGE" sh -euc '
        # nofallback: no fallback subprojects unpacked into the (read-only) source tree; the only
        # one meson tries is libarchive, used by freedreno debug tools we do not build.
        meson setup /out/build /mesa --cross-file /cfg/cross-aarch64.ini --wipe --wrap-mode=nofallback \
            --buildtype release -Db_ndebug=true \
            -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dgallium-drivers= \
            -Dplatforms=wayland -Dopengl=false -Dgbm=disabled -Dglx=disabled -Degl=disabled \
            -Dllvm=disabled -Dvulkan-layers= -Dtools= -Dvideo-codecs= -Dxmlconfig=disabled \
            > /out/meson-setup.log 2>&1 || { tail -40 /out/meson-setup.log; exit 1; }
        ninja -C /out/build src/freedreno/vulkan/libvulkan_freedreno.so > /out/ninja.log 2>&1 \
            || { tail -40 /out/ninja.log; exit 1; }
        aarch64-linux-gnu-strip -o /out/libvulkan_freedreno.so \
            /out/build/src/freedreno/vulkan/libvulkan_freedreno.so
    '

{
    echo "source: $(git -C "$SRC" remote get-url origin 2>/dev/null || echo "$SRC")"
    echo "commit: $(git -C "$SRC" rev-parse HEAD) ($(git -C "$SRC" log -1 --format=%s))"
    echo "mesa version: $(cat "$SRC/VERSION")"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "sha256: $(sha256sum "$OUT/libvulkan_freedreno.so" | cut -d' ' -f1)"
} > "$OUT/BUILD-INFO"

cat > "$OUT/turnip_icd.json" <<EOF
{
    "file_format_version": "1.0.1",
    "ICD": {
        "library_path": "$PHONE_DIR/libvulkan_freedreno.so",
        "api_version": "1.4.335"
    }
}
EOF
cat "$OUT/BUILD-INFO"
