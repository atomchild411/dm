#!/bin/sh
# The macOS release: the libraries (macos/build-deps.sh, once), the app
# (macos/make-app.sh: universal, macOS 11 on) and a disk image with it, a
# link to Applications and a note on opening it, in bin/macos/.
#
#   macos/release.sh FONTS
set -eu
cd "$(dirname "$0")/.."
fonts=${1:?"usage: $0 FONTS (the fonts directory, as for make-app.sh)"}
macos/build-deps.sh
macos/make-app.sh "$fonts"

name=DiscordMessenger-1.11-macos
tmp=$(mktemp -d)
mkdir "$tmp/Discord Messenger"
cp -R "bin/Discord Messenger.app" "$tmp/Discord Messenger/"
ln -s /Applications "$tmp/Discord Messenger/Applications"
cp macos/ReadMeFirst.txt "$tmp/Discord Messenger/Read Me First.txt"
mkdir -p bin/macos
rm -f bin/macos/$name.dmg
hdiutil create -quiet -volname "Discord Messenger" -srcfolder "$tmp/Discord Messenger" \
	-fs HFS+ -format UDZO -ov bin/macos/$name.dmg
rm -rf "$tmp"
ls -l bin/macos/$name.dmg
