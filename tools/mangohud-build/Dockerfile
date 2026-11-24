# MangoHud's cross build (stage 5.1): the Mesa cross container (amd64 host, arm64 Ubuntu 24.04 target
# with UBports' 24.04-2.x pinned) plus xkbcommon, which MangoHud's Wayland keyboard shortcuts need.
FROM utdeck-mesa-cross

RUN apt-get update && apt-get install -y --no-install-recommends \
      libxkbcommon-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
