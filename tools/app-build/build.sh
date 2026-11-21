#!/bin/sh
# Builds UTDeck's app (app/) for arm64 in the utdeck-app-cross container (stage 4.1):
#
#   docker build -t utdeck-app-cross tools/app-build/docker     once
#   tools/app-build/build.sh                                     build/app/utdeck
#
# Then tools/app-build/build-click.sh packages it.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src utdeck-app-cross sh -euc '
  cmake -S app -B build/app -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build/app
'
file "$root/build/app/utdeck"
