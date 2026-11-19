#!/bin/sh
# UTDeck stage 3.1: put the runtime's DXVK and VKD3D-Proton into a Wine prefix, as winetricks does:
# the 64-bit builds into system32 (DXVK's ARM64EC build, VKD3D-Proton's ARM64X build: x86-64 games
# load them as native ARM64 code), the 32-bit ones into syswow64. Wine keeps using its own copies
# unless a program is run with WINEDLLOVERRIDES naming them native, for example
#   WINEDLLOVERRIDES="d3d11,dxgi,d3d10core,d3d9,d3d8=n;d3d12,d3d12core=n"
#
#   . /home/phablet/utdeck-probe/wine/env.sh; prefix-dlls.sh      (uses $WINEPREFIX and the wine in PATH)
set -eu
RT=$(cd "$(dirname "$(command -v wine)")/.." && pwd)
S=$WINEPREFIX/drive_c/windows/system32
W=$WINEPREFIX/drive_c/windows/syswow64
[ -d "$S" ] && [ -d "$W" ] || { echo "no prefix at $WINEPREFIX (run wineboot first)" >&2; exit 1; }
[ -d "$RT/lib/dxvk" ] || { echo "$RT has no lib/dxvk: not a UTDeck runtime" >&2; exit 1; }
cp "$RT"/lib/dxvk/arm64ec/*.dll "$RT"/lib/vkd3d-proton/arm64x/*.dll "$S/"
cp "$RT"/lib/dxvk/x32/*.dll "$RT"/lib/vkd3d-proton/x86/*.dll "$W/"
echo "DXVK and VKD3D-Proton from $RT copied into $WINEPREFIX"
grep -E "^(dxvk|vkd3d-proton):" "$RT/BUILD-INFO" || true
