#!/bin/sh
# Builds Discord Messenger for Windows, x64 and ARM64, on Linux (WSL2
# included) or macOS, in a container: clang for the MSVC ABI, Microsoft's
# Windows SDK and C runtime fetched by xwin, and static OpenSSL, FreeType,
# libpng, zlib, libwebp and GLFW, each download checked by its SHA-256.
#
#   windows/build.sh [--accept-license] [--fonts DIR] [x86_64] [aarch64]
#
# --accept-license  accept Microsoft's licence for the SDK and C runtime
#                   (https://go.microsoft.com/fwlink/?LinkId=2086102); the
#                   first build needs it, to download them
# --fonts DIR       the fonts, as for macos/make-app.sh: Inter 4, DejaVu Sans
#                   (and Mono) and Noto Color Emoji, with Inter-LICENSE.txt
#
# Needs podman or docker.  Work files go to build-win/ (about 3 GB: the SDK
# and the libraries are built once); the zips and the installers (.msi) to
# bin/windows/.
set -eu
cd "$(dirname "$0")/.."
accept=0 fonts=${DM_FONTS:-} arches=
while [ $# -gt 0 ]; do
	case $1 in
	--accept-license) accept=1 ;;
	--fonts) fonts=$2; shift ;;
	x86_64|aarch64) arches="$arches $1" ;;
	*) sed -n 2,17p "$0"; exit 2 ;;
	esac
	shift
done
arches=${arches:-x86_64 aarch64}
[ -n "$fonts" ] && [ -f "$fonts/Inter-Regular.ttf" ] || { echo "--fonts DIR: a directory with the fonts (see the top of $0)"; exit 2; }
fonts=$(cd "$fonts" && pwd)

if command -v podman > /dev/null; then engine=podman; vol=:Z ro=:ro,Z; user=
elif command -v docker > /dev/null; then engine=docker; vol= ro=:ro; user="-u $(id -u):$(id -g)"
else echo "podman or docker is needed"; exit 1; fi

mkdir -p build-win bin/windows
# this machine's own platform, named: linux/build.sh's arm64 run leaves the
# base image's tag on arm64, and an unnamed platform would follow it
native=linux/$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')
$engine build -q --platform $native -t dm-win-cross windows > /dev/null
# the build number (the installer's version): the commits so far
build=${DM_BUILD:-$(git rev-list --count HEAD 2>/dev/null || echo 0)}
$engine run --rm $user -e DM_ACCEPT_LICENSE=$accept -e DM_BUILD=$build \
	-v "$PWD":/src$vol -v "$PWD/build-win":/work$vol -v "$fonts":/fonts$ro \
	dm-win-cross sh /src/windows/inside.sh $arches
cp build-win/dist/*.zip build-win/dist/*.msi bin/windows/
ls -l bin/windows/
