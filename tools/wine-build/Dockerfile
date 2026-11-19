# Cross-build environment for Wine's Unix-side libraries on Ubuntu Touch 24.04-2.x (arm64).
#
# Built on top of the Mesa container (tools/mesa-build/Dockerfile: Ubuntu 24.04 amd64 with the
# arm64 libraries from noble and UBports 24.04-2.x, the same versions as on the phone), plus what
# Wine's configure needs for the Wayland driver, win32u (FreeType and fontconfig: win32u renders
# system fonts with them; without them its font code is left out) and its tools (amd64 FreeType:
# sfnt2fon makes Wine's bitmap fonts on the build machine), plus CMake for
# FEX's and Box64's Windows DLLs (build-runtime.sh). The Windows side uses llvm-mingw, mounted from
# build/toolchains/ by build-winewayland.sh and build-runtime.sh.
#
# The second group is what Wine's Unix side needs to have sound, gamepads, video and the rest
# (Hangover's Ubuntu build dependencies, minus X11 and minus what the phone does not have: SANE,
# gphoto2, ODBC, OpenCL). Wine's configure leaves out quietly whatever it cannot find, so
# build-runtime.sh checks for every module. GLib's arm64 headers (needed by PulseAudio, SDL2 and
# GStreamer) depend on "python3 | qemu-user | qemu-user-static": the amd64 qemu-user-static
# satisfies that, where arm64 Python would replace the container's own and fail to run.
#
# The third group is for SDL3 with X11 and Wayland, which Steam's web helper needs on Ubuntu Touch
# (tools/steam-build/build-sdl3.sh, stage 3.S). Wine is configured --without-x and ignores it.
#
# Build:  docker build -t utdeck-wine-cross tools/wine-build
#         (after: docker build -t utdeck-mesa-cross tools/mesa-build)

FROM utdeck-mesa-cross

RUN apt-get update && apt-get install -y --no-install-recommends \
      gettext gawk autoconf cmake \
      libxkbcommon-dev:arm64 libxkbregistry-dev:arm64 linux-libc-dev:arm64 \
      libfreetype-dev:arm64 libfontconfig-dev:arm64 libfreetype-dev \
 && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends \
      libglib2.0-dev-bin qemu-user-static \
      libpulse-dev:arm64 libasound2-dev:arm64 libsdl2-dev:arm64 libudev-dev:arm64 libusb-1.0-0-dev:arm64 \
      libdbus-1-dev:arm64 libgnutls28-dev:arm64 libkrb5-dev:arm64 libcups2-dev:arm64 libpcap-dev:arm64 \
      libv4l-dev:arm64 libpcsclite-dev:arm64 \
      libgstreamer1.0-dev:arm64 libgstreamer-plugins-base1.0-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends \
      libx11-dev:arm64 libxext-dev:arm64 libxrandr-dev:arm64 libxcursor-dev:arm64 libxi-dev:arm64 \
      libxss-dev:arm64 libxtst-dev:arm64 libxfixes-dev:arm64 libdecor-0-dev:arm64 \
      libdrm-dev:arm64 libgbm-dev:arm64 libegl-dev:arm64 libgles-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
