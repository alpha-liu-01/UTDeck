# Cross-build environment for UTDeck's app (stage 4, app/) on Ubuntu Touch 24.04-2.x (arm64). It
# also holds the Qt 6 runtime that the click for 24.04-1.x bundles (/opt/ut-runtime-1x).
#
# Taken from the Dolphin port's container (dolphin-ut, ut/docker/Dockerfile), without what only
# Dolphin needed (its emulator's libraries, libhybris' Vulkan).
#
# Host: amd64 Ubuntu 24.04. Target: arm64, from the same archives the phone uses: Ubuntu noble
# (ports.ubuntu.com) plus repo.ubports.com 24.04-2.x pinned at 2000, so Qt 6 is the phone's 6.10.2,
# not noble's 6.4.
#
# Build:  docker build -t utdeck-app-cross tools/app-build/docker
# Use:    tools/app-build/build.sh (mounts the repository at /src)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# amd64 from archive.ubuntu.com, arm64 from ports.ubuntu.com.
RUN sed -i 's/^Types: deb$/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources \
 && printf '%s\n' \
      'Types: deb' 'Architectures: arm64' 'URIs: http://ports.ubuntu.com/ubuntu-ports/' \
      'Suites: noble noble-updates noble-security' 'Components: main universe restricted multiverse' \
      'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/ubuntu-ports.sources \
 && dpkg --add-architecture arm64

# UBports 24.04-2.x, as on the phone (/etc/apt/sources.list.d/ubports.list, preferences.d/ubports.pref).
COPY ubports.gpg /etc/apt/trusted.gpg.d/ubports.gpg
RUN echo 'deb [arch=amd64,arm64] http://repo.ubports.com/ 24.04-2.x main' > /etc/apt/sources.list.d/ubports.list \
 && printf '%s\n' 'Package: *' 'Pin: release o=UBports,a=24.04-2.x' 'Pin-Priority: 2000' \
      > /etc/apt/preferences.d/ubports.pref

# Host tools. The amd64 Qt packages give moc, rcc, qmlcachegen and qmltyperegistrar, found by a cross
# build through QT_HOST_PATH; they must be the same Qt version as the target (UBports' 6.10.2).
# qemu-user-static satisfies libglib2.0-dev:arm64's "python3 | qemu-user | qemu-user-static".
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git cmake ninja-build pkgconf python3 file qemu-user-static \
      crossbuild-essential-arm64 \
      qt6-base-dev qt6-base-dev-tools \
      qt6-declarative-dev qt6-declarative-dev-tools \
 && rm -rf /var/lib/apt/lists/*

# Target libraries (arm64): Qt Core, Gui, DBus, Quick.
RUN apt-get update && apt-get install -y --no-install-recommends \
      qt6-base-dev:arm64 qt6-declarative-dev:arm64 \
      libegl-dev:arm64 libgles-dev:arm64 libwayland-dev:arm64 libxkbcommon-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# Stage 4.2, the sign-in: libX11 and XTest to capture hidden Steam's screen and pass taps and key
# presses to it (both on every 24.04 phone), ZXing-C++ to read and draw the QR code (its library is
# bundled in the click).
RUN apt-get update && apt-get install -y --no-install-recommends \
      libx11-dev:arm64 libxtst-dev:arm64 libzxing-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# Click packaging: `click build`, and the Qt 6 Wayland shell integration the phone lacks (Dolphin
# port, stage 0), unpacked from UBports' arm64 packages into /opt/ut-bundle to be copied into the
# click: the xdg-shell plugin and libQt6WlShellIntegration.
RUN apt-get update && apt-get install -y --no-install-recommends click \
 && mkdir -p /tmp/debs /opt/ut-bundle && cd /tmp/debs \
 && apt-get download qt6-wayland:arm64 libqt6wlshellintegration6:arm64 \
 && for d in *.deb; do dpkg-deb -x "$d" /opt/ut-bundle; done \
 && ls /opt/ut-bundle/usr/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration/libxdg-shell.so \
       /opt/ut-bundle/usr/lib/aarch64-linux-gnu/libQt6WlShellIntegration.so.6 \
 && rm -rf /tmp/debs /var/lib/apt/lists/*

# The click for Ubuntu Touch 24.04-1.x, which has no Qt 6: the runtime of runtime-24.04-1.x.list
# in /opt/ut-runtime-1x.
COPY runtime-24.04-1.x.list /opt/runtime-24.04-1.x.list
RUN apt-get update \
 && mkdir -p /tmp/debs /opt/ut-runtime-1x && cd /tmp/debs \
 && apt-get download $(sed 's/#.*//' /opt/runtime-24.04-1.x.list | tr -s ' \n' '\n' | sed '/^$/d; s/$/:arm64/') \
 && for d in *.deb; do dpkg-deb -x "$d" /opt/ut-runtime-1x; done \
 && ls /opt/ut-runtime-1x/usr/lib/aarch64-linux-gnu/qt6/plugins/platforms/libqwayland.so \
       /opt/ut-runtime-1x/usr/lib/aarch64-linux-gnu/qt6/qml/Lomiri/Components/qmldir \
 && rm -rf /tmp/debs /var/lib/apt/lists/*

COPY aarch64-linux-gnu.cmake /opt/aarch64-linux-gnu.cmake
WORKDIR /src
