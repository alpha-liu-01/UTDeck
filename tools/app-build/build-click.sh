#!/bin/sh
# Packages UTDeck's app as a click (stage 4.1), after the Dolphin port's ut/click/build-click.sh:
#
#   tools/app-build/build.sh && tools/app-build/build-click.sh     build/app-click/<name>_<version>_arm64.click
#
# Install: tools/app-build/install-click.sh (through the click service, as clickable does).
#
# Environment:
#   CLICK_NAME        the package name (default utdeck.alphaliu01). It names the app's folders and
#                     its app id (utdeck.alphaliu01_utdeck), which games' windows carry (stage 4.1).
#   CLICK_MAINTAINER  "Name <email>" (default: git config user.name and user.email of this checkout;
#                     not stored in the repository).
#   CLICK_VERSION     the version before the release suffix (default: app/CMakeLists.txt's). A changed
#                     .apparmor file needs a new version: reinstalling one keeps the old profile.
#   CLICK_UT          24.04-2.x (default) or 24.04-1.x. 24.04-1.x has no Qt 6, so that click also
#                     bundles the Qt 6 runtime the container unpacks into /opt/ut-runtime-1x
#                     (tools/app-build/docker/runtime-24.04-1.x.list).
#
# The version ends in ".2x" or ".1x" (the Dolphin port's stage 7): a store offers a phone the highest
# version it can install, 2.x phones accept the 1.x framework too, and "0.1.0.1x" sorts below
# "0.1.0.2x". Not "~1x": systemd escapes "~" in the app's unit name and Lomiri cannot read the app
# id back.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
name=${CLICK_NAME:-utdeck.alphaliu01}
ut=${CLICK_UT:-24.04-2.x}
case "$ut" in
  24.04-2.x) out=build/app-click; policy=2404.2; bundle=0; suffix=.2x ;;
  24.04-1.x) out=build/app-click/ut-24.04-1.x; policy=2404.1; bundle=1; suffix=.1x ;;
  *) echo "CLICK_UT: 24.04-2.x or 24.04-1.x, not $ut" >&2; exit 2 ;;
esac
[ -f "$root/build/app/utdeck" ] || { echo "no build: run tools/app-build/build.sh first" >&2; exit 1; }
# An app source newer than the build: the click would carry the old app under the new version.
newer=$(find "$root/app" -newer "$root/build/app/utdeck" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' -o -name CMakeLists.txt \) | head -1)
[ -z "$newer" ] || { echo "the build is older than ${newer#$root/}: run tools/app-build/build.sh first" >&2; exit 1; }
maintainer=${CLICK_MAINTAINER:-"$(git -C "$root" config user.name) <$(git -C "$root" config user.email)>"}
base=$(sed -n 's/^project(utdeck VERSION \([0-9.]*\).*/\1/p' "$root/app/CMakeLists.txt")
version=${CLICK_VERSION:-$base}$suffix
commit=$(git -C "$root" rev-parse --short HEAD)
remote=$(git -C "$root" remote get-url origin 2>/dev/null | sed 's|^git@github.com:|https://github.com/|; s|\.git$||')
source="${remote:-UTDeck} at commit $commit"
git -C "$root" diff --quiet HEAD -- app tools/app-build || source="$source, with uncommitted changes (a development build)"

stage=build/app-click/stage-$ut/$name
mkdir -p "$root/$out"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:/src" -w /src utdeck-app-cross sh -euc "
  rm -rf '$stage'
  mkdir -p '$stage/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration'
  aarch64-linux-gnu-strip -o '$stage/utdeck' build/app/utdeck
  cp app/click/launcher.sh app/click/utdeck.desktop app/click/utdeck.apparmor app/click/utdeck.svg '$stage/'
  # The first-time setup (stage 4.2): its script, and every UTDeck file a home folder needs, at its
  # place (tools/ut-probe/home-files.sh), with the version they came with.
  mkdir -p '$stage/setup'
  cp tools/ut-probe/utdeck-setup.sh '$stage/setup/'
  chmod 755 '$stage/setup/utdeck-setup.sh'
  sh tools/ut-probe/home-files.sh '$stage/setup/home'
  echo '$version' > '$stage/setup/home/VERSION'
  chmod 755 '$stage/launcher.sh' '$stage/utdeck'
  sed -i 's/\"policy_version\": .*/\"policy_version\": $policy/' '$stage/utdeck.apparmor'
  q=/opt/ut-bundle/usr/lib/aarch64-linux-gnu
  cp -L \$q/libQt6WlShellIntegration.so.6 '$stage/lib/aarch64-linux-gnu/'
  # ZXing-C++, for the sign-in's QR code (stage 4.2); libX11 and XTest are on every 24.04 phone.
  cp -L /usr/lib/aarch64-linux-gnu/libZXing.so.3 '$stage/lib/aarch64-linux-gnu/'
  cp \$q/qt6/plugins/wayland-shell-integration/libxdg-shell.so \
     '$stage/lib/aarch64-linux-gnu/qt6/plugins/wayland-shell-integration/'
  if [ '$bundle' = 1 ]; then
    # 24.04-1.x: the Qt 6 runtime. The libraries without the EGLFS, xcb and compositor ones, the
    # plugins the app uses, all QML modules (the Dolphin port's list and selection).
    r=/opt/ut-runtime-1x/usr/lib/aarch64-linux-gnu
    l='$stage/lib/aarch64-linux-gnu'
    for f in \$r/lib*.so*; do
      case \$f in *XcbQpa*|*EglFS*|*EglFs*|*WaylandEglCompositor*|*WlShellIntegration*) continue ;; esac
      cp -P \$f \$l/
    done
    for p in platforms/libqwayland.so wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
             platforminputcontexts/libmaliitplatforminputcontextplugin.so \
             imageformats iconengines tls; do
      mkdir -p \$l/qt6/plugins/\$(dirname \$p)
      cp -r \$r/qt6/plugins/\$p \$l/qt6/plugins/\$p
    done
    cp -r \$r/qt6/qml \$l/qt6/qml
    mkdir -p '$stage/licenses'
    for d in /opt/ut-runtime-1x/usr/share/doc/*; do
      if [ -f \$d/copyright ]; then
        mkdir -p '$stage/licenses/'\$(basename \$d)
        cp \$d/copyright '$stage/licenses/'\$(basename \$d)/
      fi
    done
  fi
  sed -e 's|@NAME@|$name|' -e 's|@VERSION@|$version|' -e 's|@MAINTAINER@|$maintainer|' \
      -e 's|@SOURCE@|$source|' -e 's|@FRAMEWORK@|ubuntu-touch-$ut|' \
      app/click/manifest.json.in > '$stage/manifest.json'
  cd '$out' && click build --no-validate '/src/$stage'
"
ls -l "$root/$out/${name}_${version}_arm64.click"
