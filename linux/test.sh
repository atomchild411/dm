#!/bin/sh
# Installs each .deb in bin/linux into fresh Ubuntu 24.04 and Debian 13
# containers of its architecture (apt fetches what it depends on) and runs
# it once, hidden, under Xvfb: the size of the picture it saved says
# whether it drew the demo.
#
#   linux/test.sh [x86_64] [aarch64] [riscv64]
set -eu
cd "$(dirname "$0")/.."
arches=${*:-x86_64 aarch64 riscv64}
engine=$(command -v podman || command -v docker)
mkdir -p build-linux/test
for arch in $arches; do
	case $arch in x86_64) plat=linux/amd64 d=amd64 ;; aarch64) plat=linux/arm64 d=arm64 ;; riscv64) plat=linux/riscv64 d=riscv64 ;; esac
	deb=$(ls bin/linux/discord-messenger_*_$d.deb)
	for img in ubuntu:24.04 debian:trixie; do
		out=build-linux/test/$arch-$(echo $img | tr : -).png
		rm -f $out
		$engine run --rm --platform $plat -v "$PWD":/w:Z docker.io/library/$img sh -c "
			apt-get update -qq > /dev/null &&
			DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends /w/$deb xvfb xauth libgl1-mesa-dri > /dev/null 2>&1 &&
			mkdir -p /tmp/h && HOME=/tmp/h DM_SNAPSHOT=/w/$out DM_SNAPSHOT_AFTER=8 xvfb-run -a -s '-screen 0 1280x1024x24' discord-messenger --demo > /dev/null 2>&1;
			true"
		echo "$arch $img: $( [ -f $out ] && echo "$(wc -c < $out) bytes" || echo FAILED)"
	done
done
