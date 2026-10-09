#!/bin/sh
# Builds Discord Messenger's Linux packages, in containers (podman or
# docker), for x86_64 and aarch64 (the one this machine is not under
# emulation, if it has QEMU's user mode registered through binfmt_misc):
#
#   linux/build.sh --fonts DIR [x86_64] [aarch64]
#
# For each architecture, in bin/linux/:
#   DiscordMessenger-1.11-linux-ARCH.tar.gz  to unpack anywhere; built on
#       Ubuntu 22.04 with OpenSSL, FreeType, libpng, libwebp and GLFW linked
#       in, so it runs on distributions from about 2022 on
#   DiscordMessenger-1.11-linux-ARCH.AppImage  the same as one file
#   discord-messenger_1.11.N_ARCH.deb  for Ubuntu 24.04 and later and Debian
#       13, with their own libraries
# --fonts DIR: the fonts, as for macos/make-app.sh.  Work files go to
# build-linux/.
set -eu
cd "$(dirname "$0")/.."
fonts=${DM_FONTS:-} arches=
while [ $# -gt 0 ]; do
	case $1 in
	--fonts) fonts=$2; shift ;;
	x86_64|aarch64) arches="$arches $1" ;;
	*) sed -n 2,21p "$0"; exit 2 ;;
	esac
	shift
done
arches=${arches:-x86_64 aarch64}
[ -n "$fonts" ] && [ -f "$fonts/Inter-Regular.ttf" ] || { echo "--fonts DIR: a directory with the fonts (see the top of $0)"; exit 2; }
fonts=$(cd "$fonts" && pwd)

if command -v podman > /dev/null; then engine=podman; vol=:Z ro=:ro,Z; user=
elif command -v docker > /dev/null; then engine=docker; vol= ro=:ro; user="-u $(id -u):$(id -g)"
else echo "podman or docker is needed"; exit 1; fi

build=${DM_BUILD:-$(git rev-list --count HEAD 2>/dev/null || echo 0)}
mkdir -p build-linux/dl bin/linux
for arch in $arches; do
	case $arch in x86_64) plat=linux/amd64 ;; aarch64) plat=linux/arm64 ;; esac
	mkdir -p build-linux/$arch
	$engine build -q --platform $plat -t dm-linux-portable-$arch -f linux/Containerfile linux > /dev/null
	$engine build -q --platform $plat -t dm-linux-deb-$arch -f linux/Containerfile.deb linux > /dev/null
	for kind in portable deb; do
		$engine run --rm --platform $plat $user -e DM_BUILD=$build \
			-v "$PWD":/src$vol -v "$PWD/build-linux/$arch":/work$vol -v "$PWD/build-linux/dl":/dl$vol \
			-v "$fonts":/fonts$ro dm-linux-$kind-$arch sh /src/linux/inside-$kind.sh
	done
	cp build-linux/$arch/dist/*.tar.gz build-linux/$arch/dist/*.deb bin/linux/
	cp build-linux/$arch/dist/*.AppImage bin/linux/ 2>/dev/null || true
done
ls -l bin/linux/
