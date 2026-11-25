#!/bin/sh
# Build UTDeck's Windows runtime for Ubuntu Touch 24.04 (arm64) from pinned sources, in the cross
# container (tools/wine-build/Dockerfile), into one folder that is copied to the phone as it is.
#
#   tools/wine-build/build-runtime.sh            -> build/runtime/utdeck-runtime/ + BUILD-INFO
#                                                   + build/runtime/utdeck-runtime-<version>.tar.xz
#   PARTS="dxvk vkd3d" tools/wine-build/build-runtime.sh   rebuilds only those parts, then repacks
#   RUNTIME_VERSION=11.16-20261124.2 tools/wine-build/build-runtime.sh   another name than the date's
#   WINE_FLAVOR=proton tools/wine-build/build-runtime.sh   the same runtime on Proton 11's Wine
#                                                   (stage 3.3), into build/runtime-proton/, with its own
#                                                   source, build folders and patches (patches-proton/);
#                                                   the other parts are shared with the default build
#
# Parts (each pinned below, the same sources and recipes as Hangover 11.16's packages, except
# VKD3D-Proton, which Hangover does not ship, and a newer FEX):
#   wine   Wine with our patches (tools/wine-build/patches), ARM64 Unix side, ARM64EC, ARM64 and
#          i386 Windows DLLs (--enable-archs=arm64ec,aarch64,i386), stripped
#   fex    FEX as Windows DLLs: libarm64ecfex.dll (x86-64 code) and libwow64fex.dll (32-bit x86)
#   box64  Box64 as a Windows DLL: wowbox64.dll (32-bit x86, the faster choice for some programs),
#          with patches-box64/
#   dxvk   DXVK 2.7.1 (the pin for Adreno 6xx): arm64ec (x86-64 and ARM64EC games), x32 (32-bit
#          games), aarch64 (plain ARM64 programs)
#   vkd3d  VKD3D-Proton (Direct3D 12): arm64x (both kinds of 64-bit process) and x86 (32-bit)
#   mono   Wine Mono (.NET), the version this Wine asks for, found by wineboot in share/wine/mono
#   steam  Proton's lsteamclient (Steam's API for Windows games, passed on to the Linux Steam client)
#          and its steam.exe helper, built in Wine's own build tree as Proton does: ARM64X and i386
#          DLLs, an aarch64 Unix library (needs the wine part's build tree in build/runtime-obj)
#   gpu    (default runtime only, stage 4.3) the GPU driver every game uses, in lib/gpu/: our Mesa
#          (tools/mesa-build/build-zink.sh: Turnip for Vulkan and zink for OpenGL, with
#          tools/mesa-build/patches/) and Ubuntu's Khronos Vulkan loader (the phone's own libvulkan.so.1
#          is libhybris'). Stage 1.4's layout: vulkan/ (driver, icd.json), gl/ (zink's EGL, its
#          egl_vendor.json), loader/; manifests with relative paths, so the folder works wherever it is.
#          Until stage 4.3 this lived in ~/utdeck-probe/zink and ~/utdeck-probe/vk, put there by hand.
#   overlay (default runtime only, stage 5.1) the performance overlay, in lib/gpu/layers/: MangoHud
#          (tools/mangohud-build/build-mangohud.sh, with tools/mangohud-build/patches/) as a Vulkan
#          layer, with its layer file (a relative path, like lib/gpu's other manifests). wine-env.sh
#          only lists the folder for the Vulkan loader (VK_ADD_LAYER_PATH): a layer found that way is
#          loaded only when a game enables it by name, so a game without the overlay runs as before.
#   media  GStreamer plugins for games' videos and sounds, which Wine plays through the phone's
#          GStreamer 1.24 (winegstreamer): gst-libav (FFmpeg's decoders: WMV, WMA, H.264, VP9, ...)
#          with a small FFmpeg linked in, and asfdemux from gst-plugins-ugly (the reader for .wmv
#          and .wma files). The phone has neither. Proton ships its own for the same reason.
#
# Layout: bin/ (wine, wineserver and the user-facing launchers), lib/wine/{aarch64-unix,
# aarch64-windows,i386-windows}, share/wine, lib/dxvk/<arch>, lib/vkd3d-proton/<arch>,
# lib/gstreamer-1.0 (found through GST_PLUGIN_PATH, set by wine-env.sh). Wine finds
# its files relative to bin/wine, so the folder can live anywhere. Not included: headers, import
# libraries and Wine's development tools (winegcc, widl, ...), which only build programs.
set -eu

REPO_DIR=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$REPO_DIR/build/src"             # FEX, Box64, DXVK, VKD3D-Proton (Box64 writes into its tree)
WINE_SRC="$REPO_DIR/build/wine-src"   # shared with build-winewayland.sh (same commit, same patches)
OBJ="$REPO_DIR/build/runtime-obj"
OUT="$REPO_DIR/build/runtime"
TC_VER=20261104
TC="$REPO_DIR/build/toolchains/llvm-mingw-$TC_VER-ucrt-ubuntu-22.04-x86_64"
IMAGE=utdeck-wine-cross
PARTS=${PARTS:-wine fex box64 dxvk vkd3d mono steam media gpu overlay}

# The Wine base (stage 3.3): Hangover's (the default) or Proton 11's. Each has its own source folder,
# build folders (suffix WS inside build/runtime-obj) and output folder.
WINE_FLAVOR=${WINE_FLAVOR:-hangover}
case "$WINE_FLAVOR" in
proton)
    WINE_URL=https://github.com/ValveSoftware/wine.git
    WINE_COMMIT=e60892cc2398b0a83c332a2078c7b85e47bdb42b    # Proton 11.0-2e's wine submodule (Wine 11.0)
    WINE_SRC="$REPO_DIR/build/wine-proton-src"
    OUT="$REPO_DIR/build/runtime-proton"
    WS=-proton VERSION_BASE=proton11.0 PATCH_DIR=patches-proton ;;
hangover)
    WINE_URL=https://github.com/AndreRH/wine.git
    WINE_COMMIT=3ea3a14941b6a122b892c84141ce9662c008081e    # hangover-11.16 (Wine 11.16)
    WS="" VERSION_BASE=11.16 PATCH_DIR=patches ;;
*) echo "WINE_FLAVOR: hangover or proton" >&2; exit 2 ;;
esac
RT="$OUT/utdeck-runtime"
if [ -z "${PATCHES+x}" ]; then PATCHES=$(ls "$REPO_DIR"/tools/wine-build/$PATCH_DIR/*.patch 2>/dev/null || true); fi
FEX_URL=${FEX_URL:-https://github.com/FEX-Emu/FEX.git}           # FEX_URL/FEX_COMMIT: for A/B builds
FEX_COMMIT=${FEX_COMMIT:-4f9c1f1e36443d22d7655477e34f51b95b685bf1}      # FEX-2611.1 (upstream)
# (Hangover 11.16's FEX, AndreRH/FEX fa556167 of 2026-08-09, crashed 64-bit games: an x86-64 thread
# waiting in a system call resumed into Wine's syscall dispatcher; FEX-2611.1 fixes it, stage 3.2b.)
BOX64_URL=https://github.com/AndreRH/box64.git
BOX64_COMMIT=2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a   # hangover-11.16's box64 submodule
DXVK_URL=https://github.com/AndreRH/dxvk.git
DXVK_COMMIT=2c50a567945381d0c45035902eaae62f0293e6d4    # v2.7.1 + ARM64EC cross file (Hangover's)
DXVK_VERSION=v2.7.1
VKD3D_URL=https://github.com/HansKristian-Work/vkd3d-proton.git
VKD3D_COMMIT=4522cc1a8b1347f1ab1b0f7b14a23ed9444d95d1   # v3.1
VKD3D_VERSION=v3.1
# The Khronos Vulkan loader (part gpu): Ubuntu 24.04's own package, from the release pocket (its files
# stay in the archive), checked against the SHA256 of Ubuntu's signed index.
VKLOADER_VERSION=1.3.275.0-1build1
VKLOADER_URL=http://ports.ubuntu.com/ubuntu-ports/pool/main/v/vulkan-loader/libvulkan1_${VKLOADER_VERSION}_arm64.deb
VKLOADER_SHA256=9459488a53ead304d00587c874f9b31d56d59c9aeae5c46b6f78801a89d53e63
MONO_VERSION=11.3.0
MONO_URL=https://dl.winehq.org/wine/wine-mono/$MONO_VERSION/wine-mono-$MONO_VERSION-arm64.msi
MONO_SHA256=b0c47d374efaf22cbff76cb10488ac2d1627b067e085aef7acec573884120e0f   # as in Wine's appwiz.cpl
MONO_MSI="$REPO_DIR/build/toolchains/wine-mono-$MONO_VERSION-arm64.msi"
PROTON_URL=https://github.com/ValveSoftware/Proton.git
PROTON_COMMIT=bc896217ffcd67f892f952b010f898fe7bd0f0a1  # proton-11.0-2e: lsteamclient and steam_helper only
# media: release tarballs. FFmpeg 7.1 (its maintained long-term branch; its signature checked with
# FFmpeg's release key FCF986EA15E6E293A5644F10B4322F04D67658D8 when pinned), GStreamer 1.24.x (the
# phone's series: plugins built against its 1.24.2 headers load in any 1.24; sha256 as published).
FFMPEG_VERSION=7.1.5
FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
GST_VERSION=1.24.13
GST_LIBAV_SHA256=150e2b70588fa32a1294f42665756f2175417ce4b5988e2c2081b683719aa6c1
GST_UGLY_SHA256=dc08bb11dce0a43453466fb9034e4fe06709fb5af68475bcf6d288693b661a5d
# The decoders games use for videos and sounds: Windows Media (WMV 7-9, VC-1, WMA, XMA), MPEG-1/2/4,
# H.264, HEVC, VP8/VP9, Theora, old AVI codecs (Cinepak, Indeo, MS Video 1, ...), AAC, MP3, Vorbis,
# Opus, FLAC, AC-3, ADPCM, ATRAC. LGPL only (no --enable-gpl), no external libraries, no network.
FFMPEG_DECODERS="h264 hevc vp8 vp9 mpeg1video mpeg2video mpeg4 h263 h263p msmpeg4v1 msmpeg4v2 msmpeg4v3
    wmv1 wmv2 wmv3 wmv3image vc1 vc1image theora vp3 vp6 vp6a vp6f flv mjpeg cinepak indeo2 indeo3 indeo4
    indeo5 msvideo1 msrle huffyuv ffvhuff utvideo aac aac_latm mp1 mp1float mp2 mp2float mp3 mp3float
    vorbis opus flac alac wmav1 wmav2 wmapro wmavoice wmalossless xma1 xma2 ac3 eac3 adpcm_ms
    adpcm_ima_wav adpcm_ima_qt adpcm_xa gsm_ms atrac3 atrac3p atrac9"

has() { case " $PARTS " in *" $1 "*) return 0;; esac; return 1; }

# fetch DIR URL COMMIT: a shallow checkout of exactly COMMIT, with its submodules.
fetch() {
    if [ ! -d "$1/.git" ]; then git init -q "$1"; git -C "$1" remote add origin "$2"; fi
    git -C "$1" remote set-url origin "$2"
    if [ "$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        git -C "$1" fetch -q --depth 1 origin "$3"
        git -C "$1" checkout -q --force --detach "$3"
    fi
    git -C "$1" submodule update -q --init --recursive --depth 1
}

# in_container SCRIPT: runs SCRIPT (sh) in the cross container, as us, with the sources, the
# object folder, the output folder and llvm-mingw mounted. llvm-strip stands in for `strip`
# (DXVK's ARM64EC cross file names plain `strip`, which cannot read ARM64 PE files).
# SOURCE_DATE_EPOCH (the Wine commit's time) replaces the build time that the linker would stamp
# into every PE file, so two builds of the same sources are identical byte for byte.
in_container() {
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e SOURCE_DATE_EPOCH="$EPOCH" -e WS="$WS" \
        -v "$SRC":/src -v "$WINE_SRC":/wine:ro -v "$OBJ":/obj -v "$OUT":/out -v "$TC":/opt/llvm-mingw:ro \
        "$IMAGE" sh -euc '
            mkdir -p /tmp/bin && ln -sf /opt/llvm-mingw/bin/llvm-strip /tmp/bin/strip
            export PATH=/tmp/bin:/opt/llvm-mingw/bin:$PATH J=$(nproc)
            git config --global --add safe.directory "*"
            '"$1"
}

[ -x "$TC/bin/clang" ] || "$REPO_DIR/tools/ut-probe/win/build.sh" >/dev/null   # fetches llvm-mingw
docker image inspect utdeck-mesa-cross >/dev/null 2>&1 || docker build -t utdeck-mesa-cross "$REPO_DIR/tools/mesa-build"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$REPO_DIR/tools/wine-build"
mkdir -p "$SRC" "$OBJ" "$OUT"

# ---- sources
if has wine; then
    fetch "$WINE_SRC" "$WINE_URL" "$WINE_COMMIT"
    git -C "$WINE_SRC" checkout -q --force --detach "$WINE_COMMIT"
    git -C "$WINE_SRC" clean -fdq
    for p in $PATCHES; do git -C "$WINE_SRC" apply "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"; done
    # Proton's Wine keeps no generated files in git (configure, the server protocol, the DLL specs,
    # winevulkan's thunks): its autogen.sh makes them (Perl, Python with the vk.xml in the tree, no
    # download, autoreconf), as Proton's own build does. Run in the container, the source writable.
    if [ ! -x "$WINE_SRC/configure" ] && [ -x "$WINE_SRC/autogen.sh" ]; then
        echo "wine: generating configure and the generated sources (autogen.sh)"
        docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e XDG_CACHE_HOME=/tmp -v "$WINE_SRC":/wine "$IMAGE" \
            sh -euc 'cd /wine && ./autogen.sh' > "$OBJ/wine-autogen$WS.log" 2>&1 \
            || { tail -20 "$OBJ/wine-autogen$WS.log" >&2; exit 1; }
    fi
fi
has fex && fetch "$SRC/fex" "$FEX_URL" "$FEX_COMMIT"
if has box64; then
    # Box64's WoW64 module at this pin does not tell Wine which x86 features it emulates
    # (BTCpuIsProcessorFeaturePresent): 32-bit programs were told "no MMX, no SSE", and some refuse to
    # run (Microsoft's XAudio2 2.7). The upstream fix (ptitSeb/box64 ec1d5a6, #4286), in patches-box64/.
    fetch "$SRC/box64" "$BOX64_URL" "$BOX64_COMMIT"
    git -C "$SRC/box64" checkout -q --force -- . && git -C "$SRC/box64" clean -fdq
    for p in "$REPO_DIR"/tools/wine-build/patches-box64/*.patch; do git -C "$SRC/box64" apply "$p"; done
fi
if has dxvk; then
    # DXVK 2.7.1 does not compile with LLVM 23's libc++: two upstream fixes, in patches-dxvk/.
    fetch "$SRC/dxvk" "$DXVK_URL" "$DXVK_COMMIT"
    git -C "$SRC/dxvk" checkout -q --force -- .
    for p in "$REPO_DIR"/tools/wine-build/patches-dxvk/*.patch; do git -C "$SRC/dxvk" apply "$p"; done
fi
has vkd3d && fetch "$SRC/vkd3d-proton" "$VKD3D_URL" "$VKD3D_COMMIT"
if has steam; then
    # Only the two folders: Proton's other submodules (Wine, DXVK, gstreamer, ...) are large and unused.
    if [ "$(git -C "$SRC/proton" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$PROTON_COMMIT" ]; then
        [ -d "$SRC/proton/.git" ] || { git init -q "$SRC/proton" && git -C "$SRC/proton" remote add origin "$PROTON_URL"; }
        git -C "$SRC/proton" sparse-checkout set --no-cone /lsteamclient/ /steam_helper/
        git -C "$SRC/proton" fetch -q --depth 1 --filter=blob:none origin "$PROTON_COMMIT"
        git -C "$SRC/proton" checkout -q --force --detach "$PROTON_COMMIT"
    fi
fi
if has media; then
    # fetch_tar URL SHA256: download into build/src/dl once, check, unpack fresh into build/src.
    fetch_tar() {
        f="$SRC/dl/${1##*/}"; mkdir -p "$SRC/dl"
        [ -f "$f" ] || curl -fsSL -o "$f" "$1"
        echo "$2  $f" | sha256sum -c --quiet
        d="$SRC/$(basename "$f" .tar.xz)"; rm -rf "$d"; tar -C "$SRC" -xJf "$f"
    }
    fetch_tar https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz $FFMPEG_SHA256
    fetch_tar https://gstreamer.freedesktop.org/src/gst-libav/gst-libav-$GST_VERSION.tar.xz $GST_LIBAV_SHA256
    fetch_tar https://gstreamer.freedesktop.org/src/gst-plugins-ugly/gst-plugins-ugly-$GST_VERSION.tar.xz $GST_UGLY_SHA256
fi
# gpu: only for the default runtime (both Wine bases use the default runtime's driver).
if has gpu && [ "$WINE_FLAVOR" = hangover ]; then
    echo "gpu: building Mesa (Turnip and zink) with tools/mesa-build/build-zink.sh (log: build/runtime-obj/mesa-build.log)"
    mkdir -p "$OBJ"
    OUT="$OBJ/mesa" "$REPO_DIR/tools/mesa-build/build-zink.sh" > "$OBJ/mesa-build.log" 2>&1 \
        || { tail -30 "$OBJ/mesa-build.log" >&2; exit 1; }
    deb="$OBJ/$(basename "$VKLOADER_URL")"
    if [ ! -f "$deb" ]; then
        curl -fsSL -o "$deb.part" "$VKLOADER_URL" || { echo "download failed: $VKLOADER_URL" >&2; exit 1; }
        mv "$deb.part" "$deb"
    fi
    echo "$VKLOADER_SHA256  $deb" | sha256sum -c - > /dev/null || { echo "checksum mismatch: $deb" >&2; rm -f "$deb"; exit 1; }
    rm -rf "$OBJ/vkloader" && dpkg-deb -x "$deb" "$OBJ/vkloader"
fi
# overlay: only for the default runtime, like gpu (both Wine bases use its lib/gpu).
if has overlay && [ "$WINE_FLAVOR" = hangover ]; then
    echo "overlay: building MangoHud with tools/mangohud-build/build-mangohud.sh (log: build/runtime-obj/mangohud-build.log)"
    mkdir -p "$OBJ"
    OUT="$OBJ/mangohud" "$REPO_DIR/tools/mangohud-build/build-mangohud.sh" > "$OBJ/mangohud-build.log" 2>&1 \
        || { tail -30 "$OBJ/mangohud-build.log" >&2; exit 1; }
fi
if has mono; then
    [ -f "$MONO_MSI" ] || curl -fsSL -o "$MONO_MSI" "$MONO_URL"
    echo "$MONO_SHA256  $MONO_MSI" | sha256sum -c --quiet
fi

EPOCH=$(git -C "$WINE_SRC" log -1 --format=%ct "$WINE_COMMIT")   # the time stamped into PE files

# ---- Wine: build machine tools (amd64), then the arm64 build as Hangover's Ubuntu package does
if has wine; then
    echo "wine: building $WINE_FLAVOR's Wine (log: build/runtime-obj/wine-arm64$WS/make.log)"
    in_container '
        mkdir -p /obj/wine-tools$WS && cd /obj/wine-tools$WS
        [ -f Makefile ] || /wine/configure --enable-win64 --without-x --without-wayland \
            --without-mingw --disable-tests > configure.log 2>&1
        make -j$J __tooldeps__ > make.log 2>&1
        make -j$J -C nls >> make.log 2>&1     # the locale data wrc needs to compile resources
        mkdir -p /obj/wine-arm64$WS && cd /obj/wine-arm64$WS
        export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
        [ -f Makefile ] || /wine/configure --prefix=/usr --with-mingw=clang --enable-archs=arm64ec,aarch64,i386 \
            --disable-tests --without-x --host=aarch64-linux-gnu --with-wine-tools=/obj/wine-tools$WS \
            CC=aarch64-linux-gnu-gcc PKG_CONFIG=pkg-config > configure.log 2>&1
        grep -q "define HAVE_FT2BUILD_H 1" include/config.h || { echo "FreeType not found: win32u would have no fonts" >&2; exit 1; }
        # configure leaves out quietly what it cannot find: fail if any of these went missing
        # (sound, gamepads, USB, Bluetooth, video, TLS, Kerberos, fonts, Wayland).
        grep "^configure:" configure.log | grep -v -E "creating|Finished" > configure-notes.txt || true
        if grep -i -E "pulse|alsa|sdl|udev|usb|dbus|gstreamer|gnutls|krb5|kerberos|freetype|fontconfig|wayland|xkb" configure-notes.txt; then
            echo "Wine configure left out the features above: a build dependency is missing" >&2; exit 1
        fi
        make -j$J > make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
        for m in winepulse.drv/winepulse winealsa.drv/winealsa winebus.sys/winebus wineusb.sys/wineusb winebth.sys/winebth \
                 winegstreamer/winegstreamer winevulkan/winevulkan winewayland.drv/winewayland secur32/secur32; do
            [ -f dlls/$m.so ] || { echo "dlls/$m.so was not built" >&2; exit 1; }
        done
        rm -rf /obj/wine-dest$WS
        make install DESTDIR=/obj/wine-dest$WS > install.log 2>&1
    '
fi

# ---- FEX: two Windows DLLs, built with FEX's MinGW toolchain file (Hangover's flags)
if has fex; then
    echo "fex: building"
    FEX_VER="FEX-$(git -C "$SRC/fex" rev-parse --short=8 HEAD)-utdeck"   # goes into the CPU name: under 32 chars
    in_container '
        for t in "aarch64-w64-mingw32 wow64fex libwow64fex.dll" "arm64ec-w64-mingw32 arm64ecfex libarm64ecfex.dll"; do
            set -- $t
            cmake -S /src/fex -B /obj/fex-$1 -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_TOOLCHAIN_FILE=/src/fex/Data/CMake/toolchain_mingw.cmake -DMINGW_TRIPLE=$1 \
                -DENABLE_JEMALLOC_GLIBC_ALLOC=False -DENABLE_ASSERTIONS=False -DENABLE_LTO=False \
                -DTUNE_CPU=none -DBUILD_TESTING=False -DOVERRIDE_VERSION="'"$FEX_VER"'" \
                > /obj/fex-$1.configure.log 2>&1 || { tail -30 /obj/fex-$1.configure.log >&2; exit 1; }
            ninja -C /obj/fex-$1 $2 > /obj/fex-$1.make.log 2>&1 || { tail -30 /obj/fex-$1.make.log >&2; exit 1; }
            $1-strip --strip-unneeded -o /obj/$3 /obj/fex-$1/Bin/$3
        done
    '
fi

# ---- Box64: wowbox64.dll (Box64's own CMake builds the DLL as an external project)
if has box64; then
    echo "box64: building"
    in_container '
        cmake -S /src/box64 -B /obj/box64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
            -DARM_DYNAREC=ON -DWOW64=ON > /obj/box64.configure.log 2>&1 || { tail -30 /obj/box64.configure.log >&2; exit 1; }
        make -C /obj/box64 -j$J wowbox64 > /obj/box64.make.log 2>&1 || { tail -30 /obj/box64.make.log >&2; exit 1; }
        aarch64-w64-mingw32-strip --strip-unneeded -o /obj/wowbox64.dll /obj/box64/wowbox64-prefix/src/wowbox64-build/wowbox64.dll
    '
fi

# ---- DXVK and VKD3D-Proton: their own release scripts (stripped, no tarball)
if has dxvk; then
    echo "dxvk: building"
    in_container '
        rm -rf /obj/dxvk && mkdir -p /obj/dxvk
        /src/dxvk/package-release.sh '"$DXVK_VERSION"' /obj/dxvk --no-package > /obj/dxvk.log 2>&1 || { tail -30 /obj/dxvk.log >&2; exit 1; }
    '
fi
if has vkd3d; then
    echo "vkd3d-proton: building"
    in_container '
        rm -rf /obj/vkd3d && mkdir -p /obj/vkd3d
        /src/vkd3d-proton/package-release.sh '"$VKD3D_VERSION"' /obj/vkd3d --no-package --build-arm64x > /obj/vkd3d.log 2>&1 || { tail -30 /obj/vkd3d.log >&2; exit 1; }
    '
fi

# ---- lsteamclient and steam.exe: Proton builds them with Wine's makedep inside Wine's build tree
# (its Wine's makedep accepts modules from outside the tree; ours does not). So both modules are
# added to a copy of the tree made of links: steam-src (Wine's sources plus dlls/lsteamclient and
# programs/steam) and steam-obj (our finished wine-arm64 build plus a copy of its include folder),
# and only their five files are built. The Unix side is C++ and links with gcc: -lstdc++ added.
if has steam; then
    echo "steam: building lsteamclient and steam.exe (log: build/runtime-obj/steam-make.log)"
    rm -rf "$OBJ/steam-src$WS" "$OBJ/steam-obj$WS" "$OBJ/steam-out$WS"
    in_container '
        S=/obj/steam-src$WS B=/obj/steam-obj$WS
        mkdir -p $S/dlls $S/programs $B/dlls $B/programs /obj/steam-out$WS
        for f in /wine/*; do n=${f##*/}; case $n in dlls|programs) ;; *) ln -s $f $S/$n ;; esac; done
        for f in /wine/dlls/* /wine/programs/*; do ln -s $f $S/${f#/wine/}; done
        ln -s /src/proton/lsteamclient $S/dlls/lsteamclient
        ln -s /src/proton/steam_helper $S/programs/steam
        for f in /obj/wine-arm64$WS/*; do n=${f##*/}; case $n in dlls|programs|include|Makefile|config.status) ;; *) ln -s $f $B/$n ;; esac; done
        for f in /obj/wine-arm64$WS/dlls/* /obj/wine-arm64$WS/programs/*; do ln -s $f $B/${f#/obj/wine-arm64$WS/}; done
        cp -a /obj/wine-arm64$WS/include $B/include
        touch -r /obj/wine-arm64$WS/config.status $B/config.status   # older than include/stamp-h: no reconfigure
        sed -e "1 i\\UNIX_LIBS = -lstdc++" \
            -e "/^all:\$/,\$c all:" \
            -e "s#^SUBDIRS = *\\\\\$#SUBDIRS = dlls/lsteamclient programs/steam \\\\#" \
            -e "/^TOP_INSTALL_LIB/c TOP_INSTALL_LIB =" -e "/^TOP_INSTALL_DEV/c TOP_INSTALL_DEV =" \
            -e "/^srcdir/c srcdir = $S" /obj/wine-arm64$WS/Makefile > $B/Makefile
        cd $B && /obj/wine-tools$WS/tools/makedep
        T="dlls/lsteamclient/aarch64-windows/lsteamclient.dll dlls/lsteamclient/i386-windows/lsteamclient.dll
           dlls/lsteamclient/lsteamclient.so programs/steam/aarch64-windows/steam.exe programs/steam/i386-windows/steam.exe"
        make -j$J $T > /obj/steam-make$WS.log 2>&1 || { grep -E "error:|Error [0-9]" /obj/steam-make$WS.log | head -20 >&2; exit 1; }
        for t in $T; do a=${t%/*}; a=${a##*/}; case $a in lsteamclient|steam) a=aarch64-unix ;; esac
            mkdir -p /obj/steam-out$WS/$a && cp $t /obj/steam-out$WS/$a/; done
    '
fi

# ---- media: FFmpeg as static libraries (position-independent), linked into gst-libav's plugin with
# its symbols hidden (--exclude-libs), so it cannot clash with the phone's own libavcodec60, which
# other programs load; then asfdemux alone from gst-plugins-ugly. Both against the container's
# GStreamer 1.24.2 headers, the phone's version.
if has media; then
    echo "media: building FFmpeg $FFMPEG_VERSION, gst-libav and asfdemux $GST_VERSION (logs: build/runtime-obj/media-*.log)"
    cp "$REPO_DIR/tools/mesa-build/cross-aarch64.ini" "$OBJ/media-cross.ini"
    in_container '
        M=/obj/media; rm -rf $M /obj/media-out; mkdir -p $M/ffmpeg /obj/media-out
        cd $M/ffmpeg
        /src/ffmpeg-'"$FFMPEG_VERSION"'/configure --prefix=$M/ff --enable-cross-compile --arch=aarch64 \
            --target-os=linux --cross-prefix=aarch64-linux-gnu- --pkg-config=pkg-config \
            --enable-pic --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
            --disable-network --disable-autodetect --disable-everything --disable-swscale --disable-swresample \
            --enable-decoder='"$(echo $FFMPEG_DECODERS | tr ' ' ,)"' --enable-demuxer=xwma \
            --enable-filter=buffer,buffersink,yadif > /obj/media-ffmpeg.log 2>&1 \
            || { tail -20 /obj/media-ffmpeg.log >&2; exit 1; }
        make -j$J >> /obj/media-ffmpeg.log 2>&1 && make install >> /obj/media-ffmpeg.log 2>&1 \
            || { grep -E "error|Error" /obj/media-ffmpeg.log | head -20 >&2; exit 1; }
        export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
        meson setup $M/libav /src/gst-libav-'"$GST_VERSION"' --cross-file /obj/media-cross.ini --buildtype=release \
            --wrap-mode=nofallback -Dpkg_config_path=$M/ff/lib/pkgconfig -Ddoc=disabled -Dtests=disabled \
            -Dc_link_args=-Wl,--exclude-libs,ALL > /obj/media-libav.log 2>&1 && ninja -C $M/libav >> /obj/media-libav.log 2>&1 \
            || { tail -30 /obj/media-libav.log >&2; exit 1; }
        meson setup $M/ugly /src/gst-plugins-ugly-'"$GST_VERSION"' --cross-file /obj/media-cross.ini --buildtype=release \
            --wrap-mode=nofallback -Dauto_features=disabled -Dasfdemux=enabled -Dgpl=disabled \
            > /obj/media-ugly.log 2>&1 && ninja -C $M/ugly >> /obj/media-ugly.log 2>&1 \
            || { tail -30 /obj/media-ugly.log >&2; exit 1; }
        cp $M/libav/ext/libav/libgstlibav.so $M/ugly/gst/asfdemux/libgstasf.so /obj/media-out/
        # FFmpeg must be inside the plugin, not needed from the phone
        if aarch64-linux-gnu-readelf -d /obj/media-out/*.so | grep NEEDED | grep -E "libav|libsw"; then
            echo "media: a plugin links to a system FFmpeg library" >&2; exit 1
        fi
    '
fi

# ---- assemble the runtime folder from whatever has been built
echo "assembling build/runtime/utdeck-runtime"
in_container '
    R=/out/utdeck-runtime D=/obj/wine-dest$WS/usr
    rm -rf $R && mkdir -p $R/bin $R/lib/wine $R/share
    # Proton 11.0'"'"'s makedep does not build tools/wine (the bin/wine launcher) when cross-compiling with
    # separate tools, as we do (Valve builds natively on ARM64): bin/wine is then a link to the loader,
    # which finds its files from its real place (Debian packages Wine the same way).
    [ -e $D/bin/wine ] || ln -s ../lib/wine/aarch64-unix/wine $R/bin/wine
    for f in wine wineserver wineboot winecfg wineconsole winedbg winepath msiexec regedit regsvr32 notepad winefile; do
        [ -e $D/bin/$f ] || [ -L $D/bin/$f ] && [ "$f" != wine -o -e $D/bin/wine ] && cp -a $D/bin/$f $R/bin/
    done
    cp -a $D/lib/wine/aarch64-unix $D/lib/wine/aarch64-windows $D/lib/wine/i386-windows $R/lib/wine/
    cp -a $D/share/wine $R/share/
    find $R/lib/wine -name "*.a" -delete
    # Unix side: plain strip. Windows side: debug info out (exports and relocations stay).
    # (strip replaces a symbolic link with a stripped copy: a linked bin/wine is left alone, its target
    # is the loader in lib/wine/aarch64-unix, stripped with the rest)
    aarch64-linux-gnu-strip --strip-unneeded $([ -L $R/bin/wine ] || echo $R/bin/wine) $R/bin/wineserver \
        $R/lib/wine/aarch64-unix/*.so $R/lib/wine/aarch64-unix/wine
    find $R/lib/wine/aarch64-windows $R/lib/wine/i386-windows -type f \( -name "*.dll" -o -name "*.exe" -o -name "*.drv" \
        -o -name "*.sys" -o -name "*.cpl" -o -name "*.ocx" -o -name "*.acm" -o -name "*.ax" -o -name "*.ds" \) \
        -exec llvm-strip --strip-debug {} +
    cp /obj/libarm64ecfex.dll /obj/libwow64fex.dll /obj/wowbox64.dll $R/lib/wine/aarch64-windows/
    if [ -d /obj/steam-out$WS ]; then
        cp -a /obj/steam-out$WS/aarch64-windows /obj/steam-out$WS/i386-windows /obj/steam-out$WS/aarch64-unix $R/lib/wine/
        aarch64-linux-gnu-strip --strip-unneeded $R/lib/wine/aarch64-unix/lsteamclient.so
        llvm-strip --strip-debug $R/lib/wine/*-windows/lsteamclient.dll $R/lib/wine/*-windows/steam.exe
    fi
    mkdir -p $R/lib/dxvk $R/lib/vkd3d-proton
    for a in arm64ec x32 aarch64; do cp -a /obj/dxvk/dxvk-'"$DXVK_VERSION"'/$a $R/lib/dxvk/; done
    for a in arm64x x86; do cp -a /obj/vkd3d/vkd3d-proton-'"$VKD3D_VERSION"'/$a $R/lib/vkd3d-proton/; done
    if [ -d /obj/media-out ]; then
        mkdir -p $R/lib/gstreamer-1.0 && cp /obj/media-out/*.so $R/lib/gstreamer-1.0/
        aarch64-linux-gnu-strip --strip-unneeded $R/lib/gstreamer-1.0/*.so
    fi
'
mkdir -p "$RT/share/wine/mono"
cp "$MONO_MSI" "$RT/share/wine/mono/"

# The GPU driver (part gpu), into lib/gpu with stage 1.4's layout.
M="$OBJ/mesa/stage/usr"
if [ "$WINE_FLAVOR" = hangover ] && [ -f "$M/lib/aarch64-linux-gnu/libvulkan_freedreno.so" ] \
        && [ -f "$OBJ/vkloader/usr/lib/aarch64-linux-gnu/libvulkan.so.1" ]; then
    G="$RT/lib/gpu"
    mkdir -p "$G/vulkan" "$G/gl" "$G/loader"
    cp -L "$M/lib/aarch64-linux-gnu/libvulkan_freedreno.so" "$G/vulkan/"
    cp -L "$M/lib/aarch64-linux-gnu/libEGL_mesa.so.0" "$M"/lib/aarch64-linux-gnu/libgallium-*.so "$G/gl/"
    cp -L "$OBJ/vkloader/usr/lib/aarch64-linux-gnu/libvulkan.so.1" "$G/loader/"
    api=$(sed -n 's/.*"api_version": *"\([^"]*\)".*/\1/p' "$M"/share/vulkan/icd.d/freedreno_icd.*.json)
    printf '{\n    "ICD": {\n        "api_version": "%s",\n        "library_arch": "64",\n        "library_path": "./libvulkan_freedreno.so"\n    },\n    "file_format_version": "1.0.1"\n}\n' "$api" > "$G/vulkan/icd.json"
    printf '{\n    "file_format_version" : "1.0.0",\n    "ICD" : {\n        "library_path" : "./libEGL_mesa.so.0"\n    }\n}\n' > "$G/gl/egl_vendor.json"
    cp "$OBJ/mesa/BUILD-INFO" "$G/BUILD-INFO"
fi
# The performance overlay (part overlay), into lib/gpu/layers: the layer and its file, named like the
# layer MangoHud registers (VK_LAYER_MANGOHUD_overlay_aarch64), with the library next to it.
H="$OBJ/mangohud/stage/usr"
if [ "$WINE_FLAVOR" = hangover ] && [ -f "$H/lib/aarch64-linux-gnu/libMangoHud.so" ]; then
    L="$RT/lib/gpu/layers"
    rm -rf "$L" && mkdir -p "$L"
    cp -L "$H/lib/aarch64-linux-gnu/libMangoHud.so" "$L/"
    sed 's#"library_path": *"[^"]*"#"library_path": "./libMangoHud.so"#' \
        "$H"/share/vulkan/implicit_layer.d/MangoHud.*.json > "$L/MangoHud.json"
    grep -q '"library_path": "./libMangoHud.so"' "$L/MangoHud.json" || { echo "overlay: layer file not rewritten" >&2; exit 1; }
    cp "$OBJ/mangohud/BUILD-INFO" "$L/BUILD-INFO"
fi

# The runtime's name: the Wine base's version and the build date. RUNTIME_VERSION=... names it
# otherwise, for a second release on one day (11.16-20261124.2: get-runtime.sh takes a default
# runtime whose name starts with a digit, a Proton one whose name starts with "proton").
VERSION=${RUNTIME_VERSION:-"$VERSION_BASE-$(date -u +%Y%m%d)"}
{
    echo "UTDeck runtime $VERSION"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ), llvm-mingw $TC_VER, $IMAGE (Ubuntu 24.04, arm64 libraries from noble + UBports 24.04-2.x)"
    echo "wine: $WINE_URL $WINE_COMMIT ($(cat "$WINE_SRC/VERSION"))"
    echo "wine patches: $(for p in $PATCHES; do printf '%s ' "$(basename "$p")"; done)"
    echo "wine features left out by configure (not needed or not on the phone):"
    sed 's/^configure: /  /' "$OBJ/wine-arm64$WS/configure-notes.txt"
    echo "fex: $FEX_URL $FEX_COMMIT"
    echo "box64: $BOX64_URL $BOX64_COMMIT, patches: $(cd "$REPO_DIR/tools/wine-build/patches-box64" && ls | tr '\n' ' ')"
    echo "dxvk: $DXVK_URL $DXVK_COMMIT ($DXVK_VERSION), patches: $(cd "$REPO_DIR/tools/wine-build/patches-dxvk" && ls | tr '\n' ' ')"
    echo "vkd3d-proton: $VKD3D_URL $VKD3D_COMMIT ($VKD3D_VERSION)"
    echo "wine-mono: $MONO_URL sha256 $MONO_SHA256"
    if [ -f "$RT/lib/gpu/BUILD-INFO" ]; then
        echo "gpu: Mesa $(sed -n 's/^mesa version: //p' "$RT/lib/gpu/BUILD-INFO") ($(sed -n 's/^source: //p' "$RT/lib/gpu/BUILD-INFO") $(sed -n 's/^commit: \([0-9a-f]*\).*/\1/p' "$RT/lib/gpu/BUILD-INFO")), Turnip and zink, patches: $(sed -n 's/^extra patches: //p' "$RT/lib/gpu/BUILD-INFO")"
        echo "  Khronos Vulkan loader: libvulkan1 $VKLOADER_VERSION ($VKLOADER_URL) sha256 $VKLOADER_SHA256"
    fi
    if [ -f "$RT/lib/gpu/layers/BUILD-INFO" ]; then
        echo "overlay: MangoHud $(sed -n 's/^mangohud version: //p' "$RT/lib/gpu/layers/BUILD-INFO") ($(sed -n 's/^source: //p' "$RT/lib/gpu/layers/BUILD-INFO")), patches: $(sed -n 's/^extra patches: //p' "$RT/lib/gpu/layers/BUILD-INFO")"
    fi
    [ -f "$RT/lib/wine/aarch64-unix/lsteamclient.so" ] && echo "lsteamclient, steam.exe: $PROTON_URL $PROTON_COMMIT (proton-11.0-2e)"
    if [ -f "$RT/lib/gstreamer-1.0/libgstlibav.so" ]; then
        echo "gstreamer plugins: gst-libav and asfdemux (gst-plugins-ugly) $GST_VERSION, FFmpeg $FFMPEG_VERSION linked in"
        echo "  ffmpeg sha256 $FFMPEG_SHA256, gst-libav $GST_LIBAV_SHA256, gst-plugins-ugly $GST_UGLY_SHA256"
        echo "  ffmpeg decoders: $(echo $FFMPEG_DECODERS)"
    fi
    echo "size: $(du -sh "$RT" | cut -f1)"
    echo "sha256:"
    (cd "$RT" && sha256sum bin/wine bin/wineserver lib/wine/aarch64-unix/ntdll.so lib/wine/aarch64-unix/win32u.so \
        lib/wine/aarch64-unix/winewayland.so lib/wine/aarch64-windows/libarm64ecfex.dll \
        lib/wine/aarch64-windows/libwow64fex.dll lib/wine/aarch64-windows/wowbox64.dll \
        lib/dxvk/arm64ec/d3d11.dll lib/vkd3d-proton/arm64x/d3d12core.dll \
        $(cd "$RT" && ls lib/gpu/vulkan/*.so lib/gpu/gl/*.so* lib/gpu/loader/*.so* lib/gpu/layers/*.so 2>/dev/null) | sed 's/^/  /')
} > "$RT/BUILD-INFO"
rm -f "$OUT"/utdeck-runtime-*.tar.xz
tar -C "$OUT" -cJf "$OUT/utdeck-runtime-$VERSION.tar.xz" utdeck-runtime
cat "$RT/BUILD-INFO"
ls -l "$OUT/utdeck-runtime-$VERSION.tar.xz"
