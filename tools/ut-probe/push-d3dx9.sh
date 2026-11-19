#!/bin/sh
# UTDeck stage 3.2: install Microsoft's own d3dx9 DLLs into a prefix on the phone, as winetricks'
# d3dx9 verbs do. Some games and mods need them: Wine's built-in d3dx9 copies refuse objects made
# by another d3dx9 version (NFS Most Wanted's mods mix d3dx9_26 and d3dx9_43), which Microsoft's
# DLLs accept. Use them per game, with WINEDLLOVERRIDES naming the versions native.
#
#   tools/ut-probe/push-d3dx9.sh PREFIX VERSION...      e.g. push-d3dx9.sh prefix-games 26 43
#
# Source: Microsoft's DirectX End-User Runtime (June 2010), from download.microsoft.com, checked
# against the SHA-256 that winetricks uses. Not part of UTDeck's runtime (Microsoft's licence lets
# games ship it, not us); downloaded into build/components/ on the PC. 32-bit DLLs go to syswow64,
# 64-bit ones to system32.
set -eu
REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX=/home/phablet/utdeck-probe/wine/$1; shift
D=$REPO_DIR/build/components
URL=https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe
SHA=053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b
mkdir -p "$D"
[ -f "$D/directx_Jun2010_redist.exe" ] || curl -fsSL -o "$D/directx_Jun2010_redist.exe" "$URL"
echo "$SHA  $D/directx_Jun2010_redist.exe" | sha256sum -c --quiet
for v in "$@"; do
    for a in x86 x64; do
        cab=$(7z l "$D/directx_Jun2010_redist.exe" | awk '{print $NF}' | grep -E "_d3dx9_${v}_${a}\.cab$" | head -1)
        [ -n "$cab" ] || { echo "no d3dx9_$v for $a in the redistributable" >&2; exit 1; }
        rm -rf "$D/tmp" && mkdir -p "$D/tmp" "$D/d3dx9/$a"
        7z e -y -o"$D/tmp" "$D/directx_Jun2010_redist.exe" "$cab" > /dev/null
        7z e -y -o"$D/d3dx9/$a" "$D/tmp/$cab" "d3dx9_$v.dll" > /dev/null
        dst=$PREFIX/drive_c/windows/$([ $a = x86 ] && echo syswow64 || echo system32)
        adb push -q "$D/d3dx9/$a/d3dx9_$v.dll" "$dst/" > /dev/null
        echo "d3dx9_$v.dll ($a, from $cab) -> $dst"
    done
done
rm -rf "$D/tmp"
