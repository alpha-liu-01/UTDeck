#!/bin/sh
# UTDeck stage 3.S: SDL3 for Valve's ARM64 Steam client on Ubuntu Touch, from the same SDL commit
# Valve bundles, with both X11 and Wayland.
#
#   tools/steam-build/build-sdl3.sh     -> build/steam/libSDL3.so.0 (stripped) + BUILD-INFO
#
# Why: the client's web helper (its interface) opens its window with SDL3 on Wayland, as on the
# Steam Frame. The SDL3 Valve ships next to the client has no Wayland driver, and the Steam Runtime's
# SDL3 (3.4.18) has Wayland but lacks newer functions the client uses (SDL_TryLockJoysticks). This
# is Valve's version (`strings libSDL3.so.0`: SDL-release-3.4.0-1576-gab5541ec5) built with
# Wayland on. SDL loads X11, Wayland, EGL and the rest at run time, so only headers are needed.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
SDL_URL=https://github.com/libsdl-org/SDL.git
SDL_COMMIT=ab5541ec5d4377dcc20a701897437f07ba71566f   # the commit in Steam 1792618285 (arm64, steamdeck_stable)
SRC=$REPO_DIR/build/src/sdl3
OUT=$REPO_DIR/build/steam
IMAGE=utdeck-wine-cross
if [ ! -d "$SRC/.git" ]; then git init -q "$SRC"; git -C "$SRC" remote add origin "$SDL_URL"; fi
git -C "$SRC" fetch -q --depth 1 origin "$SDL_COMMIT"
git -C "$SRC" checkout -q --force --detach "$SDL_COMMIT"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/wine-build"
mkdir -p "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -v "$SRC":/sdl:ro -v "$OUT":/out "$IMAGE" sh -euc '
    export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
    cmake -S /sdl -B /out/obj -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
        -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_WAYLAND_LIBDECOR=ON -DSDL_KMSDRM=OFF -DSDL_VULKAN=ON \
        -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_PULSEAUDIO=ON -DSDL_PIPEWIRE=OFF -DSDL_ALSA=ON \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF > /out/cmake.log 2>&1 || { tail -30 /out/cmake.log >&2; exit 1; }
    grep -E "SDL_WAYLAND|SDL_X11 |SDL_PULSEAUDIO|SDL_HIDAPI" /out/cmake.log | head -8 || true
    ninja -C /out/obj > /out/ninja.log 2>&1 || { tail -30 /out/ninja.log >&2; exit 1; }
    so=$(ls /out/obj/libSDL3.so.0.* | head -1)
    aarch64-linux-gnu-strip --strip-unneeded -o /out/libSDL3.so.0 "$so"
'
{
    echo "source: $SDL_URL $SDL_COMMIT ($(git -C "$SRC" log -1 --format=%s))"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ), $IMAGE, X11 + Wayland (libdecor), PulseAudio, ALSA"
    echo "sha256: $(sha256sum "$OUT/libSDL3.so.0" | cut -d' ' -f1)"
} > "$OUT/BUILD-INFO"
cat "$OUT/BUILD-INFO"
