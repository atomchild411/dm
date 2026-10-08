#!/bin/sh
# inside-portable.sh: the portable Linux build, inside the container
# linux/build.sh starts for one architecture (/src = the source tree, /work =
# this architecture's work directory, /dl = the downloads, /fonts = the
# fonts): the libraries, linked in; the program; and in /work/dist
# DiscordMessenger-VERSION-linux-ARCH.tar.gz and (where AppImage has a
# runtime: x86_64, aarch64) .AppImage, each run once, hidden, under Xvfb.
set -eu
VERSION=1.11
W=/work
arch=$(uname -m)
mkdir -p $W/dist

fetch() {
	if [ ! -f /dl/$1 ]; then
		curl -fsSL -o /dl/$1.part.$arch "$2" || { echo "could not download $2"; exit 1; }
		mv /dl/$1.part.$arch /dl/$1
	fi
	echo "$3  /dl/$1" | sha256sum -c --quiet - || { echo "checksum of $1 does not match"; exit 1; }
}
fetch libpng-1.6.58.tar.xz https://download.sourceforge.net/libpng/libpng-1.6.58.tar.xz \
	28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775
fetch freetype-2.14.3.tar.xz https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
	36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
fetch libwebp-1.6.0.tar.gz https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz \
	e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
fetch glfw-3.4.tar.gz https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz \
	c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
fetch openssl-3.6.4.tar.gz https://github.com/openssl/openssl/releases/download/openssl-3.6.4/openssl-3.6.4.tar.gz \
	9bffaa1ad1e07b354c21bd3324ec02fa15579f45a7d0494b3e74bc449b7333ef
# the AppImage runtime (AppImage/type2-runtime release 20251108)
case $arch in
x86_64) fetch runtime-x86_64 https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
	2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d ;;
aarch64) fetch runtime-aarch64 https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-aarch64 \
	00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 ;;
esac

# ---- the libraries, static -----------------------------------------------
P=$W/pfx B=$W/deps
if [ ! -f $P/.done ]; then
	rm -rf $P $B && mkdir -p $P $B
	for f in libpng-1.6.58.tar.xz freetype-2.14.3.tar.xz libwebp-1.6.0.tar.gz glfw-3.4.tar.gz openssl-3.6.4.tar.gz; do
		tar xf /dl/$f -C $B
	done
	CM="cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_PREFIX_PATH=$P \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
	cmk() {
		n=$1 d=$B/$2; shift 2
		{ $CM -S $d -B $d/b "$@" && cmake --build $d/b -j16 && cmake --install $d/b; } > $B/$n.log 2>&1 ||
			{ tail -30 $B/$n.log; echo "$n ($arch) FAILED"; exit 1; }
	}
	echo "== libraries ($arch)"
	cmk png libpng-1.6.58 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DSKIP_INSTALL_FILES=ON
	cmk freetype freetype-2.14.3 -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON \
		-DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON
	cmk webp libwebp-1.6.0 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
		-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
	# X11 and Wayland both: GLFW loads either's libraries when it runs
	cmk glfw glfw-3.4 -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
	( cd $B/openssl-3.6.4 &&
	  ./Configure no-shared no-apps no-tests no-docs no-module --prefix=$P --libdir=lib &&
	  make -j16 build_libs && make install_dev ) > $B/openssl.log 2>&1 ||
		{ tail -30 $B/openssl.log; echo "openssl ($arch) FAILED"; exit 1; }
	tar xzf /dl/glfw-3.4.tar.gz -O glfw-3.4/LICENSE.md > $P/GLFW-LICENSE.md
	rm -rf $B
	touch $P/.done
fi

# ---- the program ------------------------------------------------------------
echo "== Discord Messenger ($arch)"
make -C /src -j16 FRONTEND=imgui STATIC_DEPS=1 PREFIX_DEPS=$P \
	FT_CFLAGS=-I$P/include/freetype2 FT_LIBS="$P/lib/libfreetype.a $P/lib/libpng16.a -lz" \
	GLFW_LIBS="$P/lib/libglfw3.a -lm -ldl -lpthread -lrt" \
	EXTRA_LDFLAGS="-static-libstdc++ -static-libgcc" \
	BUILD_DIR=$W/obj TARGET=$W/out/discord-messenger > $W/make.log 2>&1 ||
	{ grep -E "error" $W/make.log | head -20; echo "build ($arch) FAILED"; exit 1; }
strip $W/out/discord-messenger

# ---- the .tar.gz --------------------------------------------------------------
pkg=DiscordMessenger-$VERSION-linux-$arch
d=$W/pkg/$pkg
rm -rf $W/pkg && mkdir -p $d/fonts $d/licenses
cp $W/out/discord-messenger $d/
for f in Inter-Regular.ttf Inter-SemiBold.ttf Inter-Italic.ttf Inter-SemiBoldItalic.ttf \
	DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSans-Oblique.ttf DejaVuSans-BoldOblique.ttf \
	DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf NotoColorEmoji.ttf; do
	cp /fonts/$f $d/fonts/
done
for l in /src/irix/dist/licenses/*; do
	case $(basename $l) in
	LLVM-*|bzip2-*|zlib-*|Mozilla-CA-*) ;; # not inside this build (zlib is the system's)
	*) cp $l $d/licenses/ ;;
	esac
done
cp /src/deps/imgui/LICENSE.txt $d/licenses/Dear-ImGui-MIT
cp $P/GLFW-LICENSE.md $d/licenses/GLFW-zlib
cp /fonts/Inter-LICENSE.txt $d/licenses/Inter-OFL-1.1
cp /src/linux/README.txt /src/linux/discord-messenger.desktop $d/
icotool -x -w 64 -o $d/discord-messenger.png /src/irix/icon_discord.ico 2>/dev/null
( cd $W/pkg && tar czf $W/dist/$pkg.tar.gz --owner=0 --group=0 $pkg )
echo "$W/dist/$pkg.tar.gz"

# ---- the AppImage: the runtime, then the program's folder as a squashfs -----
if [ -f /dl/runtime-$arch ]; then
	a=$W/AppDir
	rm -rf $a && cp -R $d $a
	ln -s discord-messenger $a/AppRun
	ln -s discord-messenger.png $a/.DirIcon
	rm -f $W/app.squashfs
	mksquashfs $a $W/app.squashfs -root-owned -noappend -comp zstd -quiet > /dev/null
	cat /dl/runtime-$arch $W/app.squashfs > $W/dist/$pkg.AppImage
	chmod 755 $W/dist/$pkg.AppImage
	rm -rf $a $W/app.squashfs
	echo "$W/dist/$pkg.AppImage"
fi

# ---- a test run of each, hidden, under Xvfb ---------------------------------
Xvfb :7 -screen 0 1280x1024x24 -nolisten tcp > /dev/null 2>&1 &
xvfb=$!
sleep 2
t=$W/test && rm -rf $t && mkdir -p $t/home
tar xzf $W/dist/$pkg.tar.gz -C $t
DISPLAY=:7 HOME=$t/home DM_SNAPSHOT=$W/dist/test-tar.png DM_SNAPSHOT_AFTER=6 \
	$t/$pkg/discord-messenger --demo > $W/test-tar.log 2>&1 || true
if [ -f $W/dist/$pkg.AppImage ]; then
	DISPLAY=:7 HOME=$t/home APPIMAGE_EXTRACT_AND_RUN=1 TMPDIR=$t DM_SNAPSHOT=$W/dist/test-appimage.png DM_SNAPSHOT_AFTER=6 \
		$W/dist/$pkg.AppImage --demo > $W/test-appimage.log 2>&1 || true
fi
kill $xvfb
rm -rf $t
for p in $W/dist/test-*.png; do echo "test: $(basename $p) $(stat -c %s $p) bytes"; done
