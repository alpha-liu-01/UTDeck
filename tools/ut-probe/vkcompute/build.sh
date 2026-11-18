#!/bin/sh
# Build the UTDeck Vulkan compute tests for the phone (arm64), in the utdeck-mesa-cross container.
#   tools/ut-probe/vkcompute/build.sh      -> build/vkcompute/{intops,intops.spv,...}
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
OUT="$REPO_DIR/build/vkcompute"
mkdir -p "$OUT"
docker image inspect utdeck-mesa-cross >/dev/null 2>&1 || docker build -t utdeck-mesa-cross "$REPO_DIR/tools/mesa-build"
docker run --rm -u "$(id -u):$(id -g)" -v "$REPO_DIR/tools/ut-probe/vkcompute":/src:ro -v "$OUT":/out \
    utdeck-mesa-cross sh -euc '
        for s in /src/*.comp; do
            n=$(basename "$s" .comp)
            glslangValidator -V --target-env vulkan1.1 -o /out/$n.spv "$s" >/dev/null
        done
        for c in /src/layer/*.c; do
            n=$(basename "$c" .c)
            aarch64-linux-gnu-gcc -O2 -Wall -shared -fPIC -fvisibility=hidden -o /out/libutdeck_$n.so "$c" -lpthread
            cp /src/layer/utdeck_$n.json /out/
        done
        for c in /src/*.c; do
            n=$(basename "$c" .c)
            aarch64-linux-gnu-gcc -O2 -Wall -o /out/$n "$c" -lvulkan -lm
        done
    '
ls -la "$OUT"
