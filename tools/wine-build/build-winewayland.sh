#!/bin/sh
# Build Wine's Wayland driver (Unix side, winewayland.so) for Ubuntu Touch, with our patches, from
# the exact Wine source of the Hangover release we run, so it drops into that Hangover unchanged.
#
#   tools/wine-build/build-winewayland.sh     -> build/winewayland/winewayland.so (stripped) + BUILD-INFO
#
# Source: AndreRH/wine at the commit Hangover 11.16 pins (its `wine` submodule), configured as
# Hangover's Ubuntu 24.04 package does (--enable-archs=arm64ec,aarch64,i386, llvm-mingw). Only
# winewayland.so is built; everything else on the phone stays Hangover's.
# Patches: tools/wine-build/patches/*.patch (PATCHES="" builds the source as it is).
set -eu

REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
WINE_URL=https://github.com/AndreRH/wine.git
WINE_COMMIT=3ea3a14941b6a122b892c84141ce9662c008081e   # hangover-11.16
SRC="$REPO_DIR/build/wine-src"
OUT="$REPO_DIR/build/winewayland"
TC_VER=20261104
TC="$REPO_DIR/build/toolchains/llvm-mingw-$TC_VER-ucrt-ubuntu-22.04-x86_64"
IMAGE=utdeck-wine-cross
if [ -z "${PATCHES+x}" ]; then PATCHES=$(ls "$REPO_DIR"/tools/wine-build/patches/*.patch 2>/dev/null || true); fi

[ -x "$TC/bin/clang" ] || "$REPO_DIR/tools/ut-probe/win/build.sh" >/dev/null   # fetches llvm-mingw
if [ ! -d "$SRC/.git" ]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin "$WINE_URL"
fi
git -C "$SRC" fetch -q --depth 1 origin "$WINE_COMMIT"
git -C "$SRC" checkout -q --force --detach "$WINE_COMMIT"
git -C "$SRC" clean -fdq
for p in $PATCHES; do git -C "$SRC" apply "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"; done

docker image inspect utdeck-mesa-cross >/dev/null 2>&1 || docker build -t utdeck-mesa-cross "$REPO_DIR/tools/mesa-build"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/wine-build"
mkdir -p "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -v "$SRC":/wine:ro -v "$OUT":/out -v "$TC":/opt/llvm-mingw:ro "$IMAGE" sh -euc '
    J=$(nproc)
    # 1. Wine'"'"'s build tools for the build machine (amd64).
    mkdir -p /out/tools && cd /out/tools
    [ -f Makefile ] || /wine/configure --enable-win64 --without-x --without-freetype --without-wayland \
        --without-mingw --disable-tests > configure.log 2>&1
    make -j$J __tooldeps__ > make.log 2>&1
    # 2. The arm64 build, configured like Hangover'"'"'s package, building only winewayland.so.
    mkdir -p /out/arm64 && cd /out/arm64
    export PATH=/opt/llvm-mingw/bin:$PATH
    export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
    [ -f Makefile ] || /wine/configure --prefix=/usr --with-mingw=clang --enable-archs=arm64ec,aarch64,i386 \
        --disable-tests --without-freetype --without-x --host=aarch64-linux-gnu --with-wine-tools=/out/tools \
        CC=aarch64-linux-gnu-gcc PKG_CONFIG=pkg-config > configure.log 2>&1
    grep -i -E "wayland|xkb|egl" configure.log | grep -i -v "^checking" || true
    make -j$J dlls/winewayland.drv/winewayland.so > make.log 2>&1
    aarch64-linux-gnu-strip -o /out/winewayland.so dlls/winewayland.drv/winewayland.so
'
{
    echo "source: $WINE_URL"
    echo "commit: $WINE_COMMIT ($(git -C "$SRC" log -1 --format=%s)) $(cat "$SRC/VERSION")"
    echo "patches: $(for p in $PATCHES; do printf '%s ' "$(basename "$p")"; done)"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "sha256: $(sha256sum "$OUT/winewayland.so" | cut -d' ' -f1)"
} > "$OUT/BUILD-INFO"
cat "$OUT/BUILD-INFO"
