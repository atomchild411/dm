#!/bin/sh
# inside-deb.sh: the .deb, inside the Ubuntu 24.04 container linux/build.sh
# starts for one architecture (/src, /work, /fonts as for inside-portable.sh):
# the program against the distribution's OpenSSL, libwebp, FreeType and GLFW,
# installed in /usr, with its dependencies as dpkg-shlibdeps finds them.
# DM_BUILD is the version's third part.
set -eu
VERSION=1.11.${DM_BUILD:-0}
W=/work
debarch=$(dpkg --print-architecture)
mkdir -p $W/dist

echo "== Discord Messenger .deb ($debarch)"
make -C /src -j16 FRONTEND=imgui PREFIX_DEPS=/usr FT_CFLAGS=-I/usr/include/freetype2 \
	EXTRA_CXXFLAGS='-DDM_DATADIR=\"/usr/share/discord-messenger\"' \
	BUILD_DIR=$W/obj-deb TARGET=$W/out-deb/discord-messenger > $W/make-deb.log 2>&1 ||
	{ grep -E "error" $W/make-deb.log | head -20; echo "deb build ($debarch) FAILED"; exit 1; }

r=$W/debroot
rm -rf $r && mkdir -p $r/DEBIAN $r/usr/bin $r/usr/share/discord-messenger/fonts \
	$r/usr/share/applications $r/usr/share/icons/hicolor/64x64/apps $r/usr/share/doc/discord-messenger/licenses
install -m 755 -s $W/out-deb/discord-messenger $r/usr/bin/
for f in Inter-Regular.ttf Inter-SemiBold.ttf Inter-Italic.ttf Inter-SemiBoldItalic.ttf \
	DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSans-Oblique.ttf DejaVuSans-BoldOblique.ttf \
	DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf NotoColorEmoji.ttf; do
	install -m 644 /fonts/$f $r/usr/share/discord-messenger/fonts/
done
install -m 644 /src/linux/discord-messenger.desktop $r/usr/share/applications/
icotool -x -w 64 -o $r/usr/share/icons/hicolor/64x64/apps/discord-messenger.png /src/irix/icon_discord.ico 2>/dev/null
chmod 644 $r/usr/share/icons/hicolor/64x64/apps/discord-messenger.png
# the licences of what is inside (the libraries are the distribution's)
for l in Discord-Messenger-MIT DejaVu-fonts Noto-Color-Emoji-OFL-1.1 \
	nlohmann-json-MIT qrcodegen-MIT; do
	install -m 644 /src/irix/dist/licenses/$l $r/usr/share/doc/discord-messenger/licenses/
done
install -m 644 /src/deps/imgui/LICENSE.txt $r/usr/share/doc/discord-messenger/licenses/Dear-ImGui-MIT
install -m 644 /fonts/Inter-LICENSE.txt $r/usr/share/doc/discord-messenger/licenses/Inter-OFL-1.1
cat > $r/usr/share/doc/discord-messenger/copyright <<'COPYRIGHT'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Discord Messenger
Source: https://github.com/atomchild411/dm

Files: *
Copyright: iProgramInCpp and the Discord Messenger contributors
License: MIT
 See /usr/share/doc/discord-messenger/licenses/Discord-Messenger-MIT; the
 licences of the libraries and fonts inside are in that directory too.
COPYRIGHT
chmod 644 $r/usr/share/doc/discord-messenger/copyright

# the dependencies, from what the program links; and the root certificates
# it checks servers against
s=$W/shlibs && rm -rf $s && mkdir -p $s/debian
printf 'Source: discord-messenger\n\nPackage: discord-messenger\nArchitecture: any\n' > $s/debian/control
depends=$(cd $s && dpkg-shlibdeps -O -e $r/usr/bin/discord-messenger 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
cat > $r/DEBIAN/control <<CONTROL
Package: discord-messenger
Version: $VERSION
Architecture: $debarch
Maintainer: atomchild411 <143453386+atomchild411@users.noreply.github.com>
Installed-Size: $(du -sk $r/usr | cut -f1)
Depends: $depends, ca-certificates
Section: net
Priority: optional
Homepage: https://github.com/atomchild411/dm
Description: Discord-compatible messenger
 A messenger compatible with Discord, in the style of Discord's own client:
 servers, channels, direct messages, formatting, reactions, embeds, colour
 emoji and pictures.  Logs in with a QR code or a token.
 .
 Third-party clients are against Discord's terms of service.
CONTROL
deb=discord-messenger_${VERSION}_$debarch.deb
dpkg-deb --root-owner-group -Zxz --build $r $W/dist/$deb > /dev/null
echo "$W/dist/$deb"
echo "Depends: $depends, ca-certificates"
