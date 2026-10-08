#!/bin/sh
# Builds the Dear ImGui client as "Discord Messenger.app" in bin/.
#
#   macos/make-app.sh FONTS [PREFIX]
#
# FONTS     a directory with Inter-{Regular,SemiBold,Italic,SemiBoldItalic}.ttf,
#           DejaVuSans*.ttf, DejaVuSansMono*.ttf, NotoColorEmoji.ttf and
#           Inter-LICENSE.txt
# PREFIX    where OpenSSL, libwebp, FreeType, libpng and GLFW are, with their
#           static libraries: build-mac/pfx when macos/build-deps.sh made it
#           (universal, for macOS 11 on), else /opt/homebrew (this Mac's
#           architecture and macOS version only)
#
# Those libraries are linked in, so the app needs only macOS.  It is built
# for the architectures they have, and signed ad hoc (codesign -s -), as
# Apple Silicon requires of anything it runs.
set -eu
cd "$(dirname "$0")/.."
fonts=$1
if [ -n "${2:-}" ]; then prefix=$2
elif [ -f build-mac/pfx/.done ]; then prefix=$PWD/build-mac/pfx
else prefix=/opt/homebrew; fi
app="bin/Discord Messenger.app"

# the architectures the libraries have; macOS 11 on, with ours
archflags=
for a in $(lipo -archs "$prefix/lib/libssl.a"); do archflags="$archflags -arch $a"; done
minflag= objdir=build-unix/macapp
[ -f "$prefix/.done" ] && minflag=-mmacosx-version-min=11.0 objdir=build-unix/macapp-universal

make -j4 FRONTEND=imgui STATIC_DEPS=1 PREFIX_DEPS="$prefix" \
	CXX=clang++ CC=clang EXTRA_CXXFLAGS="$archflags $minflag" EXTRA_LDFLAGS="$archflags $minflag" \
	BUILD_DIR=$objdir TARGET=bin/dm-imgui-app \
	FT_CFLAGS="-I$prefix/include/freetype2" \
	FT_LIBS="$prefix/lib/libfreetype.a $prefix/lib/libpng16.a -lz -lbz2" \
	GLFW_LIBS="$prefix/lib/libglfw3.a -framework Cocoa -framework IOKit -framework CoreFoundation -framework QuartzCore"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/fonts" "$app/Contents/Resources/licenses"
cp bin/dm-imgui-app "$app/Contents/MacOS/Discord Messenger"

# the version: the release's, and the commit count as the build
build=$(git rev-list --count HEAD)
minos=$(vtool -show-build "$app/Contents/MacOS/Discord Messenger" 2>/dev/null | awk '/minos/ {print $2; exit}')
sed -e "s/@VERSION@/1.11/" -e "s/@BUILD@/$build/" -e "s/@MINOS@/${minos:-11.0}/" macos/Info.plist.in > "$app/Contents/Info.plist"
printf 'APPL????' > "$app/Contents/PkgInfo"

# the icon, from the app's .ico (64 px at most: larger sizes are scaled up)
tmp=$(mktemp -d)
sips -s format png irix/icon_discord.ico --out "$tmp/icon.png" > /dev/null
mkdir "$tmp/AppIcon.iconset"
for s in 16 32 64 128 256 512; do
	sips -z $s $s "$tmp/icon.png" --out "$tmp/AppIcon.iconset/icon_${s}x${s}.png" > /dev/null
	d=$((s * 2))
	sips -z $d $d "$tmp/icon.png" --out "$tmp/AppIcon.iconset/icon_${s}x${s}@2x.png" > /dev/null
done
rm "$tmp/AppIcon.iconset/icon_64x64.png" "$tmp/AppIcon.iconset/icon_64x64@2x.png"
iconutil -c icns "$tmp/AppIcon.iconset" -o "$app/Contents/Resources/AppIcon.icns"
rm -rf "$tmp"

# the fonts, and the licences of what is inside
for f in Inter-Regular.ttf Inter-SemiBold.ttf Inter-Italic.ttf Inter-SemiBoldItalic.ttf \
	DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSans-Oblique.ttf DejaVuSans-BoldOblique.ttf \
	DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf NotoColorEmoji.ttf; do
	cp "$fonts/$f" "$app/Contents/Resources/fonts/"
done
for l in irix/dist/licenses/*; do
	case $(basename "$l") in
	LLVM-*|bzip2-*|zlib-*|Mozilla-CA-*) ;; # macOS's own, or not inside
	*) cp "$l" "$app/Contents/Resources/licenses/" ;;
	esac
done
cp deps/imgui/LICENSE.txt "$app/Contents/Resources/licenses/Dear-ImGui-MIT"
cp "$prefix/GLFW-LICENSE.md" "$app/Contents/Resources/licenses/GLFW-zlib" 2>/dev/null ||
	cp "$prefix/opt/glfw/LICENSE.md" "$app/Contents/Resources/licenses/GLFW-zlib"
cp "$fonts/Inter-LICENSE.txt" "$app/Contents/Resources/licenses/Inter-OFL-1.1"

codesign --force --sign - "$app"
echo "$app"
