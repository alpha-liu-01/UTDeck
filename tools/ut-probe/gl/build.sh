#!/bin/sh
# Build gltest for the phone (arm64) in the utdeck-mesa-cross container.
#   tools/ut-probe/gl/build.sh   -> build/gl/gltest
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
OUT="$REPO_DIR/build/gl"
mkdir -p "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -v "$REPO_DIR/tools/ut-probe/gl":/src:ro -v "$OUT":/out \
    utdeck-mesa-cross sh -euc '
        X=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
        wayland-scanner client-header $X /out/xdg-shell-client-protocol.h
        wayland-scanner private-code $X /out/xdg-shell-protocol.c
        aarch64-linux-gnu-gcc -O2 -Wall -I/out -o /out/gltest /src/gltest.c /out/xdg-shell-protocol.c \
            -lEGL -lwayland-egl -lwayland-client
    '
ls -la "$OUT/gltest"
