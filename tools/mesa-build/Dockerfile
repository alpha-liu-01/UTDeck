# Cross-build environment for Mesa Turnip on Ubuntu Touch 24.04-2.x (arm64).
#
# Host: amd64 Ubuntu 24.04. Target: arm64, from the archives the phone uses: Ubuntu noble
# (ports.ubuntu.com) plus repo.ubports.com 24.04-2.x pinned at 2000 (the same setup as
# dolphin-ut's ut/docker). UBports is needed for wayland-protocols 1.45: Mesa 26.2 asks for
# 1.41 or later, and noble has 1.34.
#
# Build:  docker build -t utdeck-mesa-cross tools/mesa-build
# Use:    tools/mesa-build/build-turnip.sh

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

# UBports 24.04-2.x, as on the phone.
COPY ubports.gpg /etc/apt/trusted.gpg.d/ubports.gpg
RUN echo 'deb [arch=amd64,arm64] http://repo.ubports.com/ 24.04-2.x main' > /etc/apt/sources.list.d/ubports.list \
 && printf '%s\n' 'Package: *' 'Pin: release o=UBports,a=24.04-2.x' 'Pin-Priority: 2000' \
      > /etc/apt/preferences.d/ubports.pref

# Host tools. meson comes from pip: Mesa 26.2 needs 1.4 or later, noble has 1.3.2.
# glslang-tools: Mesa 26.2 compiles its ray-tracing BVH shaders with glslangValidator.
# build-essential: Mesa builds a few helper programs for the build machine.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git ninja-build pkgconf file python3 python3-pip python3-venv \
      python3-mako python3-yaml python3-ply python3-packaging \
      bison flex glslang-tools build-essential crossbuild-essential-arm64 \
      libwayland-bin wayland-protocols \
 && rm -rf /var/lib/apt/lists/* \
 && python3 -m venv --system-site-packages /opt/meson \
 && /opt/meson/bin/pip install --no-cache-dir 'meson==1.6.1'
ENV PATH=/opt/meson/bin:$PATH

# Target libraries (arm64): what a Turnip-only build links (all present on the phone), and
# libvulkan-dev for the Vulkan headers and loader stub of our test programs (tools/ut-probe/vkcompute),
# libglvnd-dev for the zink build (stage 1.3): Mesa's EGL as a glvnd vendor library.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libwayland-dev:arm64 libdrm-dev:arm64 libexpat1-dev:arm64 zlib1g-dev:arm64 \
      libzstd-dev:arm64 libelf-dev:arm64 \
      libvulkan-dev:arm64 \
      libglvnd-dev:arm64 libwayland-egl-backend-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
