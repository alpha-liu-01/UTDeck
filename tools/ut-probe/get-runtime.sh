#!/bin/sh
# UTDeck stage 3.3 (step 5.5): install the second Wine base on the phone, on request. The default
# runtime (Hangover's Wine 11.16 base) is always there; Proton 11's Wine base, which only some games
# need (the second compatibility tool, "UTDeck runtime (Proton 11)"), is not shipped by default.
#
#   get-runtime.sh KIND             download the version named in runtimes.list, check it, install it
#   get-runtime.sh KIND FILE        install from a file already on the phone (runtime-push.sh proton
#                                   pushes one from the PC); checked against FILE.sha256 if present,
#                                   else against runtimes.list
#
# KIND: proton (Proton 11's base, on request), or default (the default runtime; stage 4.2: the
# app's first-time setup installs it this way, with no PC).
# runtimes.list (next to this script), one line per runtime: KIND VERSION SHA256 URL.
# Installs into wine/runtime-<version>/ and points wine/runtime (default) or wine/runtime-proton
# (proton) at it; older versions stay, for going back (ln -sfn runtime-<old> runtime). Touches no
# prefix.
set -eu
W=/home/phablet/utdeck-probe/wine
HERE=$(cd "$(dirname "$0")" && pwd)
KIND=${1:-}
case "$KIND" in proton) LINK=runtime-proton ;; default) LINK=runtime ;; *) echo "usage: $0 default|proton [FILE]" >&2; exit 2 ;; esac
FILE=${2:-}
LIST=$HERE/runtimes.list

entry=$(grep "^$KIND " "$LIST" 2>/dev/null | tail -1 || true)
VER=$(echo "$entry" | awk '{print $2}') SUM=$(echo "$entry" | awk '{print $3}') URL=$(echo "$entry" | awk '{print $4}')

# Space: the unpacked runtime is about 1.1 GB; keep 1 GB more for the games.
free=$(df -m "$W" | awk 'NR == 2 {print $4}')
[ "$free" -ge 2200 ] || { echo "not enough space: $free MB free, about 2200 MB needed" >&2; exit 1; }

if [ -z "$FILE" ]; then
    [ -n "$URL" ] || { echo "no download address for $KIND in $LIST" >&2; exit 1; }
    mkdir -p "$W/dl"
    FILE=$W/dl/$(basename "$URL")
    echo "downloading $(basename "$URL") (about 200 MB)"
    wget -q -O "$FILE.part" "$URL" || { rm -f "$FILE.part"; echo "download failed: $URL" >&2; exit 1; }
    mv "$FILE.part" "$FILE"
fi
[ -f "$FILE" ] || { echo "no such file: $FILE" >&2; exit 1; }

# The file's own checksum (pushed with it) comes first; the list's applies to the version it names.
if [ -f "$FILE.sha256" ]; then
    want=$(cut -d' ' -f1 < "$FILE.sha256")
elif [ "$(basename "$FILE")" = "utdeck-runtime-$VER.tar.xz" ]; then
    want=$SUM
else
    echo "no checksum for $(basename "$FILE"): neither $(basename "$FILE").sha256 nor an entry in runtimes.list" >&2
    exit 1
fi
have=$(sha256sum "$FILE" | cut -d' ' -f1)
[ "$have" = "$want" ] || { echo "checksum mismatch for $(basename "$FILE"): got $have" >&2; exit 1; }

VER=$(basename "$FILE" .tar.xz | sed 's/^utdeck-runtime-//')   # e.g. proton11.0-20261120
D=runtime-$VER
case "$KIND:$VER" in proton:proton*|default:[0-9]*) ;; *) echo "not a $KIND runtime: $VER" >&2; exit 1 ;; esac
cd "$W"
rm -rf "$D.new"; mkdir "$D.new"
tar -C "$D.new" -xJf "$FILE"
rm -rf "$D"; mv "$D.new/utdeck-runtime" "$D"; rmdir "$D.new"
ln -sfn "$D" "$LINK"
case "$FILE" in "$W"/dl/*) rm -f "$FILE" "$FILE.sha256" ;; esac   # a download or a file left there is not kept; a pushed file is removed by runtime-push.sh
echo "installed: $(head -1 "$LINK/BUILD-INFO"), $(du -sh "$D" | cut -f1)"
