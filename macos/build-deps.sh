#!/bin/sh
# Builds the libraries the macOS app links, as static universal libraries
# (arm64 and x86_64) for macOS 11 and later, into build-mac/pfx:
# OpenSSL, FreeType, libpng, libwebp and GLFW (zlib is the system's), from
# their release tarballs, each checked by its SHA-256, with the fixes in
# deps/patches.  Again only when those change; macos/release.sh runs it.
#
#   macos/build-deps.sh
set -eu
cd "$(dirname "$0")/.."
W=$PWD/build-mac
MIN=11.0
P=$W/pfx
# what the libraries are built from: a build is reused while it matches
stamp=$(grep '^fetch ' "$0" | cat - deps/patches/*/*.patch | shasum -a 256 | cut -c1-16)
[ "$(cat $P/.done 2>/dev/null)" = "$stamp" ] && exit 0
mkdir -p $W/dl
export MACOSX_DEPLOYMENT_TARGET=$MIN

fetch() {
	if [ ! -f $W/dl/$1 ]; then
		curl -fsSL -o $W/dl/$1.part "$2" || { echo "could not download $2"; exit 1; }
		mv $W/dl/$1.part $W/dl/$1
	fi
	echo "$3  $W/dl/$1" | shasum -a 256 -c --quiet - || { echo "checksum of $1 does not match"; rm -f $W/dl/$1; exit 1; }
}
fetch libpng-1.6.58.tar.xz https://download.sourceforge.net/libpng/libpng-1.6.58.tar.xz \
	28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775
fetch freetype-2.14.3.tar.xz https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
	36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
fetch libwebp-1.6.0.tar.gz https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz \
	e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
fetch glfw-3.4.tar.gz https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz \
	c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
fetch openssl-3.6.5.tar.gz https://github.com/openssl/openssl/releases/download/openssl-3.6.5/openssl-3.6.5.tar.gz \
	a2157c2830efdec3788939b00c9b0638306d3f0bbb76dc4832ee503bb397df98

# Each architecture on its own (the libraries' SIMD code picks by it), then
# the two put together with lipo.
for arch in arm64 x86_64; do
	A=$W/pfx-$arch B=$W/deps-$arch
	[ "$(cat $A/.done 2>/dev/null)" = "$stamp" ] && continue
	rm -rf $A $B && mkdir -p $A $B
	for f in libpng-1.6.58.tar.xz freetype-2.14.3.tar.xz libwebp-1.6.0.tar.gz glfw-3.4.tar.gz openssl-3.6.5.tar.gz; do
		tar xf $W/dl/$f -C $B
	done
	for d in $B/*; do
		for f in deps/patches/${d##*/}/*.patch; do [ -f "$f" ] && patch -s -p1 -d $d < $f; done
	done
	CM="cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$A -DCMAKE_PREFIX_PATH=$A \
		-DCMAKE_OSX_ARCHITECTURES=$arch -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN -DBUILD_SHARED_LIBS=OFF \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_FIND_FRAMEWORK=NEVER"
	cmk() {
		n=$1 d=$B/$2; shift 2
		{ $CM -S $d -B $d/b "$@" && cmake --build $d/b -j4 && cmake --install $d/b; } > $B/$n.log 2>&1 ||
			{ tail -30 $B/$n.log; echo "$n ($arch) FAILED"; exit 1; }
	}
	echo "== libraries ($arch)"
	cmk png libpng-1.6.58 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF -DSKIP_INSTALL_FILES=ON
	cmk freetype freetype-2.14.3 -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON \
		-DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON
	cmk webp libwebp-1.6.0 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
		-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
	cmk glfw glfw-3.4 -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
	case $arch in arm64) t=darwin64-arm64-cc ;; x86_64) t=darwin64-x86_64-cc ;; esac
	( cd $B/openssl-3.6.5 &&
	  ./Configure $t no-shared no-apps no-tests no-docs no-module --prefix=$A --libdir=lib \
		-mmacosx-version-min=$MIN &&
	  make -j4 build_libs && make install_dev ) > $B/openssl.log 2>&1 ||
		{ tail -30 $B/openssl.log; echo "openssl ($arch) FAILED"; exit 1; }
	tar xzf $W/dl/glfw-3.4.tar.gz -O glfw-3.4/LICENSE.md > $A/GLFW-LICENSE.md
	rm -rf $B
	echo $stamp > $A/.done
done

echo "== universal libraries"
rm -rf $P && mkdir -p $P/lib
cp -R $W/pfx-arm64/include $P/
for l in libssl.a libcrypto.a libfreetype.a libpng16.a libwebp.a libsharpyuv.a libglfw3.a; do
	lipo -create $W/pfx-arm64/lib/$l $W/pfx-x86_64/lib/$l -output $P/lib/$l
done
cp $W/pfx-arm64/GLFW-LICENSE.md $P/
echo $stamp > $P/.done
