#!/bin/sh
# Build the UTDeck Windows test programs for every architecture, with llvm-mingw.
#   tools/ut-probe/win/build.sh   -> build/win/wtest-{arm64,arm64ec,x86_64,i686}.exe,
#                                    build/win/vktest-{arm64,x86_64}.exe
# llvm-mingw (mstorsjo/llvm-mingw 20261104, ucrt, Ubuntu 22.04 x86-64 host) and the Khronos Vulkan
# headers (1.4.335) are downloaded into build/toolchains/ the first time.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
TC_VER=20261104
TC="$REPO_DIR/build/toolchains/llvm-mingw-$TC_VER-ucrt-ubuntu-22.04-x86_64"
if [ ! -x "$TC/bin/clang" ]; then
    mkdir -p "$REPO_DIR/build/toolchains"
    curl -fsSL -o "$REPO_DIR/build/toolchains/llvm-mingw-$TC_VER.tar.xz" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/$TC_VER/llvm-mingw-$TC_VER-ucrt-ubuntu-22.04-x86_64.tar.xz"
    tar xf "$REPO_DIR/build/toolchains/llvm-mingw-$TC_VER.tar.xz" -C "$REPO_DIR/build/toolchains"
fi
VKH="$REPO_DIR/build/toolchains/Vulkan-Headers-1.4.335"
if [ ! -f "$VKH/include/vulkan/vulkan.h" ]; then
    curl -fsSL -o "$REPO_DIR/build/toolchains/vulkan-headers-1.4.335.tar.gz" \
        https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.335.tar.gz
    tar xzf "$REPO_DIR/build/toolchains/vulkan-headers-1.4.335.tar.gz" -C "$REPO_DIR/build/toolchains"
fi
OUT="$REPO_DIR/build/win"
mkdir -p "$OUT"
for t in aarch64:arm64 arm64ec:arm64ec x86_64:x86_64 i686:i686; do
    cc=${t%%:*}; name=${t##*:}
    "$TC/bin/$cc-w64-mingw32-clang" -O2 -Wall -o "$OUT/wtest-$name.exe" "$REPO_DIR/tools/ut-probe/win/wtest.c"
done
for t in aarch64:arm64 x86_64:x86_64; do
    cc=${t%%:*}; name=${t##*:}
    "$TC/bin/$cc-w64-mingw32-clang" -O2 -Wall -I"$VKH/include" -o "$OUT/vktest-$name.exe" \
        "$REPO_DIR/tools/ut-probe/win/vktest.c"
done
ls -la "$OUT"
