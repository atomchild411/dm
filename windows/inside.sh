#!/bin/sh
# inside.sh ARCH...: the Windows build, inside the container windows/build.sh
# starts (/src = the source tree, /work = the work directory, /fonts = the
# fonts).  For each ARCH (x86_64, aarch64): the libraries, the program, and
# /work/dist/DiscordMessenger-VERSION-windows-{x64,arm64}.zip.
set -eu
VERSION=1.11
W=/work
mkdir -p $W/dl $W/dist

# fetch FILE URL SHA256: a download, checked
fetch() {
	if [ ! -f $W/dl/$1 ]; then
		curl -fsSL -o $W/dl/$1.part "$2" || { echo "could not download $2"; exit 1; }
		mv $W/dl/$1.part $W/dl/$1
	fi
	echo "$3  $W/dl/$1" | sha256sum -c --quiet - || { echo "checksum of $1 does not match"; rm -f $W/dl/$1; exit 1; }
}

# ---- the Windows SDK and CRT (xwin: accepting Microsoft's licence) ----------
if [ ! -d $W/xwin/crt ]; then
	if [ "${DM_ACCEPT_LICENSE:-}" != 1 ]; then
		echo "The Windows SDK and C runtime come from Microsoft, under Microsoft's licence"
		echo "(https://go.microsoft.com/fwlink/?LinkId=2086102).  Run windows/build.sh"
		echo "with --accept-license to accept it and download them."
		exit 1
	fi
	XV=0.10.0
	case $(uname -m) in
	x86_64) xa=x86_64 xs=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88 ;;
	aarch64|arm64) xa=aarch64 xs=6d56d28537a86f37aa3d041318898f25ee3100c6b6ec332ad873c28faf37be23 ;;
	*) echo "no xwin for $(uname -m)"; exit 1 ;;
	esac
	fetch xwin-$XV-$xa.tar.gz https://github.com/Jake-Shadle/xwin/releases/download/$XV/xwin-$XV-$xa-unknown-linux-musl.tar.gz $xs
	tar xzf $W/dl/xwin-$XV-$xa.tar.gz -C $W/dl
	echo "== the Windows SDK and CRT (xwin)"
	$W/dl/xwin-$XV-$xa-unknown-linux-musl/xwin --accept-license --cache-dir $W/xwin-cache \
		--arch x86_64,aarch64 splat --output $W/xwin > $W/xwin.log 2>&1 ||
		{ tail -20 $W/xwin.log; exit 1; }
	rm -rf $W/xwin-cache
fi

# ---- the libraries ------------------------------------------------------------
fetch zlib-1.3.2.tar.xz https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.xz \
	d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3
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

deps() {
	arch=$1 P=$W/pfx-$1 B=$W/deps-$1
	[ -f $P/.done ] && return 0
	rm -rf $P $B && mkdir -p $P $B
	CM="cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=/src/windows/toolchain.cmake -DWIN_ARCH=$arch \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_PREFIX_PATH=$P \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
	# cmk NAME DIR [ARGS...]: configure, build and install (its log on failure)
	cmk() {
		n=$1 d=$B/$2; shift 2
		{ $CM -S $d -B $d/b "$@" && cmake --build $d/b && cmake --install $d/b; } > $B/$n.log 2>&1 ||
			{ tail -30 $B/$n.log; echo "$n ($arch) FAILED"; exit 1; }
	}
	for f in zlib-1.3.2.tar.xz libpng-1.6.58.tar.xz freetype-2.14.3.tar.xz libwebp-1.6.0.tar.gz glfw-3.4.tar.gz openssl-3.6.4.tar.gz; do
		tar xf $W/dl/$f -C $B
	done
	echo "== libraries ($arch)"
	cmk zlib zlib-1.3.2 -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF
	cp $P/lib/zs.lib $P/lib/zlib.lib # the name FindZLIB looks for
	cmk png libpng-1.6.58 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DSKIP_INSTALL_FILES=ON
	cmk freetype freetype-2.14.3 -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON \
		-DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON
	cmk webp libwebp-1.6.0 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
		-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
	cmk glfw glfw-3.4 -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
	# OpenSSL: its MinGW targets use a Unix makefile, but the compiler is
	# still clang for the MSVC ABI.  The C code everywhere (no-asm).
	case $arch in x86_64) t=mingw64 ;; aarch64) t=mingwarm64 ;; esac
	( cd $B/openssl-3.6.4 &&
	  ./Configure $t no-asm no-shared no-apps no-tests no-docs no-module \
		--prefix=$P --libdir=lib CC=$arch-windows-clang AR=llvm-ar RANLIB=llvm-ranlib RC=llvm-rc \
		CFLAGS="-O2 -Wno-everything" &&
	  make -j$(nproc) build_libs && make install_dev ) > $B/openssl.log 2>&1 ||
		{ tail -30 $B/openssl.log; echo "openssl ($arch) FAILED"; exit 1; }
	rm -rf $B
	touch $P/.done
}

# ---- the program, and its zip -------------------------------------------------
for arch in "$@"; do
	case $arch in x86_64) name=x64 ;; aarch64) name=arm64 ;; *) echo "unknown arch $arch"; exit 1 ;; esac
	deps $arch
	echo "== Discord Messenger ($arch)"
	make -C /src -j$(nproc) FRONTEND=imgui TARGET_OS=windows CXX=$arch-windows-clang++ CC=$arch-windows-clang \
		PREFIX_DEPS=$W/pfx-$arch BUILD_DIR=$W/obj-$arch TARGET=$W/out-$arch/dm-imgui.exe > $W/make-$arch.log 2>&1 ||
		{ grep -E "error" $W/make-$arch.log | head -20; echo "build ($arch) FAILED"; exit 1; }
	llvm-strip $W/out-$arch/dm-imgui.exe

	pkg=DiscordMessenger-$VERSION-windows-$name
	d=$W/pkg/$pkg
	rm -rf $d && mkdir -p $d/fonts $d/licenses
	cp $W/out-$arch/dm-imgui.exe "$d/Discord Messenger.exe"
	for f in Inter-Regular.ttf Inter-SemiBold.ttf Inter-Italic.ttf Inter-SemiBoldItalic.ttf \
		DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSans-Oblique.ttf DejaVuSans-BoldOblique.ttf \
		DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf NotoColorEmoji.ttf; do
		cp /fonts/$f $d/fonts/
	done
	for l in /src/irix/dist/licenses/*; do
		case $(basename $l) in
		LLVM-*|bzip2-*|Mozilla-CA-*) ;; # not inside the Windows build
		*) cp $l $d/licenses/ ;;
		esac
	done
	# Microsoft's C++ library is linked in: its licence is the Apache 2.0
	# licence with the LLVM exception, as libc++'s
	cp /src/irix/dist/licenses/LLVM-libcxx-Apache-2.0-with-LLVM-exception $d/licenses/Microsoft-STL-Apache-2.0-with-LLVM-exception
	cp /src/deps/imgui/LICENSE.txt $d/licenses/Dear-ImGui-MIT
	tar xzf $W/dl/glfw-3.4.tar.gz -O glfw-3.4/LICENSE.md > $d/licenses/GLFW-zlib
	cp /fonts/Inter-LICENSE.txt $d/licenses/Inter-OFL-1.1
	cp /src/windows/README.txt $d/README.txt
	( cd $W/pkg && rm -f $W/dist/$pkg.zip && zip -qr $W/dist/$pkg.zip $pkg )
	echo "$W/dist/$pkg.zip"
done
