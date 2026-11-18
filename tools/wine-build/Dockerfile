# Cross-build environment for Wine's Unix-side libraries on Ubuntu Touch 24.04-2.x (arm64).
#
# Built on top of the Mesa container (tools/mesa-build/Dockerfile: Ubuntu 24.04 amd64 with the
# arm64 libraries from noble and UBports 24.04-2.x, the same versions as on the phone), plus what
# Wine's configure needs for the Wayland driver and its tools. The Windows side uses llvm-mingw,
# mounted from build/toolchains/ by build-winewayland.sh.
#
# Build:  docker build -t utdeck-wine-cross tools/wine-build
#         (after: docker build -t utdeck-mesa-cross tools/mesa-build)

FROM utdeck-mesa-cross

RUN apt-get update && apt-get install -y --no-install-recommends \
      gettext gawk autoconf \
      libxkbcommon-dev:arm64 libxkbregistry-dev:arm64 linux-libc-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
